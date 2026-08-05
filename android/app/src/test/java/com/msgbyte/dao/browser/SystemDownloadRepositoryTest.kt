package com.msgbyte.dao.browser

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class SystemDownloadRepositoryTest {
    private val gateway = FakeDownloadGateway()
    private val store = InMemoryDownloadMetadataStore()
    private val repository = SystemDownloadRepository(gateway, store)

    @Test
    fun enqueuePreservesTheGeckoRequestAndPublishesRunningProgress() = runBlocking {
        val request = DownloadRequestData(
            url = "https://example.com/archive.zip",
            fileName = "archive.zip",
            contentLength = 1_000,
            contentType = "application/zip",
            cookie = "session=dao",
            userAgent = "Dao/1",
        )

        val id = repository.enqueue(request)
        gateway.records[id] = DownloadGatewayRecord(
            id = id,
            status = DownloadGatewayStatus.RUNNING,
            bytesDownloaded = 250,
            totalBytes = 1_000,
            localUri = null,
            reason = 0,
            lastModified = 50,
        )
        repository.refresh()

        assertEquals(request, gateway.enqueued.single())
        assertEquals(DownloadStatus.RUNNING, repository.downloads.value.single().status)
        assertEquals(0.25f, repository.downloads.value.single().progress)
    }

    @Test
    fun retryRemovesTheFailedTaskAndEnqueuesItsOriginalRequest() = runBlocking {
        val request = DownloadRequestData("https://example.com/archive.zip", "archive.zip")
        val failedId = repository.enqueue(request)
        gateway.records[failedId] = DownloadGatewayRecord(
            id = failedId,
            status = DownloadGatewayStatus.FAILED,
            bytesDownloaded = 10,
            totalBytes = 100,
            localUri = null,
            reason = 500,
            lastModified = 50,
        )

        val retriedId = repository.retry(failedId)

        assertTrue(failedId in gateway.removed)
        assertEquals(request, gateway.enqueued.last())
        assertTrue(retriedId != failedId)
        assertEquals(setOf(retriedId), store.readAll().keys)
    }

    @Test
    fun cancelRemovesTheSystemTaskAndOwnedMetadata() = runBlocking {
        val id = repository.enqueue(DownloadRequestData("https://example.com/file.pdf", "file.pdf"))

        repository.cancel(id)

        assertEquals(listOf(id), gateway.removed)
        assertTrue(store.readAll().isEmpty())
        assertTrue(repository.downloads.value.isEmpty())
    }

    @Test
    fun nonHttpDownloadsAreRejectedBeforeReachingAndroid() = runBlocking {
        try {
            repository.enqueue(DownloadRequestData("file:///tmp/private.txt", "private.txt"))
            fail("Expected a non-HTTP download to be rejected")
        } catch (_: IllegalArgumentException) {
            // Expected.
        }

        assertTrue(gateway.enqueued.isEmpty())
    }
}

private class FakeDownloadGateway : DownloadGateway {
    val enqueued = mutableListOf<DownloadRequestData>()
    val records = mutableMapOf<Long, DownloadGatewayRecord>()
    val removed = mutableListOf<Long>()
    private var nextId = 1L

    override fun enqueue(request: DownloadRequestData): Long {
        enqueued += request
        return nextId++
    }

    override fun query(ids: Set<Long>): List<DownloadGatewayRecord> = ids.mapNotNull(records::get)

    override fun remove(id: Long) {
        removed += id
        records.remove(id)
    }
}

private class InMemoryDownloadMetadataStore : DownloadMetadataStore {
    private val values = linkedMapOf<Long, DownloadRequestData>()

    override fun readAll(): Map<Long, DownloadRequestData> = values.toMap()

    override fun writeAll(values: Map<Long, DownloadRequestData>) {
        this.values.clear()
        this.values.putAll(values)
    }
}
