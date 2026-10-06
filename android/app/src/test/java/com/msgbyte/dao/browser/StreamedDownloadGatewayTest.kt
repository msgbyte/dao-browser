package com.msgbyte.dao.browser

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Response
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class StreamedDownloadGatewayTest {
    private val sink = FakeSink()
    private val store = InMemoryRecordStore()
    private val fetched = mutableListOf<DownloadRequestData>()
    private var fetchResponse: () -> Response = { throw IOException("Offline") }
    private val finished = mutableListOf<DownloadGatewayRecord>()

    @Test
    fun savesTheResponseGeckoAlreadyOpenedWithoutRequestingItAgain() {
        val gateway = gateway()
        val bytes = "a,b\n1,2\n".toByteArray()

        val id = gateway.enqueue(DownloadRequestData("blob:https://example.com/1", "report.csv"), response(bytes))

        val record = gateway.query(setOf(id)).single()
        assertTrue(id < 0)
        assertEquals(DownloadGatewayStatus.SUCCESSFUL, record.status)
        assertEquals(bytes.size.toLong(), record.bytesDownloaded)
        assertEquals("content://test/report.csv", record.localUri)
        assertArrayEquals(bytes, sink.files.getValue("report.csv").toByteArray())
        assertTrue(sink.published.contains("report.csv"))
        assertTrue(fetched.isEmpty())
        assertTrue(gateway.active.value.isEmpty())
        assertEquals(listOf(record), finished)
    }

    @Test
    fun withoutAResponseItFetchesAgainAndAnHttpErrorFailsWithoutLeavingAFile() {
        fetchResponse = { response(ByteArray(0), status = 403) }
        val gateway = gateway()
        val request = DownloadRequestData("https://example.com/private.zip", "private.zip")

        val id = gateway.enqueue(request, response = null)

        assertEquals(listOf(request), fetched)
        assertEquals(DownloadGatewayStatus.FAILED, gateway.query(setOf(id)).single().status)
        assertTrue(sink.files.isEmpty())
    }

    @Test
    fun idsAreNeverHandedOutAgainEvenWithinTheSameMillisecond() {
        val gateway = gateway()
        val first = gateway.enqueue(DownloadRequestData("https://example.com/a", "a"), response("a".toByteArray()))
        gateway.remove(first)

        val second = gateway.enqueue(DownloadRequestData("https://example.com/a", "a"), response("a".toByteArray()))

        assertEquals(FIRST_ID, first)
        assertEquals(FIRST_ID - 1, second)
        assertEquals(FIRST_ID - 2, gateway().enqueue(DownloadRequestData("https://example.com/b", "b"), response("b".toByteArray())))
    }

    @Test
    fun anEncodedBodyIsNotMeasuredAgainstItsEncodedLength() {
        val gateway = gateway()
        val response = Response(
            "https://example.com/a.png",
            200,
            MutableHeaders("Content-Encoding" to "gzip", "Content-Length" to "120"),
            Response.Body(ByteArrayInputStream(ByteArray(100))),
        )

        val id = gateway.enqueue(DownloadRequestData("https://example.com/a.png", "a.png", 120), response)

        assertEquals(DownloadGatewayStatus.SUCCESSFUL, gateway.query(setOf(id)).single().status)
    }

    @Test
    fun aRefetchThatReturnsAWebPageInsteadOfTheFileFails() {
        fetchResponse = {
            Response(
                "https://example.com/login",
                200,
                MutableHeaders("Content-Type" to "text/html; charset=utf-8"),
                Response.Body(ByteArrayInputStream("<form>".toByteArray())),
            )
        }
        val gateway = gateway()

        val id = gateway.enqueue(DownloadRequestData("https://example.com/export", "export.csv", contentType = "text/csv"), null)

        assertEquals(DownloadGatewayStatus.FAILED, gateway.query(setOf(id)).single().status)
        assertTrue(sink.files.isEmpty())
    }

    @Test
    fun aBodyThatEndsBeforeItsContentLengthFails() {
        val gateway = gateway()

        val id = gateway.enqueue(
            DownloadRequestData("https://example.com/cut.bin", "cut.bin", contentLength = 1_000),
            response(ByteArray(10)),
        )

        assertEquals(DownloadGatewayStatus.FAILED, gateway.query(setOf(id)).single().status)
        assertTrue(sink.files.isEmpty())
    }

    @Test
    fun aRefetchIsMeasuredAgainstItsOwnContentLength() {
        fetchResponse = {
            Response(
                "https://example.com/report.pdf",
                200,
                MutableHeaders("Content-Length" to "4"),
                Response.Body(ByteArrayInputStream("%PDF".toByteArray())),
            )
        }
        val gateway = gateway()

        val id = gateway.enqueue(DownloadRequestData("https://example.com/report.pdf", "report.pdf", 1_000), null)

        val record = gateway.query(setOf(id)).single()
        assertEquals(DownloadGatewayStatus.SUCCESSFUL, record.status)
        assertEquals(4L, record.totalBytes)
    }

    @Test
    fun privateDownloadsNeverReachDiskAndUnfinishedOnesFailAfterRestart() {
        val gateway = gateway()
        val public = gateway.enqueue(DownloadRequestData("https://example.com/a", "a"), response("a".toByteArray()))
        gateway.enqueue(DownloadRequestData("https://example.com/b", "b", isPrivate = true), response("b".toByteArray()))
        assertEquals(setOf(public), store.records.keys)

        store.records[-9] = DownloadGatewayRecord(-9, DownloadGatewayStatus.RUNNING, 1, 2, null, 0, 0)
        val restarted = gateway()

        assertEquals(DownloadGatewayStatus.FAILED, restarted.query(setOf(-9L)).single().status)
        assertEquals(DownloadGatewayStatus.SUCCESSFUL, restarted.query(setOf(public)).single().status)
    }

    @Test
    fun removingADownloadMidTransferStopsItAndDeletesThePartialFile() {
        val gateway = gateway()
        val removed = mutableListOf<Boolean>()
        val body = object : InputStream() {
            private var reads = 0

            override fun read() = error("Unused")

            override fun read(b: ByteArray, off: Int, len: Int): Int {
                if (reads++ == 0) return 1.also { b[off] = 7 }
                // The user deletes the row while the next chunk is still arriving.
                removed += gateway.remove(FIRST_ID)
                throw IOException("Closed")
            }
        }

        gateway.enqueue(
            DownloadRequestData("https://example.com/big.iso", "big.iso"),
            Response("https://example.com/big.iso", 200, MutableHeaders(), Response.Body(body)),
        )

        assertEquals(listOf(false), removed)
        assertTrue(gateway.query(setOf(FIRST_ID)).isEmpty())
        assertTrue(sink.files.isEmpty())
        assertTrue(finished.isEmpty())
        assertTrue(gateway.active.value.isEmpty())
    }

    @Test
    fun removingAFinishedDownloadDeletesItsPublishedFile() {
        val gateway = gateway()
        val id = gateway.enqueue(DownloadRequestData("https://example.com/a.pdf", "a.pdf"), response("pdf".toByteArray()))

        assertTrue(gateway.remove(id))

        assertTrue(sink.files.isEmpty())
        assertTrue(store.records.isEmpty())
    }

    private fun gateway() = StreamedDownloadGateway(
        sink = sink,
        recordStore = store,
        fetch = { request ->
            fetched += request
            fetchResponse()
        },
        scope = CoroutineScope(Dispatchers.Unconfined),
        onFinished = { _, record -> finished += record },
        clock = { 1_000L },
    )

    private fun response(bytes: ByteArray, status: Int = 200) =
        Response("https://example.com", status, MutableHeaders(), Response.Body(ByteArrayInputStream(bytes)))

    private companion object {
        // The fake clock reads 1_000 ms, and ids start at its negation.
        const val FIRST_ID = -1_000L
    }
}

private class FakeSink : DownloadSink {
    val files = mutableMapOf<String, ByteArrayOutputStream>()
    val published = mutableSetOf<String>()

    override fun create(fileName: String): DownloadTarget {
        val output = ByteArrayOutputStream().also { files[fileName] = it }
        return object : DownloadTarget {
            override val uri = "content://test/$fileName"

            override fun open(): OutputStream = output

            override fun publish() {
                published += fileName
            }

            override fun delete() = files.remove(fileName) != null
        }
    }

    override fun delete(uri: String) = files.remove(uri.substringAfterLast('/')) != null
}

private class InMemoryRecordStore : StreamedDownloadRecordStore {
    val records = linkedMapOf<Long, DownloadGatewayRecord>()

    override fun readAll(): Map<Long, DownloadGatewayRecord> = records.toMap()

    override fun writeAll(records: Map<Long, DownloadGatewayRecord>) {
        this.records.clear()
        this.records.putAll(records)
    }
}
