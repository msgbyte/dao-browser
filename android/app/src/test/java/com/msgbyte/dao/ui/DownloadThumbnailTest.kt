package com.msgbyte.dao.ui

import android.content.ContentResolver
import android.content.Context
import android.graphics.Bitmap
import android.media.ExifInterface
import android.net.Uri
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.FileNotFoundException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class DownloadThumbnailTest {
    @get:Rule val temporaryFolder = TemporaryFolder()
    private val resolver = mockk<ContentResolver>()
    private val context = mockk<Context> { every { contentResolver } returns resolver }
    private val uri = Uri.parse("content://downloads/42")

    @Test
    fun largeImagesAreDownsampledBeforeDisplaying() = runBlocking {
        val image = Bitmap.createBitmap(1280, 960, Bitmap.Config.ARGB_8888)
        val bytes = ByteArrayOutputStream().also { image.compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()
        image.recycle()
        every { resolver.openInputStream(uri) } answers { ByteArrayInputStream(bytes) }

        val thumbnail = loadDownloadThumbnail(context, uri.toString(), 160)

        assertNotNull(thumbnail)
        assertEquals(160, thumbnail!!.width)
        assertEquals(120, thumbnail.height)
    }

    @Test
    fun cameraPhotosKeepTheirExifOrientation() = runBlocking {
        val file = temporaryFolder.newFile("portrait.jpg")
        val image = Bitmap.createBitmap(320, 160, Bitmap.Config.ARGB_8888)
        file.outputStream().use { image.compress(Bitmap.CompressFormat.JPEG, 100, it) }
        image.recycle()
        ExifInterface(file.path).apply {
            setAttribute(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_ROTATE_90.toString())
            saveAttributes()
        }
        every { resolver.openInputStream(uri) } answers { file.inputStream() }

        val thumbnail = loadDownloadThumbnail(context, uri.toString(), 160)

        assertNotNull(thumbnail)
        assertEquals(80, thumbnail!!.width)
        assertEquals(160, thumbnail.height)
    }

    @Test
    fun missingDeniedAndUnsupportedFilesKeepTheFallbackIcon() = runBlocking {
        every { resolver.openInputStream(uri) } throws FileNotFoundException()
        assertNull(loadDownloadThumbnail(context, uri.toString(), 160))

        every { resolver.openInputStream(uri) } throws SecurityException()
        assertNull(loadDownloadThumbnail(context, uri.toString(), 160))

        every { resolver.openInputStream(uri) } returns null
        assertNull(loadDownloadThumbnail(context, uri.toString(), 160))

        for (bytes in listOf(byteArrayOf(1, 2, 3), "<svg xmlns=\"http://www.w3.org/2000/svg\"/>".toByteArray())) {
            every { resolver.openInputStream(uri) } answers { ByteArrayInputStream(bytes) }
            assertNull(loadDownloadThumbnail(context, uri.toString(), 160))
        }
    }

    @Test
    fun onlyLocalContentUrisAndBoundedSizesAreRead() = runBlocking {
        for (value in listOf("https://example.com/photo.png", "file:///sdcard/photo.png", "", "data:image/png;base64,AAAA")) {
            assertNull(loadDownloadThumbnail(context, value, 160))
        }
        assertNull(loadDownloadThumbnail(context, uri.toString(), 0))
        assertNull(loadDownloadThumbnail(context, uri.toString(), 257))
        verify(exactly = 0) { resolver.openInputStream(any<Uri>()) }
    }
}
