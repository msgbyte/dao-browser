package com.msgbyte.dao.browser

import android.content.Context
import android.net.Uri
import java.io.File
import java.io.IOException
import java.util.IdentityHashMap
import java.util.UUID
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.ktx.android.net.getFileName

internal data class StagedUpload(val directory: File, val uris: List<Uri>)

internal class FileUploadStorage(private val context: Context) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val parent = File(context.cacheDir, "web-file-uploads")
    private val root = File(parent, UUID.randomUUID().toString())
    private val sessions = IdentityHashMap<EngineSession, MutableList<StagedUpload>>()

    init {
        // A process restart cannot restore Gecko's live file prompts or their staged documents.
        scope.launch {
            runCatching {
                parent.listFiles()?.filter { it != root }?.forEach { it.deleteRecursively() }
            }
        }
    }

    suspend fun stage(uris: List<Uri>, multiple: Boolean): StagedUpload {
        val directory = File(root, UUID.randomUUID().toString())
        val staged = mutableListOf<Uri>()
        try {
            uris.forEachIndexed { index, uri ->
                currentCoroutineContext().ensureActive()
                try {
                    if (uri.scheme != "content" ||
                        context.packageManager.resolveContentProvider(uri.authority.orEmpty(), 0)?.packageName == context.packageName
                    ) {
                        throw IOException("Not an external document")
                    }
                    val name = uri.getFileName(context.contentResolver)
                    if (name.isBlank() || name == "." || name == ".." || File(name).name != name) {
                        throw IOException("Invalid document name")
                    }
                    val destination = File(directory, "$index/$name")
                    if (!destination.parentFile!!.mkdirs()) throw IOException("Cannot create upload cache")
                    context.contentResolver.openAssetFileDescriptor(uri, "r")?.use { descriptor ->
                        descriptor.createInputStream().use { input ->
                            destination.outputStream().use { output ->
                                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                                var size = 0L
                                while (true) {
                                    currentCoroutineContext().ensureActive()
                                    val read = input.read(buffer)
                                    if (read == -1) break
                                    output.write(buffer, 0, read)
                                    size += read
                                }
                                if (descriptor.declaredLength >= 0 && size != descriptor.declaredLength) {
                                    throw IOException("Incomplete document")
                                }
                            }
                        }
                    } ?: throw IOException("Document is unavailable")
                    staged += Uri.fromFile(destination)
                    if (!multiple) return StagedUpload(directory, staged)
                } catch (error: Exception) {
                    if (error is CancellationException || multiple) throw error
                }
            }
            if (staged.isEmpty()) throw IOException("No readable document")
            return StagedUpload(directory, staged)
        } catch (error: Exception) {
            directory.deleteRecursively()
            throw error
        }
    }

    fun retain(session: EngineSession, upload: StagedUpload) {
        sessions.getOrPut(session) { mutableListOf() }.add(upload)
    }

    fun discard(upload: StagedUpload) {
        scope.launch { runCatching { upload.directory.deleteRecursively() } }
    }

    fun removeClosedSessions(live: List<EngineSession>) {
        val closed = sessions.keys.filter { session -> live.none { it === session } }
        closed.forEach { session ->
            sessions.remove(session)?.forEach(::discard)
        }
    }
}
