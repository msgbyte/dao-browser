package com.msgbyte.dao.browser

import android.webkit.MimeTypeMap
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class DownloadFileNameTest {
    @Before
    fun registerMimeTypes() {
        shadowOf(MimeTypeMap.getSingleton()).addExtensionMimeTypeMapping("pdf", "application/pdf")
        shadowOf(MimeTypeMap.getSingleton()).addExtensionMimeTypeMapping("csv", "text/csv")
    }

    @Test
    fun contentDispositionNamesWin() {
        assertEquals(
            "report.pdf",
            guessDownloadFileName("attachment; filename=\"report.pdf\"", "https://example.com/get?id=5", "application/pdf"),
        )
    }

    @Test
    fun aUrlWithoutAnExtensionGetsOneFromItsMimeType() {
        assertEquals("export.pdf", guessDownloadFileName(null, "https://example.com/export", "application/pdf"))
    }

    @Test
    fun pageGeneratedUrlsWithoutANameFallBackToDownload() {
        assertEquals("download.csv", guessDownloadFileName(null, "data:text/csv,a,b", "text/csv; charset=utf-8"))
        assertEquals("download", guessDownloadFileName(null, "blob:https://example.com/5f0c", null))
    }
}
