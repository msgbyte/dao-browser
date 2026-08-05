package com.msgbyte.dao.browser

import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.verify
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import org.junit.Assert.assertSame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BrowserSessionControllerTest {
    private val engine = mockk<Engine>()
    private val session = mockk<EngineSession>(relaxed = true)

    @Test
    fun createsOneRegularSessionAndLoadsInitialUrl() {
        every { engine.createSession(private = false, contextId = null) } returns session

        val controller = BrowserSessionController(engine, "about:blank")

        assertSame(session, controller.session)
        verify(exactly = 1) { engine.createSession(private = false, contextId = null) }
        verify(exactly = 1) { session.loadUrl("about:blank") }
    }

    @Test
    fun createsAPrivateSessionWhenRequested() {
        every { engine.createSession(private = true, contextId = null) } returns session

        BrowserSessionController(
            engine = engine,
            initialUrl = "about:blank",
            privateBrowsing = true,
        )

        verify(exactly = 1) { engine.createSession(private = true, contextId = null) }
    }

    @Test
    fun navigateForwardsUrlToTheOwnedSession() {
        every { engine.createSession(private = false, contextId = null) } returns session
        val controller = BrowserSessionController(engine, "about:blank")

        controller.navigate("https://example.com")

        verify(exactly = 1) { session.loadUrl("https://example.com") }
    }

    @Test
    fun browserNavigationCommandsAreForwardedToTheOwnedSession() {
        every { engine.createSession(private = false, contextId = null) } returns session
        val controller = BrowserSessionController(engine, "about:blank")

        controller.goBack()
        controller.goForward()
        controller.reload()

        verify(exactly = 1) { session.goBack(userInteraction = true) }
        verify(exactly = 1) { session.goForward(userInteraction = true) }
        verify(exactly = 1) { session.reload() }
    }

    @Test
    fun closeClosesTheOwnedSession() {
        every { engine.createSession(private = false, contextId = null) } returns session
        val controller = BrowserSessionController(engine, "about:blank")

        controller.close()

        verify(exactly = 1) { session.close() }
    }

    @Test
    fun sessionCallbacksPublishRealBrowserState() {
        every { engine.createSession(private = false, contextId = null) } returns session
        val observerSlot = slot<EngineSession.Observer>()
        every { session.register(capture(observerSlot)) } returns Unit
        val controller = BrowserSessionController(engine, "about:blank")

        observerSlot.captured.onLocationChange("https://example.com/final", false)
        observerSlot.captured.onTitleChange("Example")
        observerSlot.captured.onNavigationStateChange(true, false)
        observerSlot.captured.onLoadingStateChange(true)
        observerSlot.captured.onProgress(65)
        observerSlot.captured.onSecurityChange(true)

        assertEquals("https://example.com/final", controller.state.value.url)
        assertEquals("Example", controller.state.value.title)
        assertTrue(controller.state.value.canGoBack)
        assertFalse(controller.state.value.canGoForward)
        assertTrue(controller.state.value.isLoading)
        assertEquals(65, controller.state.value.progress)
        assertTrue(controller.state.value.isSecure)
    }

    @Test
    fun completedRegularLoadRecordsObservedUrlAndTitle() {
        every { engine.createSession(private = false, contextId = null) } returns session
        val observerSlot = slot<EngineSession.Observer>()
        every { session.register(capture(observerSlot)) } returns Unit
        val visits = mutableListOf<Pair<String, String>>()
        BrowserSessionController(
            engine = engine,
            initialUrl = "about:blank",
            onVisitCompleted = { url, title -> visits += url to title },
        )

        observerSlot.captured.onLoadingStateChange(true)
        observerSlot.captured.onLocationChange("https://example.com/final", false)
        observerSlot.captured.onTitleChange("Example")
        observerSlot.captured.onLoadingStateChange(false)

        assertEquals(listOf("https://example.com/final" to "Example"), visits)
    }

    @Test
    fun findCommandsAndResultsUseTheOwnedSession() {
        every { engine.createSession(private = false, contextId = null) } returns session
        val observerSlot = slot<EngineSession.Observer>()
        every { session.register(capture(observerSlot)) } returns Unit
        val controller = BrowserSessionController(engine, "about:blank")

        controller.findAll("dao")
        controller.findNext(forward = false)
        observerSlot.captured.onFindResult(2, 5, true)
        assertEquals(FindResultState(2, 5, true), controller.state.value.findResult)
        controller.clearFindMatches()

        verify(exactly = 1) { session.findAll("dao") }
        verify(exactly = 1) { session.findNext(false) }
        verify(exactly = 1) { session.clearFindMatches() }
        assertEquals(null, controller.state.value.findResult)
    }

    @Test
    fun externalResourceCallbackForwardsCompleteDownloadMetadata() {
        every { engine.createSession(private = false, contextId = null) } returns session
        val observerSlot = slot<EngineSession.Observer>()
        every { session.register(capture(observerSlot)) } returns Unit
        val requests = mutableListOf<DownloadRequestData>()
        BrowserSessionController(
            engine = engine,
            initialUrl = "about:blank",
            onDownloadRequested = requests::add,
        )

        observerSlot.captured.onExternalResource(
            url = "https://example.com/archive.zip",
            fileName = "archive.zip",
            contentLength = 1_000,
            contentType = "application/zip",
            cookie = "session=dao",
            userAgent = "Dao/1",
            isPrivate = false,
            skipConfirmation = false,
            openInApp = false,
            response = null,
        )

        assertEquals(
            DownloadRequestData(
                url = "https://example.com/archive.zip",
                fileName = "archive.zip",
                contentLength = 1_000,
                contentType = "application/zip",
                cookie = "session=dao",
                userAgent = "Dao/1",
            ),
            requests.single(),
        )
    }
}
