package com.msgbyte.dao.browser

import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

class SystemDownloadRepositoryTest {
    private val gateway = FakeDownloadGateway()
    private val store = InMemoryDownloadMetadataStore()
    private var now = 1_000L
    private val repository = SystemDownloadRepository(gateway, store, elapsedRealtime = { now })

    @Test
    fun speedUsesEachDownloadsByteDeltaAndActualElapsedTime() = runBlocking {
        val first = repository.enqueue(DownloadRequestData("https://example.com/a", "a"))
        val second = repository.enqueue(DownloadRequestData("https://example.com/b", "b"))
        gateway.records[first] = runningRecord(first, 10_000)
        gateway.records[second] = runningRecord(second, 20_000)
        repository.refresh()
        assertNull(repository.find(first)?.bytesPerSecond)

        now += 2_000
        gateway.records[first] = runningRecord(first, 14_000)
        gateway.records[second] = runningRecord(second, 21_000)
        repository.refresh()
        assertEquals(2_000L, repository.find(first)?.bytesPerSecond)
        assertEquals(500L, repository.find(second)?.bytesPerSecond)

        now += 1_000
        repository.refresh()
        assertEquals(2_000L, repository.find(first)?.bytesPerSecond)

        repeat(4) {
            now += 1_000
            repository.refresh()
        }
        assertEquals(0L, repository.find(first)?.bytesPerSecond)

        now += 1_000
        gateway.records[first] = runningRecord(first, 20_000)
        repository.refresh()
        assertEquals(1_000L, repository.find(first)?.bytesPerSecond)
    }

    @Test
    fun speedIgnoresUnchangedProviderSnapshots() = runBlocking {
        val id = repository.enqueue(DownloadRequestData("https://example.com/a", "a"))
        val bytesPerSecond = 1_048_576L
        gateway.records[id] = runningRecord(id, 0)
        repository.refresh()

        for (second in 1..9) {
            now += 1_000
            if (second % 3 == 0) {
                gateway.records[id] = runningRecord(id, second * bytesPerSecond)
            }
            repository.refresh()
            assertEquals(
                "Speed at second $second",
                if (second < 3) null else bytesPerSecond,
                repository.find(id)?.bytesPerSecond,
            )
        }
    }

    @Test
    fun queryFailureClearsSpeedAndRestartsSampling() = runBlocking {
        val id = repository.enqueue(DownloadRequestData("https://example.com/a", "a"))
        gateway.records[id] = runningRecord(id, 1_000)
        repository.refresh()
        now += 1_000
        gateway.records[id] = runningRecord(id, 3_000)
        repository.refresh()
        assertEquals(2_000L, repository.find(id)?.bytesPerSecond)

        val failure = IllegalStateException("Download provider unavailable")
        gateway.queryFailure = failure
        repeat(2) {
            now += 1_000
            val error = runCatching { repository.refresh() }.exceptionOrNull()
            assertTrue(error is IllegalStateException)
            assertEquals(failure.message, error?.message)
            assertNull(repository.find(id)?.bytesPerSecond)
        }

        gateway.queryFailure = null
        now += 1_000
        gateway.records[id] = runningRecord(id, 50_000)
        repository.refresh()
        assertNull(repository.find(id)?.bytesPerSecond)
        now += 1_000
        gateway.records[id] = runningRecord(id, 51_000)
        repository.refresh()
        assertEquals(1_000L, repository.find(id)?.bytesPerSecond)
    }

    @Test
    fun speedResetsAcrossInactiveStatesAndResumesFromANewSample() = runBlocking {
        val id = repository.enqueue(DownloadRequestData("https://example.com/a", "a"))
        for (status in DownloadGatewayStatus.entries.filter { it != DownloadGatewayStatus.RUNNING }) {
            gateway.records[id] = runningRecord(id, 1_000)
            repository.refresh()
            now += 1_000
            gateway.records[id] = runningRecord(id, 2_000)
            repository.refresh()
            assertEquals(1_000L, repository.find(id)?.bytesPerSecond)

            now += 1_000
            gateway.records[id] = runningRecord(id, 2_000).copy(status = status)
            repository.refresh()
            assertNull(repository.find(id)?.bytesPerSecond)

            now += 1_000
            gateway.records[id] = runningRecord(id, 3_000)
            repository.refresh()
            assertNull(repository.find(id)?.bytesPerSecond)
            now += 1_000
        }
    }

    @Test
    fun speedDiscardsStaleMissingAndResetSamples() = runBlocking {
        val id = repository.enqueue(DownloadRequestData("https://example.com/a", "a"))
        gateway.records[id] = runningRecord(id, 1_000)
        repository.refresh()

        now += 60_000
        gateway.records[id] = runningRecord(id, 50_000)
        repository.refresh()
        assertNull(repository.find(id)?.bytesPerSecond)

        now += 1_000
        gateway.records[id] = runningRecord(id, 51_000)
        repository.refresh()
        assertEquals(1_000L, repository.find(id)?.bytesPerSecond)

        repository.refresh()
        assertNull(repository.find(id)?.bytesPerSecond)
        now += 1_000
        gateway.records[id] = runningRecord(id, 0)
        repository.refresh()
        assertNull(repository.find(id)?.bytesPerSecond)

        now += 1_000
        gateway.records.clear()
        repository.refresh()
        assertNull(repository.find(id)?.bytesPerSecond)
        now += 1_000
        gateway.records[id] = runningRecord(id, 2_000)
        repository.refresh()
        assertNull(repository.find(id)?.bytesPerSecond)
    }

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

private fun runningRecord(id: Long, bytes: Long) = DownloadGatewayRecord(
    id = id,
    status = DownloadGatewayStatus.RUNNING,
    bytesDownloaded = bytes,
    totalBytes = -1,
    localUri = null,
    reason = 0,
    lastModified = 50,
)

private class FakeDownloadGateway : DownloadGateway {
    val enqueued = mutableListOf<DownloadRequestData>()
    val records = mutableMapOf<Long, DownloadGatewayRecord>()
    val removed = mutableListOf<Long>()
    var queryFailure: Exception? = null
    private var nextId = 1L

    override fun enqueue(request: DownloadRequestData): Long {
        enqueued += request
        return nextId++
    }

    override fun query(ids: Set<Long>): List<DownloadGatewayRecord> {
        queryFailure?.let { throw it }
        return ids.mapNotNull(records::get)
    }

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
