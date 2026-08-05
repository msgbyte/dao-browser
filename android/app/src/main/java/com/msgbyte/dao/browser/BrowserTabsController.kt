package com.msgbyte.dao.browser

import android.util.Log
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import mozilla.components.browser.session.storage.RecoverableBrowserState
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.RestoreCompleteAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession

interface BrowserSessionSnapshotStorage {
    suspend fun restore(): RecoverableBrowserState?

    suspend fun clear()

    fun startAutoSave(store: BrowserStore, scope: CoroutineScope) = Unit
}

class BrowserTabsController(
    val store: BrowserStore,
    private val defaultPrivateBrowsing: Boolean,
    private val snapshotStorage: BrowserSessionSnapshotStorage? = null,
    scope: CoroutineScope? = null,
    private val onVisitCompleted: (url: String, title: String) -> Unit = { _, _ -> },
    private val onDownloadRequested: (DownloadRequestData) -> Unit = {},
    private val onSnapshotClearFailure: (Throwable) -> Unit = { error ->
        Log.e(TAG, "Unable to clear the invalid browser session snapshot", error)
    },
) {
    private val sessionObservers = mutableMapOf<EngineSession, EngineSession.Observer>()
    val state: StateFlow<BrowserState> = store.stateFlow

    init {
        scope?.launch {
            val previousLoading = mutableMapOf<String, Boolean>()
            state.collect { browserState ->
                browserState.tabs.forEach { tab ->
                    val wasLoading = previousLoading.put(tab.id, tab.content.loading)
                    if (wasLoading == true && !tab.content.loading && !tab.content.private) {
                        onVisitCompleted(tab.content.url, tab.content.title)
                    }
                }
                previousLoading.keys.retainAll(browserState.tabs.mapTo(mutableSetOf()) { it.id })
                syncSessionObservers(browserState)
            }
        }
    }

    suspend fun initialize() {
        if (state.value.tabs.isNotEmpty()) return

        if (defaultPrivateBrowsing) {
            createTab(private = true)
            clearSnapshotBestEffort()
        } else {
            val snapshot = snapshotStorage?.restore()
            if (snapshot == null || snapshot.tabs.isEmpty()) {
                createTab(private = false)
                if (snapshot != null) clearSnapshotBestEffort()
            } else {
                store.dispatch(
                    TabListAction.RestoreAction(
                        tabs = snapshot.tabs,
                        selectedTabId = snapshot.selectedTabId,
                        restoreLocation = TabListAction.RestoreAction.RestoreLocation.BEGINNING,
                        tabPartitions = snapshot.tabPartitions,
                    ),
                )
                store.dispatch(RestoreCompleteAction)
            }
        }
    }

    fun createTab(
        private: Boolean = selectedTab()?.content?.private ?: defaultPrivateBrowsing,
    ): String {
        val tab = TabSessionState(
            content = ContentState(
                url = AppConfiguration.INITIAL_URL,
                private = private,
            ),
        )
        store.dispatch(TabListAction.AddTabAction(tab, select = true))
        return tab.id
    }

    fun selectTab(tabId: String) {
        if (state.value.tabs.none { it.id == tabId }) return
        store.dispatch(TabListAction.SelectTabAction(tabId))
    }

    fun closeTab(tabId: String) {
        val tab = state.value.tabs.firstOrNull { it.id == tabId } ?: return
        store.dispatch(TabListAction.RemoveTabAction(tabId))
        if (state.value.tabs.isEmpty()) {
            createTab(private = tab.content.private)
        }
    }

    fun navigate(url: String) {
        val tabId = state.value.selectedTabId ?: return
        store.dispatch(ContentAction.UpdateUrlAction(tabId, url))
        store.dispatch(EngineAction.LoadUrlAction(tabId, url))
    }

    fun ensureSession(tabId: String) {
        val tab = state.value.tabs.firstOrNull { it.id == tabId } ?: return
        if (tab.engineState.engineSession != null || tab.engineState.initializing) return
        store.dispatch(EngineAction.CreateEngineSessionAction(tabId))
    }

    fun goHome() {
        navigate(AppConfiguration.INITIAL_URL)
    }

    fun goBack() {
        val tabId = state.value.selectedTabId ?: return
        store.dispatch(EngineAction.GoBackAction(tabId, userInteraction = true))
    }

    fun goForward() {
        val tabId = state.value.selectedTabId ?: return
        store.dispatch(EngineAction.GoForwardAction(tabId, userInteraction = true))
    }

    fun reload() {
        val tabId = state.value.selectedTabId ?: return
        store.dispatch(EngineAction.ReloadAction(tabId))
    }

    fun findAll(query: String) {
        selectedSession()?.findAll(query)
    }

    fun findNext(forward: Boolean) {
        selectedSession()?.findNext(forward)
    }

    fun clearFindMatches() {
        selectedSession()?.clearFindMatches()
    }

    fun flushRegularSessionStates() {
        state.value.tabs
            .asSequence()
            .filterNot { it.content.private }
            .mapNotNull { it.engineState.engineSession }
            .forEach(EngineSession::flushSessionState)
    }

    fun selectedTab(): TabSessionState? {
        val selectedTabId = state.value.selectedTabId ?: return null
        return state.value.tabs.firstOrNull { it.id == selectedTabId }
    }

    fun selectedSession(): EngineSession? = selectedTab()?.engineState?.engineSession

    fun close() {
        sessionObservers.forEach { (session, observer) ->
            session.unregister(observer)
            session.close()
        }
        sessionObservers.clear()
    }

    private fun syncSessionObservers(browserState: BrowserState) {
        val currentSessions = browserState.tabs.mapNotNullTo(mutableSetOf()) {
            it.engineState.engineSession
        }
        sessionObservers.keys.filterNot(currentSessions::contains).forEach { session ->
            sessionObservers.remove(session)?.let(session::unregister)
        }
        browserState.tabs.forEach { tab ->
            val session = tab.engineState.engineSession ?: return@forEach
            if (sessionObservers.containsKey(session)) return@forEach
            val observer = object : EngineSession.Observer {
                override fun onExternalResource(
                    url: String,
                    fileName: String?,
                    contentLength: Long?,
                    contentType: String?,
                    cookie: String?,
                    userAgent: String?,
                    isPrivate: Boolean,
                    skipConfirmation: Boolean,
                    openInApp: Boolean,
                    response: mozilla.components.concept.fetch.Response?,
                ) {
                    onDownloadRequested(
                        DownloadRequestData(
                            url = url,
                            fileName = fileName.orEmpty(),
                            contentLength = contentLength,
                            contentType = contentType,
                            cookie = cookie,
                            userAgent = userAgent,
                        ),
                    )
                }
            }
            session.register(observer)
            sessionObservers[session] = observer
        }
    }

    private suspend fun clearSnapshotBestEffort() {
        try {
            snapshotStorage?.clear()
        } catch (error: Throwable) {
            if (error is CancellationException) throw error
            onSnapshotClearFailure(error)
        }
    }

    private companion object {
        const val TAG = "BrowserTabsController"
    }

}
