package com.msgbyte.dao.browser

import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Response
import java.io.ByteArrayInputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BrowserDownloadConfirmationTest {
    private val observer = slot<EngineSession.Observer>()
    private val session = mockk<EngineSession>(relaxed = true).also {
        every { it.register(capture(observer)) } answers {}
    }
    private val started = mutableListOf<Pair<DownloadRequestData, Response?>>()
    private val store = BrowserStore()
    private val controller = BrowserTabsController(
        store = store,
        defaultPrivateBrowsing = false,
        scope = CoroutineScope(Dispatchers.Unconfined),
        onDownloadRequested = { request, response -> started += request to response },
    ).also {
        store.dispatch(TabListAction.AddTabAction(TabSessionState(
            content = ContentState(url = "https://example.com", private = true),
            engineState = EngineState(engineSession = session),
        ), select = true))
    }

    @Test
    fun aPageDownloadWaitsForConfirmationEvenWhenGeckoRequestsSkippingIt() {
        val body = TrackingBody()
        requestDownload(body)

        assertTrue(started.isEmpty())
        assertFalse(body.closed)
        assertEquals("report.csv", controller.pendingDownloads.value.single().request.fileName)
        controller.close()
    }

    @Test
    fun confirmationHandsOverTheOriginalPrivateResponseOnlyOnce() {
        val body = TrackingBody()
        val response = requestDownload(body)
        val pending = controller.pendingDownloads.value.single()
        requestDownload(TrackingBody())

        controller.confirmDownload(pending)
        controller.confirmDownload(pending)

        assertEquals(1, started.size)
        assertSame(response, started.single().second)
        assertTrue(started.single().first.isPrivate)
        assertEquals("blob:https://example.com/export", started.single().first.url)
        assertEquals(3L, started.single().first.contentLength)
        assertFalse(body.closed)
        assertEquals(3, body.available())
        assertEquals(1, controller.pendingDownloads.value.size)
        controller.close()
        assertFalse(body.closed)
        response.close()
    }

    @Test
    fun cancellationClosesOnlyTheDismissedResponseAndLeavesTheNextRequestPending() {
        val firstBody = TrackingBody()
        requestDownload(firstBody)
        val first = controller.pendingDownloads.value.single()
        val secondBody = TrackingBody()
        requestDownload(secondBody)

        controller.cancelDownload(first)
        controller.cancelDownload(first)

        assertTrue(started.isEmpty())
        assertTrue(firstBody.closed)
        assertFalse(secondBody.closed)
        assertEquals(1, controller.pendingDownloads.value.size)
        controller.close()
        assertTrue(secondBody.closed)
        assertTrue(controller.pendingDownloads.value.isEmpty())
    }

    @Test
    fun closingTheSourceTabCancelsItsUnconfirmedDownload() {
        val body = TrackingBody()
        requestDownload(body)

        controller.closeTab(controller.selectedTab()!!.id)

        assertTrue(started.isEmpty())
        assertTrue(body.closed)
        assertTrue(controller.pendingDownloads.value.isEmpty())
        controller.close()
    }

    @Test
    fun closingAllPrivateTabsCancelsTheirUnconfirmedDownloads() {
        val body = TrackingBody()
        requestDownload(body)

        controller.closeAllPrivateTabs()

        assertTrue(started.isEmpty())
        assertTrue(body.closed)
        assertTrue(controller.pendingDownloads.value.isEmpty())
        assertFalse(controller.selectedTab()!!.content.private)
        controller.close()
    }

    @Test
    fun endingTheSessionClosesEveryUnconfirmedResponse() {
        val first = TrackingBody()
        val second = TrackingBody()
        requestDownload(first)
        requestDownload(second)

        controller.close()

        assertTrue(started.isEmpty())
        assertTrue(first.closed)
        assertTrue(second.closed)
        assertTrue(controller.pendingDownloads.value.isEmpty())
    }

    private fun requestDownload(body: TrackingBody): Response {
        val response = Response("blob:https://example.com/export", 200, MutableHeaders(), Response.Body(body))
        observer.captured.onExternalResource(
            url = response.url,
            fileName = "report.csv",
            contentLength = 3,
            contentType = "text/csv",
            cookie = null,
            userAgent = null,
            isPrivate = true,
            skipConfirmation = true,
            openInApp = false,
            response = response,
        )
        return response
    }

    private class TrackingBody : ByteArrayInputStream("a,b".toByteArray()) {
        var closed = false
        override fun close() {
            closed = true
            super.close()
        }
    }
}
