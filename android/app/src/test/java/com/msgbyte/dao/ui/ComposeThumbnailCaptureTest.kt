package com.msgbyte.dao.ui

import android.content.Context
import android.graphics.Color
import android.view.View
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ComposeThumbnailCaptureTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun `laid out Compose host view can provide a thumbnail`() {
        val view = View(context).apply {
            setBackgroundColor(Color.BLUE)
            layout(0, 0, 240, 320)
        }

        val bitmap = captureViewThumbnail(view)

        requireNotNull(bitmap)
        assertEquals(240, bitmap.width)
        assertEquals(320, bitmap.height)
        assertEquals(Color.BLUE, bitmap.getPixel(120, 160))
    }

    @Test
    fun `view without layout does not provide a thumbnail`() {
        assertNull(captureViewThumbnail(View(context)))
    }
}
