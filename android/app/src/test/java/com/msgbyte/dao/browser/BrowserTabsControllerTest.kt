package com.msgbyte.dao.browser

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import mozilla.components.browser.session.storage.RecoverableBrowserState
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.recover.RecoverableTab
import mozilla.components.browser.state.state.recover.TabState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSessionState

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BrowserTabsControllerTest {
    @Test
    fun `createTab adds and selects a BrowserStore tab`() {
        val controller = createController()

        val id = controller.createTab(private = false)

        assertEquals(listOf(id), controller.state.value.tabs.map { it.id })
        assertEquals(id, controller.state.value.selectedTabId)
        assertFalse(controller.state.value.tabs.single().content.private)
    }

    @Test
    fun `createTab inherits privacy from the selected tab`() {
        val controller = createController()
        controller.createTab(private = true)

        controller.createTab()

        assertEquals(2, controller.state.value.tabs.size)
        assertTrue(controller.state.value.tabs.all { it.content.private })
    }

    @Test
    fun `selectTab changes the selected BrowserStore tab`() {
        val controller = createController()
        val firstId = controller.createTab(private = false)
        controller.createTab(private = false)

        controller.selectTab(firstId)

        assertEquals(firstId, controller.state.value.selectedTabId)
    }

    @Test
    fun `closeTab removes only the requested background tab`() {
        val controller = createController()
        val firstId = controller.createTab(private = false)
        val secondId = controller.createTab(private = false)

        controller.closeTab(firstId)

        assertEquals(listOf(secondId), controller.state.value.tabs.map { it.id })
        assertEquals(secondId, controller.state.value.selectedTabId)
    }

    @Test
    fun `closeTab replaces the final tab using its privacy mode`() {
        val controller = createController(defaultPrivateBrowsing = false)
        val originalId = controller.createTab(private = true)

        controller.closeTab(originalId)

        val replacement = controller.state.value.tabs.single()
        assertNotEquals(originalId, replacement.id)
        assertEquals(replacement.id, controller.state.value.selectedTabId)
        assertTrue(replacement.content.private)
    }

    @Test
    fun `initialize restores regular tabs in order and selects the saved tab`() = runBlocking {
        val storage = FakeBrowserSessionSnapshotStorage(
            snapshot = RecoverableBrowserState(
                tabs = listOf(
                    recoverableTab("first", "https://one.example"),
                    recoverableTab("second", "https://two.example"),
                ),
                selectedTabId = "second",
                tabPartitions = emptyMap(),
            ),
        )
        val controller = createController(snapshotStorage = storage)

        controller.initialize()

        assertEquals(listOf("first", "second"), controller.state.value.tabs.map { it.id })
        assertEquals("second", controller.state.value.selectedTabId)
        assertEquals(
            listOf("https://one.example", "https://two.example"),
            controller.state.value.tabs.map { it.content.url },
        )
        assertFalse(storage.wasCleared)
    }

    @Test
    fun `initialize creates a regular tab when no snapshot exists`() = runBlocking {
        val controller = createController(
            snapshotStorage = FakeBrowserSessionSnapshotStorage(snapshot = null),
        )

        controller.initialize()

        assertEquals(1, controller.state.value.tabs.size)
        assertFalse(controller.state.value.tabs.single().content.private)
    }

    @Test
    fun `initialize creates a regular tab when the restored snapshot has no tabs`() = runBlocking {
        val controller = createController(
            snapshotStorage = FakeBrowserSessionSnapshotStorage(
                snapshot = RecoverableBrowserState(
                    tabs = emptyList(),
                    selectedTabId = null,
                    tabPartitions = emptyMap(),
                ),
            ),
        )

        controller.initialize()

        assertEquals(1, controller.state.value.tabs.size)
        assertFalse(controller.state.value.tabs.single().content.private)
    }

    @Test
    fun `initialize preserves one regular tab when empty snapshot cleanup fails`() = runBlocking {
        val cleanupFailure = IllegalStateException("snapshot cleanup failed")
        val reportedFailures = mutableListOf<Throwable>()
        val controller = createController(
            snapshotStorage = FakeBrowserSessionSnapshotStorage(
                snapshot = RecoverableBrowserState(
                    tabs = emptyList(),
                    selectedTabId = null,
                    tabPartitions = emptyMap(),
                ),
                clearFailure = cleanupFailure,
            ),
            onSnapshotClearFailure = { reportedFailures += it },
        )

        controller.initialize()

        assertEquals(1, controller.state.value.tabs.size)
        assertFalse(controller.state.value.tabs.single().content.private)
        assertEquals(listOf(cleanupFailure), reportedFailures)
    }

    @Test
    fun `initialize clears saved state and creates a private tab in default private mode`() =
        runBlocking {
            val storage = FakeBrowserSessionSnapshotStorage(
                snapshot = RecoverableBrowserState(
                    tabs = listOf(recoverableTab("regular", "https://example.com")),
                    selectedTabId = "regular",
                    tabPartitions = emptyMap(),
                ),
            )
            val controller = createController(
                defaultPrivateBrowsing = true,
                snapshotStorage = storage,
            )

            controller.initialize()

            assertTrue(storage.wasCleared)
            assertEquals(1, controller.state.value.tabs.size)
            assertTrue(controller.state.value.tabs.single().content.private)
        }

    @Test
    fun `navigate loads the target in the selected BrowserStore tab session`() {
        val engine = mockk<Engine>(relaxed = true)
        val session = mockk<EngineSession>(relaxed = true)
        every { engine.createSession(any(), any()) } returns session
        val store = BrowserStore(
            middleware = EngineMiddleware.create(
                engine = engine,
                scope = CoroutineScope(Dispatchers.Unconfined),
            ),
        )
        val controller = BrowserTabsController(
            store = store,
            defaultPrivateBrowsing = false,
        )
        controller.createTab(private = false)

        controller.navigate("https://selected.example")

        assertEquals("https://selected.example", controller.selectedTab()?.content?.url)
        verify {
            session.loadUrl(
                url = "https://selected.example",
                parent = any(),
                flags = any(),
                additionalHeaders = any(),
                originalInput = any(),
                textDirectiveUserActivation = any(),
            )
        }
    }

    @Test
    fun `ensureSession creates the selected restored engine session`() {
        val engine = mockk<Engine>(relaxed = true)
        val session = mockk<EngineSession>(relaxed = true)
        val savedState = mockk<EngineSessionState>()
        every { engine.createSession(any(), any()) } returns session
        every { session.restoreState(savedState) } returns true
        val store = BrowserStore(
            middleware = EngineMiddleware.create(
                engine = engine,
                scope = CoroutineScope(Dispatchers.Unconfined),
            ),
        )
        val controller = BrowserTabsController(store, defaultPrivateBrowsing = false)
        val tabId = "restored"
        store.dispatch(
            TabListAction.AddTabAction(
                TabSessionState(
                    id = tabId,
                    content = ContentState(url = "https://restored.example"),
                    engineState = EngineState(engineSessionState = savedState),
                    restored = true,
                ),
                select = true,
            ),
        )

        controller.ensureSession(tabId)

        assertEquals(session, controller.selectedSession())
        verify(exactly = 1) { engine.createSession(false, null) }
        verify(exactly = 1) { session.restoreState(savedState) }
    }

    @Test
    fun `goBack targets only the selected BrowserStore tab session`() {
        val engine = mockk<Engine>(relaxed = true)
        val firstSession = mockk<EngineSession>(relaxed = true)
        val secondSession = mockk<EngineSession>(relaxed = true)
        every { engine.createSession(any(), any()) } returnsMany listOf(firstSession, secondSession)
        val store = BrowserStore(
            middleware = EngineMiddleware.create(
                engine = engine,
                scope = CoroutineScope(Dispatchers.Unconfined),
            ),
        )
        val controller = BrowserTabsController(store, defaultPrivateBrowsing = false)
        val firstId = controller.createTab(private = false)
        controller.navigate("https://one.example")
        controller.createTab(private = false)
        controller.navigate("https://two.example")
        controller.selectTab(firstId)

        controller.goBack()

        verify(exactly = 1) { firstSession.goBack(userInteraction = true) }
        verify(exactly = 0) { secondSession.goBack(any()) }
    }

    @Test
    fun `goHome loads home in the selected tab without creating a tab`() {
        val engine = mockk<Engine>(relaxed = true)
        val firstSession = mockk<EngineSession>(relaxed = true)
        val selectedSession = mockk<EngineSession>(relaxed = true)
        every { engine.createSession(any(), any()) } returnsMany listOf(firstSession, selectedSession)
        val store = BrowserStore(
            middleware = EngineMiddleware.create(
                engine = engine,
                scope = CoroutineScope(Dispatchers.Unconfined),
            ),
        )
        val controller = BrowserTabsController(store, defaultPrivateBrowsing = false)
        controller.createTab(private = false)
        val selectedId = controller.createTab(private = false)
        controller.navigate("https://selected.example")

        controller.goHome()

        assertEquals(2, controller.state.value.tabs.size)
        assertEquals(selectedId, controller.state.value.selectedTabId)
        assertEquals(AppConfiguration.INITIAL_URL, controller.selectedTab()?.content?.url)
    }

    @Test
    fun `flushRegularSessionStates requests fresh Gecko state for regular tabs only`() {
        val regularSession = mockk<EngineSession>(relaxed = true)
        val privateSession = mockk<EngineSession>(relaxed = true)
        val store = BrowserStore()
        val controller = BrowserTabsController(store, defaultPrivateBrowsing = false)
        store.dispatch(
            TabListAction.AddTabAction(
                TabSessionState(
                    content = ContentState(url = "https://regular.example"),
                    engineState = EngineState(engineSession = regularSession),
                ),
                select = true,
            ),
        )
        store.dispatch(
            TabListAction.AddTabAction(
                TabSessionState(
                    content = ContentState(url = "https://private.example", private = true),
                    engineState = EngineState(engineSession = privateSession),
                ),
                select = true,
            ),
        )

        controller.flushRegularSessionStates()

        verify(exactly = 1) { regularSession.flushSessionState() }
        verify(exactly = 0) { privateSession.flushSessionState() }
    }

    @Test
    fun `completed regular loads record history while private loads do not`() {
        val visits = mutableListOf<Pair<String, String>>()
        val store = BrowserStore()
        val controller = BrowserTabsController(
            store = store,
            defaultPrivateBrowsing = false,
            scope = CoroutineScope(Dispatchers.Unconfined),
            onVisitCompleted = { url, title -> visits += url to title },
        )
        val regularId = "regular"
        store.dispatch(
            TabListAction.AddTabAction(
                TabSessionState(
                    id = regularId,
                    content = ContentState(
                        url = "https://regular.example",
                        title = "Regular",
                    ),
                ),
                select = true,
            ),
        )
        store.dispatch(ContentAction.UpdateLoadingStateAction(regularId, true))
        store.dispatch(ContentAction.UpdateLoadingStateAction(regularId, false))
        val privateId = "private"
        store.dispatch(
            TabListAction.AddTabAction(
                TabSessionState(
                    id = privateId,
                    content = ContentState(
                        url = "https://private.example",
                        private = true,
                        title = "Private",
                    ),
                ),
                select = true,
            ),
        )
        store.dispatch(ContentAction.UpdateLoadingStateAction(privateId, true))
        store.dispatch(ContentAction.UpdateLoadingStateAction(privateId, false))

        assertEquals(listOf("https://regular.example" to "Regular"), visits)
    }

    private fun createController(
        defaultPrivateBrowsing: Boolean = false,
        snapshotStorage: BrowserSessionSnapshotStorage? = null,
        onSnapshotClearFailure: (Throwable) -> Unit = {},
    ) = BrowserTabsController(
        store = BrowserStore(),
        defaultPrivateBrowsing = defaultPrivateBrowsing,
        snapshotStorage = snapshotStorage,
        onSnapshotClearFailure = onSnapshotClearFailure,
    )

    private fun recoverableTab(id: String, url: String) = RecoverableTab(
        engineSessionState = null,
        state = TabState(id = id, url = url),
    )

    private class FakeBrowserSessionSnapshotStorage(
        private val snapshot: RecoverableBrowserState?,
        private val clearFailure: Throwable? = null,
    ) : BrowserSessionSnapshotStorage {
        var wasCleared = false

        override suspend fun restore(): RecoverableBrowserState? = snapshot

        override suspend fun clear() {
            wasCleared = true
            clearFailure?.let { throw it }
        }
    }
}
