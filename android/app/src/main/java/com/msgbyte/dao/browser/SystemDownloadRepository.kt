package com.msgbyte.dao.browser

import android.app.DownloadManager
import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.SystemClock
import java.net.URI
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

class SystemDownloadRepository internal constructor(
    private val gateway: DownloadGateway,
    private val metadataStore: DownloadMetadataStore,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val elapsedRealtime: () -> Long = SystemClock::elapsedRealtime,
) {
    constructor(context: Context) : this(
        gateway = AndroidDownloadGateway(context.applicationContext),
        metadataStore = SharedPreferencesDownloadMetadataStore(context.applicationContext),
    )

    private var metadata = metadataStore.readAll().toMutableMap()
    private var lastRefreshTime: Long? = null
    private val lastProgressTimes = mutableMapOf<Long, Long>()
    private val mutableDownloads = MutableStateFlow(
        metadata.map { (id, request) -> request.toPendingDownload(id) },
    )

    val downloads: StateFlow<List<BrowserDownload>> = mutableDownloads.asStateFlow()

    suspend fun enqueue(request: DownloadRequestData): Long {
        require(isHttpUrl(request.url)) { "Only HTTP and HTTPS resources can be downloaded" }
        val normalized = request.copy(fileName = sanitizeFileName(request.fileName, request.url))
        return withContext(ioDispatcher) {
            val id = gateway.enqueue(normalized)
            metadata[id] = normalized
            persistMetadata()
            mutableDownloads.value = listOf(normalized.toPendingDownload(id)) + mutableDownloads.value
            id
        }
    }

    suspend fun refresh() {
        withContext(ioDispatcher) {
            if (metadata.isEmpty()) {
                mutableDownloads.value = emptyList()
                lastRefreshTime = null
                lastProgressTimes.clear()
                return@withContext
            }
            val records = try {
                gateway.query(metadata.keys)
            } catch (error: Exception) {
                lastRefreshTime = null
                lastProgressTimes.clear()
                mutableDownloads.value = mutableDownloads.value.map { it.copy(bytesPerSecond = null) }
                throw error
            }
            val now = elapsedRealtime()
            val elapsed = lastRefreshTime?.let { now - it }
            val previousDownloads = mutableDownloads.value.associateBy { it.id }
            val recordsById = records.associateBy { it.id }
            mutableDownloads.value = metadata.map { (id, request) ->
                val download = recordsById[id]?.toBrowserDownload(request) ?: request.toPendingDownload(id)
                val previous = previousDownloads[id]
                val progressElapsed = lastProgressTimes[id]?.let { now - it }
                val speed = if (
                    download.status == DownloadStatus.RUNNING &&
                    previous?.status == DownloadStatus.RUNNING &&
                    elapsed != null && elapsed in 1..5_000L &&
                    progressElapsed != null && progressElapsed > 0 &&
                    download.bytesDownloaded >= previous.bytesDownloaded
                ) {
                    // DownloadManager batches byte updates; unchanged polls must keep the baseline.
                    when {
                        download.bytesDownloaded > previous.bytesDownloaded -> {
                            lastProgressTimes[id] = now
                            ((download.bytesDownloaded - previous.bytesDownloaded).toDouble() *
                                1_000 / progressElapsed).toLong()
                        }
                        progressElapsed >= 5_000L -> 0L
                        else -> previous.bytesPerSecond
                    }
                } else {
                    if (download.status == DownloadStatus.RUNNING) {
                        lastProgressTimes[id] = now
                    } else {
                        lastProgressTimes.remove(id)
                    }
                    null
                }
                download.copy(bytesPerSecond = speed)
            }.sortedByDescending { it.lastModified }
            lastRefreshTime = now
        }
    }

    suspend fun cancel(id: Long) {
        remove(id)
    }

    suspend fun remove(id: Long) {
        withContext(ioDispatcher) {
            gateway.remove(id)
            metadata.remove(id)
            lastProgressTimes.remove(id)
            persistMetadata()
            mutableDownloads.value = mutableDownloads.value.filterNot { it.id == id }
        }
    }

    suspend fun retry(id: Long): Long {
        val request = requireNotNull(metadata[id]) { "Download does not exist" }
        remove(id)
        return enqueue(request)
    }

    fun find(id: Long): BrowserDownload? = downloads.value.firstOrNull { it.id == id }

    private fun persistMetadata() {
        metadataStore.writeAll(metadata)
    }
}

private class AndroidDownloadGateway(context: Context) : DownloadGateway {
    private val appContext = context.applicationContext
    private val manager = appContext.getSystemService(DownloadManager::class.java)

    override fun enqueue(request: DownloadRequestData): Long {
        val systemRequest = DownloadManager.Request(Uri.parse(request.url)).apply {
            setTitle(request.fileName)
            setDescription(request.url)
            setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
            request.contentType?.takeIf(String::isNotBlank)?.let(::setMimeType)
            request.cookie?.takeIf(String::isNotBlank)?.let { addRequestHeader("Cookie", it) }
            request.userAgent?.takeIf(String::isNotBlank)?.let { addRequestHeader("User-Agent", it) }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, request.fileName)
            } else {
                setDestinationInExternalFilesDir(appContext, Environment.DIRECTORY_DOWNLOADS, request.fileName)
            }
        }
        return manager.enqueue(systemRequest)
    }

    override fun query(ids: Set<Long>): List<DownloadGatewayRecord> {
        if (ids.isEmpty()) return emptyList()
        return manager.query(DownloadManager.Query().setFilterById(*ids.toLongArray())).use { cursor ->
            buildList {
                while (cursor.moveToNext()) add(cursor.toGatewayRecord())
            }
        }
    }

    override fun remove(id: Long) {
        manager.remove(id)
    }

    private fun Cursor.toGatewayRecord(): DownloadGatewayRecord {
        val id = getLong(getColumnIndexOrThrow(DownloadManager.COLUMN_ID))
        val status = when (getInt(getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS))) {
            DownloadManager.STATUS_PENDING -> DownloadGatewayStatus.PENDING
            DownloadManager.STATUS_RUNNING -> DownloadGatewayStatus.RUNNING
            DownloadManager.STATUS_PAUSED -> DownloadGatewayStatus.PAUSED
            DownloadManager.STATUS_SUCCESSFUL -> DownloadGatewayStatus.SUCCESSFUL
            else -> DownloadGatewayStatus.FAILED
        }
        return DownloadGatewayRecord(
            id = id,
            status = status,
            bytesDownloaded = getLong(getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR)),
            totalBytes = getLong(getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES)),
            localUri = if (status == DownloadGatewayStatus.SUCCESSFUL) {
                manager.getUriForDownloadedFile(id)?.toString()
            } else {
                null
            },
            reason = getInt(getColumnIndexOrThrow(DownloadManager.COLUMN_REASON)),
            lastModified = getLong(getColumnIndexOrThrow(DownloadManager.COLUMN_LAST_MODIFIED_TIMESTAMP)),
        )
    }
}

private class SharedPreferencesDownloadMetadataStore(context: Context) : DownloadMetadataStore {
    private val preferences = context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)

    override fun readAll(): Map<Long, DownloadRequestData> {
        val raw = preferences.getString(KEY_DOWNLOADS, null) ?: return emptyMap()
        return runCatching {
            val array = JSONArray(raw)
            buildMap {
                for (index in 0 until array.length()) {
                    val value = array.getJSONObject(index)
                    put(
                        value.getLong("id"),
                        DownloadRequestData(
                            url = value.getString("url"),
                            fileName = value.getString("fileName"),
                            contentLength = value.optNullableLong("contentLength"),
                            contentType = value.optNullableString("contentType"),
                            cookie = value.optNullableString("cookie"),
                            userAgent = value.optNullableString("userAgent"),
                        ),
                    )
                }
            }
        }.getOrDefault(emptyMap())
    }

    override fun writeAll(values: Map<Long, DownloadRequestData>) {
        val array = JSONArray()
        values.forEach { (id, request) ->
            array.put(
                JSONObject().apply {
                    put("id", id)
                    put("url", request.url)
                    put("fileName", request.fileName)
                    put("contentLength", request.contentLength ?: JSONObject.NULL)
                    put("contentType", request.contentType ?: JSONObject.NULL)
                    put("cookie", request.cookie ?: JSONObject.NULL)
                    put("userAgent", request.userAgent ?: JSONObject.NULL)
                },
            )
        }
        preferences.edit().putString(KEY_DOWNLOADS, array.toString()).apply()
    }

    private fun JSONObject.optNullableString(key: String): String? =
        if (isNull(key)) null else optString(key).takeIf(String::isNotBlank)

    private fun JSONObject.optNullableLong(key: String): Long? =
        if (isNull(key)) null else optLong(key)

    private companion object {
        const val PREFERENCES_NAME = "dao-downloads"
        const val KEY_DOWNLOADS = "owned-downloads"
    }
}

private fun DownloadGatewayRecord.toBrowserDownload(request: DownloadRequestData) = BrowserDownload(
    id = id,
    request = request,
    status = when (status) {
        DownloadGatewayStatus.PENDING -> DownloadStatus.PENDING
        DownloadGatewayStatus.RUNNING -> DownloadStatus.RUNNING
        DownloadGatewayStatus.PAUSED -> DownloadStatus.PAUSED
        DownloadGatewayStatus.SUCCESSFUL -> DownloadStatus.SUCCESSFUL
        DownloadGatewayStatus.FAILED -> DownloadStatus.FAILED
    },
    bytesDownloaded = bytesDownloaded,
    totalBytes = totalBytes.takeIf { it > 0 } ?: request.contentLength,
    localUri = localUri,
    reason = reason,
    lastModified = lastModified,
)

private fun DownloadRequestData.toPendingDownload(id: Long) = BrowserDownload(
    id = id,
    request = this,
    status = DownloadStatus.PENDING,
    bytesDownloaded = 0,
    totalBytes = contentLength,
    localUri = null,
    reason = 0,
    lastModified = 0,
)

private fun isHttpUrl(url: String): Boolean =
    runCatching { URI(url).scheme?.lowercase() in setOf("http", "https") }.getOrDefault(false)

private fun sanitizeFileName(fileName: String, url: String): String {
    val fallback = runCatching { URI(url).path.substringAfterLast('/').takeIf(String::isNotBlank) }.getOrNull()
        ?: "download"
    return fileName.trim().ifEmpty { fallback }
        .replace(Regex("[\\\\/:*?\"<>|\\u0000-\\u001F]"), "_")
        .take(180)
        .ifEmpty { "download" }
}
