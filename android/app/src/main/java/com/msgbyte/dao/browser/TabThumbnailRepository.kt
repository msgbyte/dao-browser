package com.msgbyte.dao.browser

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.AtomicFile
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class TabThumbnailRepository(
    context: Context,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val maxWidth: Int = MAX_WIDTH,
    private val maxHeight: Int = MAX_HEIGHT,
) {
    private val directory = File(context.cacheDir, DIRECTORY_NAME)
    private val privateThumbnails = ConcurrentHashMap<String, Bitmap>()

    suspend fun save(tabId: String, private: Boolean, bitmap: Bitmap) {
        if (private) {
            privateThumbnails[tabId] = bitmap
            withContext(ioDispatcher) { AtomicFile(fileFor(tabId)).delete() }
            return
        }

        privateThumbnails.remove(tabId)
        withContext(ioDispatcher) {
            directory.mkdirs()
            val scaled = bitmap.scaleWithin(maxWidth, maxHeight)
            val file = AtomicFile(fileFor(tabId))
            val output = file.startWrite()
            try {
                check(scaled.compress(Bitmap.CompressFormat.PNG, 100, output))
                file.finishWrite(output)
            } catch (error: Throwable) {
                file.failWrite(output)
                throw error
            } finally {
                if (scaled !== bitmap) scaled.recycle()
            }
        }
    }

    suspend fun load(tabId: String, private: Boolean): Bitmap? {
        if (private) return privateThumbnails[tabId]
        return withContext(ioDispatcher) {
            BitmapFactory.decodeFile(fileFor(tabId).path)
        }
    }

    suspend fun delete(tabId: String) {
        privateThumbnails.remove(tabId)
        withContext(ioDispatcher) { AtomicFile(fileFor(tabId)).delete() }
    }

    private fun fileFor(tabId: String): File {
        val digest = MessageDigest.getInstance("SHA-256")
            .digest(tabId.toByteArray(Charsets.UTF_8))
            .joinToString("") { byte -> "%02x".format(byte) }
        return File(directory, "$digest.webp")
    }

    private fun Bitmap.scaleWithin(maxWidth: Int, maxHeight: Int): Bitmap {
        val scale = minOf(
            1f,
            maxWidth.toFloat() / width,
            maxHeight.toFloat() / height,
        )
        if (scale >= 1f) return this
        return Bitmap.createScaledBitmap(
            this,
            (width * scale).toInt().coerceAtLeast(1),
            (height * scale).toInt().coerceAtLeast(1),
            true,
        )
    }

    private companion object {
        const val DIRECTORY_NAME = "dao_tab_thumbnails"
        const val MAX_WIDTH = 360
        const val MAX_HEIGHT = 480
    }
}
