package com.msgbyte.dao.browser

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.webkit.MimeTypeMap
import androidx.annotation.RequiresApi
import androidx.core.content.FileProvider
import java.io.File
import java.io.IOException
import java.io.OutputStream
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import mozilla.components.concept.fetch.Response
import mozilla.components.support.utils.DownloadUtils
import org.json.JSONArray
import org.json.JSONObject

data class ActiveDownload(
    val id: Long,
    val fileName: String,
    val bytesDownloaded: Long,
    val totalBytes: Long?,
)

/**
 * Saves browser downloads from the response Gecko already opened, so cookies, POST results and
 * blob:/data: URLs work; a retry re-fetches through Gecko's networking with the browser's cookies.
 * Ids are negative so they never collide with DownloadManager row ids.
 *
 * ponytail: a dropped connection fails the download and Retry restarts it from byte zero; add
 * Range-based resume if large downloads over flaky networks matter.
 */
internal class StreamedDownloadGateway(
    private val sink: DownloadSink,
    private val recordStore: StreamedDownloadRecordStore,
    private val fetch: (DownloadRequestData) -> Response,
    private val scope: CoroutineScope,
    private val onStarted: () -> Unit = {},
    private val onFinished: (DownloadRequestData, DownloadGatewayRecord) -> Unit = { _, _ -> },
    private val onRemoved: (id: Long) -> Unit = {},
    private val clock: () -> Long = System::currentTimeMillis,
) : DownloadGateway {
    private class Stream(
        val request: DownloadRequestData,
        @Volatile var record: DownloadGatewayRecord,
        @Volatile var response: Response? = null,
        @Volatile var target: DownloadTarget? = null,
        @Volatile var job: Job? = null,
    )

    private val lock = Any()
    private val streams = recordStore.readAll().mapValuesTo(mutableMapOf()) { (_, record) ->
        // A stream cannot outlive its process; anything unfinished at startup has failed.
        val finished = record.status == DownloadGatewayStatus.SUCCESSFUL ||
            record.status == DownloadGatewayStatus.FAILED
        Stream(PERSISTED_REQUEST, if (finished) record else record.copy(status = DownloadGatewayStatus.FAILED))
    }
    private var lastId = streams.keys.minOrNull() ?: 0L
    private val mutableActive = MutableStateFlow<List<ActiveDownload>>(emptyList())

    /** Downloads currently transferring, for the foreground notification. */
    val active: StateFlow<List<ActiveDownload>> = mutableActive.asStateFlow()

    override fun enqueue(request: DownloadRequestData, response: Response?): Long {
        val stream = synchronized(lock) {
            // Ids only ever decrease, also across restarts, so a stale Retry tap or an old metadata row
            // never matches a newer download.
            val id = minOf(-clock(), lastId - 1).also { lastId = it }
            Stream(
                request = request,
                record = DownloadGatewayRecord(
                    id = id,
                    status = DownloadGatewayStatus.PENDING,
                    bytesDownloaded = 0,
                    totalBytes = request.contentLength ?: -1,
                    localUri = null,
                    reason = 0,
                    lastModified = clock(),
                ),
                response = response,
            ).also { streams[id] = it }
        }
        // Persisted right away so a process death leaves a failed row instead of a missing one.
        persist()
        publishActive()
        // Without the foreground service (e.g. started from the background) the transfer still runs.
        runCatching(onStarted)
        stream.job = scope.launch { transfer(stream) }
        return stream.record.id
    }

    override fun query(ids: Set<Long>): List<DownloadGatewayRecord> = synchronized(lock) {
        ids.mapNotNull { streams[it]?.record }
    }

    override fun remove(id: Long): Boolean {
        val stream = synchronized(lock) { streams.remove(id) } ?: return false
        stream.job?.cancel()
        // Closing unblocks a read that is waiting on the network.
        runCatching { stream.response?.close() }
        val fileDeleted = stream.record.localUri?.let { runCatching { sink.delete(it) }.getOrDefault(false) }
        if (fileDeleted == null) runCatching { stream.target?.delete() }
        persist()
        publishActive()
        runCatching { onRemoved(id) }
        return fileDeleted == true
    }

    private suspend fun transfer(stream: Stream) {
        val id = stream.record.id
        val job = currentCoroutineContext()[Job]
        var bytes = 0L
        val target = try {
            val geckoResponse = stream.response
            val response = geckoResponse ?: fetch(stream.request).also { fetched ->
                stream.response = fetched
                // Gecko only hands over responses it decided to download; a re-fetch must succeed.
                check(fetched.status in 200..299) { "Download request failed with HTTP ${fetched.status}" }
                // An expired session or a POST-only endpoint answers a plain GET with a web page.
                check(
                    !fetched.headers["Content-Type"].orEmpty().startsWith("text/html", ignoreCase = true) ||
                        stream.request.contentType.orEmpty().startsWith("text/html", ignoreCase = true),
                ) { "The re-fetched download is a web page" }
            }
            // Content-Length counts encoded bytes, which a decoded body need not match. A re-fetch may
            // also serve a different size than the response the request was recorded from.
            val encoded = response.headers["Content-Encoding"]
                ?.let { !it.equals("identity", ignoreCase = true) } == true
            val expected = if (encoded) {
                null
            } else {
                response.headers["Content-Length"]?.toLongOrNull()
                    ?: stream.request.contentLength.takeIf { geckoResponse != null }
            }
            val target = sink.create(stream.request.fileName).also { stream.target = it }
            update(id) { it.copy(status = DownloadGatewayStatus.RUNNING, totalBytes = expected ?: -1) }
            var lastPublished = 0L
            response.use {
                it.body.useStream { input ->
                    target.open().use { output ->
                        val buffer = ByteArray(BUFFER_SIZE)
                        while (true) {
                            job?.ensureActive()
                            val read = input.read(buffer)
                            if (read < 0) break
                            output.write(buffer, 0, read)
                            bytes += read
                            update(id) { record -> record.copy(bytesDownloaded = bytes) }
                            val now = clock()
                            if (now - lastPublished >= PUBLISH_INTERVAL_MS) {
                                lastPublished = now
                                publishActive()
                            }
                        }
                    }
                }
            }
            // A dropped connection can end the body early without an error.
            if (expected != null && bytes < expected) throw IOException("Download ended after $bytes of $expected bytes")
            target.apply { publish() }
        } catch (error: Throwable) {
            runCatching { stream.response?.close() }
            runCatching { stream.target?.delete() }
            if (error is CancellationException) return
            finish(stream) { it.copy(status = DownloadGatewayStatus.FAILED) }
            return
        }
        finish(stream) {
            it.copy(status = DownloadGatewayStatus.SUCCESSFUL, totalBytes = bytes, localUri = target.uri)
        }
    }

    private fun update(id: Long, transform: (DownloadGatewayRecord) -> DownloadGatewayRecord) {
        synchronized(lock) { streams[id]?.let { it.record = transform(it.record) } }
    }

    private fun finish(stream: Stream, transform: (DownloadGatewayRecord) -> DownloadGatewayRecord) {
        val record = synchronized(lock) {
            // A removed stream stays removed even if its transfer was mid-flight.
            if (streams[stream.record.id] !== stream) return
            stream.record = transform(stream.record).copy(lastModified = clock())
            stream.response = null
            stream.record
        }
        persist()
        publishActive()
        // A notification failure must not crash the app or undo a finished download.
        runCatching { onFinished(stream.request, record) }
    }

    // Both snapshot and write under the lock so concurrent transfers cannot publish stale state last.
    private fun persist() = synchronized(lock) {
        // Private downloads never reach disk; their rows vanish with the process.
        recordStore.writeAll(streams.values.filterNot { it.request.isPrivate }.associate { it.record.id to it.record })
    }

    private fun publishActive() = synchronized(lock) {
        mutableActive.value = streams.values
            .filter {
                it.record.status == DownloadGatewayStatus.PENDING ||
                    it.record.status == DownloadGatewayStatus.RUNNING
            }
            .map {
                ActiveDownload(
                    id = it.record.id,
                    fileName = it.request.fileName,
                    bytesDownloaded = it.record.bytesDownloaded,
                    totalBytes = it.record.totalBytes.takeIf { total -> total > 0 },
                )
            }
    }

    private companion object {
        const val BUFFER_SIZE = 64 * 1024
        const val PUBLISH_INTERVAL_MS = 500L

        // Restored rows only need their record; the request lives in the repository metadata.
        val PERSISTED_REQUEST = DownloadRequestData(url = "", fileName = "")
    }
}

/**
 * Names a Gecko download from Content-Disposition or its URL, adding an extension for its MIME type
 * when the name has none; blob: and data: URLs carry no usable name of their own.
 */
internal fun guessDownloadFileName(contentDisposition: String?, url: String?, mimeType: String?): String {
    val pageOnly = url == null || url.startsWith("blob:", ignoreCase = true) || url.startsWith("data:", ignoreCase = true)
    val name = if (pageOnly && contentDisposition == null) "download" else DownloadUtils.extractFileNameFromUrl(contentDisposition, url)
    val extension = mimeType?.substringBefore(';')?.trim()?.let(MimeTypeMap.getSingleton()::getExtensionFromMimeType)
    return if ('.' in name || extension == null) name else "$name.$extension"
}

internal interface DownloadSink {
    fun create(fileName: String): DownloadTarget

    /** Deletes a file previously published at [uri], reporting whether it existed. */
    fun delete(uri: String): Boolean
}

internal interface DownloadTarget {
    /** The content:// URI other apps open the finished file through. */
    val uri: String

    fun open(): OutputStream

    /** Makes the finished file visible to other apps. */
    fun publish()

    fun delete(): Boolean
}

internal interface StreamedDownloadRecordStore {
    fun readAll(): Map<Long, DownloadGatewayRecord>
    fun writeAll(records: Map<Long, DownloadGatewayRecord>)
}

/** Public Downloads through MediaStore on Android 10+, the app's own download folder before. */
internal class AndroidDownloadSink(context: Context) : DownloadSink {
    private val appContext = context.applicationContext
    private val resolver = appContext.contentResolver

    override fun create(fileName: String): DownloadTarget =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) mediaStoreTarget(fileName) else legacyTarget(fileName)

    override fun delete(uri: String): Boolean {
        val parsed = Uri.parse(uri)
        return if (parsed.authority == MediaStore.AUTHORITY) {
            resolver.delete(parsed, null, null) > 0
        } else {
            val name = parsed.lastPathSegment ?: return false
            File(legacyDirectory(), name).delete()
        }
    }

    @RequiresApi(Build.VERSION_CODES.Q)
    private fun mediaStoreTarget(fileName: String): DownloadTarget {
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, fileName)
            // A MIME type that matches the extension keeps MediaStore from renaming the file.
            put(MediaStore.MediaColumns.MIME_TYPE, mimeTypeFor(fileName))
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val uri = checkNotNull(resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)) {
            "MediaStore refused the download"
        }
        return object : DownloadTarget {
            override val uri = uri.toString()

            override fun open(): OutputStream = checkNotNull(resolver.openOutputStream(uri, "w"))

            override fun publish() {
                resolver.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null)
            }

            override fun delete() = resolver.delete(uri, null, null) > 0
        }
    }

    private fun legacyTarget(fileName: String): DownloadTarget {
        val directory = legacyDirectory().apply { mkdirs() }
        val base = fileName.substringBeforeLast('.')
        val extension = fileName.substringAfterLast('.', "").let { if (it.isEmpty()) "" else ".$it" }
        val file = generateSequence(0) { it + 1 }
            .map { index -> File(directory, if (index == 0) fileName else "$base ($index)$extension") }
            // createNewFile is atomic, so concurrent downloads of the same name get distinct files.
            .first { it.createNewFile() }
        return object : DownloadTarget {
            override val uri = FileProvider.getUriForFile(appContext, "${appContext.packageName}.updates", file).toString()

            override fun open(): OutputStream = file.outputStream()

            override fun publish() = Unit

            override fun delete() = file.delete()
        }
    }

    private fun legacyDirectory(): File =
        checkNotNull(appContext.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)) {
            "External storage is unavailable"
        }

    private fun mimeTypeFor(fileName: String): String =
        MimeTypeMap.getSingleton()
            .getMimeTypeFromExtension(fileName.substringAfterLast('.', "").lowercase())
            ?: "application/octet-stream"
}

internal class SharedPreferencesStreamedDownloadRecordStore(context: Context) : StreamedDownloadRecordStore {
    private val preferences = context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)

    override fun readAll(): Map<Long, DownloadGatewayRecord> {
        val raw = preferences.getString(KEY_RECORDS, null) ?: return emptyMap()
        return runCatching {
            val array = JSONArray(raw)
            (0 until array.length()).associate { index ->
                val value = array.getJSONObject(index)
                val id = value.getLong("id")
                id to DownloadGatewayRecord(
                    id = id,
                    status = DownloadGatewayStatus.valueOf(value.getString("status")),
                    bytesDownloaded = value.getLong("bytesDownloaded"),
                    totalBytes = value.getLong("totalBytes"),
                    localUri = if (value.isNull("localUri")) null else value.getString("localUri"),
                    reason = 0,
                    lastModified = value.getLong("lastModified"),
                )
            }
        }.getOrDefault(emptyMap())
    }

    override fun writeAll(records: Map<Long, DownloadGatewayRecord>) {
        val array = JSONArray()
        records.values.forEach { record ->
            array.put(
                JSONObject().apply {
                    put("id", record.id)
                    put("status", record.status.name)
                    put("bytesDownloaded", record.bytesDownloaded)
                    put("totalBytes", record.totalBytes)
                    put("localUri", record.localUri ?: JSONObject.NULL)
                    put("lastModified", record.lastModified)
                },
            )
        }
        preferences.edit().putString(KEY_RECORDS, array.toString()).apply()
    }

    private companion object {
        const val PREFERENCES_NAME = "dao-streamed-downloads"
        const val KEY_RECORDS = "records"
    }
}
