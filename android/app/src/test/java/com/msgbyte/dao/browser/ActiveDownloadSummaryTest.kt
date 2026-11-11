package com.msgbyte.dao.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ActiveDownloadSummaryTest {
    @Test
    fun progressIsWeightedByBytesAcrossUnfinishedDownloads() {
        val summary = listOf(
            download(bytes = 50, total = 100),
            download(bytes = 50, total = 900),
            download(status = DownloadStatus.PENDING, bytes = 0, total = 500),
            download(status = DownloadStatus.PAUSED, bytes = 100, total = 500),
            download(status = DownloadStatus.SUCCESSFUL, bytes = 900, total = 900),
            download(status = DownloadStatus.FAILED, bytes = 400, total = 500),
        ).activeDownloadSummary()

        assertEquals(4, summary.count)
        assertEquals(0.1f, summary.progress!!, 0.0001f)
    }

    @Test
    fun anyUnknownOrNonpositiveSizeMakesTheWholeProgressIndeterminate() {
        for (unknownSize in listOf(null, 0L, -1L)) {
            val summary = listOf(download(bytes = 50, total = 100), download(total = unknownSize))
                .activeDownloadSummary()

            assertEquals(2, summary.count)
            assertNull(summary.progress)
        }
    }

    @Test
    fun progressBecomesDeterminateWhenTheSizeArrives() {
        val pending = download(status = DownloadStatus.PENDING, total = null)
        assertEquals(ActiveDownloadSummary(1), listOf(pending).activeDownloadSummary())

        val running = pending.copy(status = DownloadStatus.RUNNING, bytesDownloaded = 25, totalBytes = 100)
        assertEquals(ActiveDownloadSummary(1, 0.25f), listOf(running).activeDownloadSummary())
    }

    @Test
    fun completionFailureAndRemovalClearTheIndicator() {
        val first = download(bytes = 50, total = 100)
        val second = download(bytes = 300, total = 900)
        assertEquals(2, listOf(first, second).activeDownloadSummary().count)

        val completed = first.copy(status = DownloadStatus.SUCCESSFUL, bytesDownloaded = 100)
        assertEquals(ActiveDownloadSummary(1, 1f / 3f), listOf(completed, second).activeDownloadSummary())
        assertEquals(ActiveDownloadSummary(), listOf(completed, second.copy(status = DownloadStatus.FAILED))
            .activeDownloadSummary())
        assertEquals(ActiveDownloadSummary(), emptyList<BrowserDownload>().activeDownloadSummary())
    }

    @Test
    fun progressClampsEachFilesByteCounter() {
        val summary = listOf(download(bytes = -50, total = 100), download(bytes = 400, total = 100))
            .activeDownloadSummary()

        assertEquals(ActiveDownloadSummary(2, 0.5f), summary)
    }

    @Test
    fun largeByteTotalsDoNotOverflow() {
        val summary = listOf(
            download(bytes = Long.MAX_VALUE, total = Long.MAX_VALUE),
            download(bytes = 0, total = Long.MAX_VALUE),
        ).activeDownloadSummary()

        assertEquals(ActiveDownloadSummary(2, 0.5f), summary)
    }

    private fun download(
        status: DownloadStatus = DownloadStatus.RUNNING,
        bytes: Long = 0,
        total: Long? = 100,
    ) = BrowserDownload(
        id = 1,
        request = DownloadRequestData("https://example.com/file", "file"),
        status = status,
        bytesDownloaded = bytes,
        totalBytes = total,
        localUri = null,
        reason = 0,
        lastModified = 0,
    )
}
