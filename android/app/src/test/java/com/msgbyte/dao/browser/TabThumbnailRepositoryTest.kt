package com.msgbyte.dao.browser

import android.graphics.Bitmap
import androidx.test.core.app.ApplicationProvider
import java.io.File
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class TabThumbnailRepositoryTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    private val thumbnailDirectory = File(context.cacheDir, "dao_tab_thumbnails")

    @Before
    fun setUp() {
        thumbnailDirectory.deleteRecursively()
    }

    @After
    fun tearDown() {
        thumbnailDirectory.deleteRecursively()
    }

    @Test
    fun `regular thumbnail is stored on disk and decoded at bounded size`() = runBlocking {
        val repository = TabThumbnailRepository(context, maxWidth = 36, maxHeight = 48)
        val source = Bitmap.createBitmap(72, 96, Bitmap.Config.ARGB_8888)

        repository.save(tabId = "regular", private = false, bitmap = source)

        val restored = repository.load(tabId = "regular", private = false)
        assertNotNull(restored)
        assertEquals(36, restored!!.width)
        assertEquals(48, restored.height)
        assertEquals(1, thumbnailDirectory.listFiles().orEmpty().size)
    }

    @Test
    fun `private thumbnail stays in memory and never creates a disk file`() = runBlocking {
        val repository = TabThumbnailRepository(context)
        val source = Bitmap.createBitmap(32, 48, Bitmap.Config.ARGB_8888)

        repository.save(tabId = "private", private = true, bitmap = source)

        assertSame(source, repository.load(tabId = "private", private = true))
        assertEquals(0, thumbnailDirectory.listFiles().orEmpty().size)
    }

    @Test
    fun `private lookup never reveals an old regular disk thumbnail`() = runBlocking {
        val repository = TabThumbnailRepository(context)
        val source = Bitmap.createBitmap(32, 48, Bitmap.Config.ARGB_8888)
        repository.save(tabId = "same-id", private = false, bitmap = source)

        val privateResult = repository.load(tabId = "same-id", private = true)

        assertNull(privateResult)
    }

    @Test
    fun `delete removes both memory and disk thumbnail state`() = runBlocking {
        val repository = TabThumbnailRepository(context)
        repository.save(
            tabId = "regular",
            private = false,
            bitmap = Bitmap.createBitmap(32, 32, Bitmap.Config.ARGB_8888),
        )
        repository.save(
            tabId = "private",
            private = true,
            bitmap = Bitmap.createBitmap(32, 32, Bitmap.Config.ARGB_8888),
        )

        repository.delete("regular")
        repository.delete("private")

        assertNull(repository.load("private", private = true))
        assertEquals(0, thumbnailDirectory.listFiles().orEmpty().size)
    }
}
