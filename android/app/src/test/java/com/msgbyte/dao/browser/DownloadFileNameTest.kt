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
    fun aFileNameAfterOtherDispositionParametersDoesNotBecomeDown() {
        assertEquals("report.pdf", guessDownloadFileName(
            "attachment; size=123; filename=\"report.pdf\"", "https://example.com/down", null,
        ))
    }

    @Test
    fun anEncodedFileNameAfterOtherParametersKeepsItsDecodedName() {
        assertEquals("report final.pdf", guessDownloadFileName(
            "attachment; size=123; filename*=UTF-8''report%20final.pdf", "https://example.com/down", null,
        ))
    }

    @Test
    fun anEncodedFileNameWinsRegardlessOfParameterOrder() {
        assertEquals("report final.pdf", guessDownloadFileName(
            "attachment; filename*=UTF-8''report%20final.pdf; size=123; filename=\"fallback.pdf\"",
            "https://example.com/down", null,
        ))
    }

    @Test
    fun quotedParametersCannotInjectAFileNameAndFileNamesCanContainSemicolons() {
        assertEquals("report; final.pdf", guessDownloadFileName(
            "attachment; note=\"; filename=wrong.pdf\"; filename=\"report; final.pdf\"",
            "https://example.com/down", null,
        ))
    }

    @Test
    fun mimeTypesAreCaseInsensitiveWhenAddingAnExtension() {
        assertEquals("down.pdf", guessDownloadFileName(null, "https://example.com/down", "APPLICATION/PDF; charset=utf-8"))
    }

    @Test
    fun missingFileMetadataDoesNotInventAFileType() {
        assertEquals("down", guessDownloadFileName(null, "https://example.com/down", null))
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
