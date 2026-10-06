package com.msgbyte.dao.browser

import android.content.ClipboardManager
import android.content.Context
import android.graphics.Bitmap
import androidx.test.core.app.ApplicationProvider
import io.mockk.mockk
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Response
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class PageImageActionsTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    // One test: FileProvider caches its roots statically, but each Robolectric test gets a new cache dir.
    @Test
    fun copyPutsTheImageOnTheClipboardAndAFailedCopyKeepsIt() = runBlocking {
        val bytes = byteArrayOf(1, 2, 3)
        val clipboard = context.getSystemService(ClipboardManager::class.java)

        actions { response(200, bytes, "image/webp") }.copy("https://example.com/cat", private = false)

        val uri = clipboard.primaryClip!!.getItemAt(0).uri
        assertTrue(uri.toString().startsWith("content://${context.packageName}.updates/images/image-"))
        assertTrue(uri.toString().endsWith(".webp"))
        assertArrayEquals(bytes, context.contentResolver.openInputStream(uri)!!.use { it.readBytes() })

        val failing = object : java.io.InputStream() {
            override fun read(): Int = throw IOException("connection dropped")
        }
        runCatching {
            actions { Response(it.url, 200, MutableHeaders("Content-Type" to "image/png"), Response.Body(failing)) }
                .copy("https://example.com/dog", private = false)
        }

        assertEquals(uri, clipboard.primaryClip!!.getItemAt(0).uri)
        assertArrayEquals(bytes, context.contentResolver.openInputStream(uri)!!.use { it.readBytes() })
    }

    @Test
    fun aFailedImageRequestThrowsAndClosesTheResponse() = runBlocking {
        var closed = false
        val body = object : ByteArrayInputStream(ByteArray(0)) {
            override fun close() {
                closed = true
            }
        }
        val actions = actions { Response(it.url, 404, MutableHeaders(), Response.Body(body)) }

        val error = runCatching { actions.loadPreview("https://example.com/missing.png", private = false) }
            .exceptionOrNull()

        assertTrue(error is IOException)
        assertTrue(closed)
    }

    @Test
    fun aWebPageInsteadOfTheImageIsRejected() = runBlocking {
        val actions = actions { response(200, "<html>".toByteArray(), "text/html; charset=utf-8") }

        val error = runCatching { actions.loadPreview("https://example.com/hotlinked.png", false) }.exceptionOrNull()

        assertTrue(error is IOException)
    }

    @Test
    fun previewDownsamplesOversizedImages() = runBlocking {
        val png = ByteArrayOutputStream().also {
            Bitmap.createBitmap(5000, 8, Bitmap.Config.ARGB_8888).compress(Bitmap.CompressFormat.PNG, 100, it)
        }.toByteArray()

        val bitmap = actions { response(200, png, "image/png") }.loadPreview("https://example.com/wide.png", false)

        assertEquals(1250, bitmap.width)
    }

    private fun actions(fetch: (mozilla.components.concept.fetch.Request) -> Response) =
        PageImageActions(context, fetch, mockk(relaxed = true), Dispatchers.Unconfined)

    private fun response(status: Int, bytes: ByteArray, contentType: String) = Response(
        "https://example.com/image",
        status,
        MutableHeaders("Content-Type" to contentType),
        Response.Body(ByteArrayInputStream(bytes)),
    )
}
