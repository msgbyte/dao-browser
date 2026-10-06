package com.msgbyte.dao.browser

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.webkit.MimeTypeMap
import androidx.core.content.FileProvider
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.engine.HitResult
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response

/** The image a long press hit, also when the image sits inside a link. */
val HitResult.imageSrc: String?
    get() = when (this) {
        is HitResult.IMAGE, is HitResult.IMAGE_SRC -> src.takeIf(String::isNotBlank)
        else -> null
    }

/**
 * Download, copy and preview for images on a page, fetched through Gecko's networking so the
 * browser's cookies apply.
 *
 * ponytail: no Referer is sent, so hotlink-protected images can fail; send the page URL once the
 * fetch client supports it.
 */
class PageImageActions internal constructor(
    private val context: Context,
    private val fetch: (Request) -> Response,
    private val downloads: SystemDownloadRepository,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    suspend fun download(url: String, private: Boolean) {
        withContext(ioDispatcher) {
            val response = open(url, private)
            try {
                val contentType = response.headers["Content-Type"]
                downloads.enqueue(
                    DownloadRequestData(
                        url = url,
                        fileName = guessDownloadFileName(response.headers["Content-Disposition"], url, contentType),
                        contentLength = response.headers["Content-Length"]?.toLongOrNull(),
                        contentType = contentType,
                        isPrivate = private,
                    ),
                    response,
                )
            } catch (error: Throwable) {
                response.close()
                throw error
            }
        }
    }

    /** Puts the image on the clipboard as a content:// URI that other apps can paste. */
    suspend fun copy(url: String, private: Boolean) {
        val file = withContext(ioDispatcher) {
            open(url, private).use { response ->
                val mimeType = response.headers["Content-Type"]?.substringBefore(';')?.trim()
                val extension = mimeType?.takeIf { it.startsWith("image/", ignoreCase = true) }
                    ?.let(MimeTypeMap.getSingleton()::getExtensionFromMimeType)
                    ?: MimeTypeMap.getFileExtensionFromUrl(url).ifEmpty { "png" }
                val directory = File(context.cacheDir, "images").apply { mkdirs() }
                val file = File.createTempFile("image-", ".$extension", directory)
                try {
                    response.body.useStream { input -> file.outputStream().use(input::copyTo) }
                } catch (error: Throwable) {
                    file.delete()
                    throw error
                }
                // Only the latest copy needs to stay readable; a failed copy keeps the previous one.
                directory.listFiles()?.filter { it != file }?.forEach(File::delete)
                file
            }
        }
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.updates", file)
        context.getSystemService(ClipboardManager::class.java)
            .setPrimaryClip(ClipData.newUri(context.contentResolver, file.name, uri))
    }

    /** Decodes the image for preview, downsampled so it stays drawable. */
    suspend fun loadPreview(url: String, private: Boolean): Bitmap = withContext(ioDispatcher) {
        val bytes = open(url, private).use { it.readBytes(MAX_PREVIEW_BYTES) }
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        var sampleSize = 1
        while (maxOf(bounds.outWidth, bounds.outHeight) / sampleSize > MAX_PREVIEW_DIMENSION) sampleSize *= 2
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, BitmapFactory.Options().apply { inSampleSize = sampleSize })
            ?: throw IOException("Unsupported image format")
    }

    private fun open(url: String, private: Boolean): Response {
        val response = fetch(Request(url = url, cookiePolicy = Request.CookiePolicy.INCLUDE, private = private))
        if (response.status !in 200..299) {
            response.close()
            throw IOException("Image request failed with HTTP ${response.status}")
        }
        // An expired session or a blocked hotlink answers with a web page instead of the image.
        if (response.headers["Content-Type"]?.startsWith("text/html", ignoreCase = true) == true) {
            response.close()
            throw IOException("Image request returned a web page")
        }
        return response
    }

    private fun Response.readBytes(limit: Int): ByteArray = body.useStream { input ->
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(64 * 1024)
        while (true) {
            val read = input.read(buffer)
            if (read < 0) break
            output.write(buffer, 0, read)
            if (output.size() > limit) throw IOException("Image is too large to preview")
        }
        output.toByteArray()
    }

    private companion object {
        const val MAX_PREVIEW_BYTES = 32 * 1024 * 1024
        // 2048 px keeps the decoded bitmap at 16 MB at most.
        const val MAX_PREVIEW_DIMENSION = 2048
    }
}
