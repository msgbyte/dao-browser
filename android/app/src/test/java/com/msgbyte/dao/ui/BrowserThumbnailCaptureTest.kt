package com.msgbyte.dao.ui

import android.graphics.Bitmap
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import java.util.ArrayDeque
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import mozilla.components.concept.engine.EngineView
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Test

class BrowserThumbnailCaptureTest {
    @Test
    fun `capture returns null when no EngineView is bound`() = runBlocking {
        val capture = BrowserThumbnailCapture()

        assertNull(capture.capture())
    }

    @Test
    fun `capture returns the bitmap produced by the bound EngineView`() = runBlocking {
        val expected = mockk<Bitmap>()
        val engineView = mockk<EngineView>()
        every { engineView.captureThumbnail(any()) } answers {
            firstArg<(Bitmap) -> Unit>().invoke(expected)
        }
        val capture = BrowserThumbnailCapture()
        capture.bind(engineView)

        val actual = capture.capture()

        assertSame(expected, actual)
    }

    @Test
    fun `capture returns null when bound EngineView never calls back`() = runBlocking {
        val engineView = mockk<EngineView>(relaxed = true)
        every { engineView.captureThumbnail(any()) } just Runs
        val capture = BrowserThumbnailCapture()
        capture.bind(engineView)

        val actual = withTimeout(1_000) { capture.capture() }

        assertNull(actual)
    }

    @Test
    fun `unbind prevents the disposed EngineView from serving captures`() = runBlocking {
        val engineView = mockk<EngineView>(relaxed = true)
        val capture = BrowserThumbnailCapture()
        capture.bind(engineView)

        capture.unbind(engineView)

        assertNull(capture.capture())
    }

    @Test
    fun `closing a tab waits for cancellation before deleting and ignores a delayed callback`() =
        runBlocking {
            val pendingCapture = CompletableDeferred<Bitmap?>()
            val events = mutableListOf<String>()
            val coordinator = TabThumbnailCaptureCoordinator(
                scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
                captureThumbnail = {
                    try {
                        pendingCapture.await()
                    } finally {
                        events += "capture-cancelled"
                    }
                },
                saveThumbnail = { _, _, _ -> events += "save" },
                deleteThumbnail = { events += "delete" },
            )

            coordinator.capture(tabId = "closed-tab", private = false)
            coordinator.close(tabId = "closed-tab")
            pendingCapture.complete(mockk())
            yield()

            assertEquals(listOf("capture-cancelled", "delete"), events)
        }

    @Test
    fun `a newer capture wins when callbacks complete out of order`() = runBlocking {
        val firstCapture = CompletableDeferred<Bitmap?>()
        val secondCapture = CompletableDeferred<Bitmap?>()
        val pendingCaptures = ArrayDeque(listOf(firstCapture, secondCapture))
        val oldBitmap = mockk<Bitmap>()
        val newBitmap = mockk<Bitmap>()
        val saved = mutableListOf<Bitmap>()
        val coordinator = TabThumbnailCaptureCoordinator(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            captureThumbnail = { pendingCaptures.removeFirst().await() },
            saveThumbnail = { _, _, bitmap -> saved += bitmap },
            deleteThumbnail = {},
        )

        coordinator.capture(tabId = "repeated-tab", private = false)
        coordinator.capture(tabId = "repeated-tab", private = false)
        secondCapture.complete(newBitmap)
        firstCapture.complete(oldBitmap)
        yield()

        assertEquals(listOf(newBitmap), saved)
    }

    @Test
    fun `capture failure is reported and later capture succeeds`() = runBlocking {
        val captureFailure = IllegalStateException("capture failed")
        val expectedBitmap = mockk<Bitmap>()
        val reported = mutableListOf<Throwable>()
        val saved = mutableListOf<Bitmap>()
        var attempts = 0
        val coordinator = TabThumbnailCaptureCoordinator(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            captureThumbnail = {
                if (attempts++ == 0) throw captureFailure
                expectedBitmap
            },
            saveThumbnail = { _, _, bitmap -> saved += bitmap },
            deleteThumbnail = {},
            onError = { reported += it },
        )

        coordinator.capture(tabId = "retry-tab", private = false)
        yield()
        coordinator.capture(tabId = "retry-tab", private = false)
        yield()

        assertEquals(listOf(captureFailure), reported)
        assertEquals(listOf(expectedBitmap), saved)
    }

    @Test
    fun `capture and wait returns only after the thumbnail is saved`() = runBlocking {
        val expectedBitmap = mockk<Bitmap>()
        val allowSave = CompletableDeferred<Unit>()
        val coordinator = TabThumbnailCaptureCoordinator(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            captureThumbnail = { null },
            saveThumbnail = { _, _, _ -> allowSave.await() },
            deleteThumbnail = {},
        )

        val capture = async {
            coordinator.captureAndWait(tabId = "new-tab", private = false) { expectedBitmap }
        }
        yield()

        assertFalse(capture.isCompleted)
        allowSave.complete(Unit)
        capture.await()
    }

    @Test
    fun `save failure is reported and later capture succeeds`() = runBlocking {
        val saveFailure = IllegalStateException("save failed")
        val expectedBitmap = mockk<Bitmap>()
        val reported = mutableListOf<Throwable>()
        val saved = mutableListOf<Bitmap>()
        var attempts = 0
        val coordinator = TabThumbnailCaptureCoordinator(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            captureThumbnail = { expectedBitmap },
            saveThumbnail = { _, _, bitmap ->
                if (attempts++ == 0) throw saveFailure
                saved += bitmap
            },
            deleteThumbnail = {},
            onError = { reported += it },
        )

        coordinator.capture(tabId = "retry-tab", private = false)
        yield()
        coordinator.capture(tabId = "retry-tab", private = false)
        yield()

        assertEquals(listOf(saveFailure), reported)
        assertEquals(listOf(expectedBitmap), saved)
    }

    @Test
    fun `delete failure is reported and later capture succeeds`() = runBlocking {
        val deleteFailure = IllegalStateException("delete failed")
        val expectedBitmap = mockk<Bitmap>()
        val reported = mutableListOf<Throwable>()
        val saved = mutableListOf<Bitmap>()
        val coordinator = TabThumbnailCaptureCoordinator(
            scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            captureThumbnail = { expectedBitmap },
            saveThumbnail = { _, _, bitmap -> saved += bitmap },
            deleteThumbnail = { throw deleteFailure },
            onError = { reported += it },
        )

        coordinator.close(tabId = "closed-tab")
        coordinator.capture(tabId = "retry-tab", private = false)
        yield()

        assertEquals(listOf(deleteFailure), reported)
        assertEquals(listOf(expectedBitmap), saved)
    }
}
