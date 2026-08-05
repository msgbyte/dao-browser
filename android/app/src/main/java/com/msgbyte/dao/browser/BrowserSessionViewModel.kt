package com.msgbyte.dao.browser

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.msgbyte.dao.DaoApplication
import kotlinx.coroutines.launch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.store.BrowserStore

class BrowserSessionViewModel(application: Application) : AndroidViewModel(application) {
    private val daoApplication = application as DaoApplication
    private val initialPreferences = runBlocking(Dispatchers.IO) {
        daoApplication.browserPreferences.state.first()
    }
    private val defaultPrivateBrowsing = initialPreferences.defaultPrivateBrowsing

    private val engine = daoApplication.browserRuntime.run {
        setRemoteDebuggingEnabled(initialPreferences.remoteDebuggingEnabled)
        engine
    }
    private val snapshotStorage = MozillaBrowserSessionSnapshotStorage(application, engine)
    val thumbnailRepository = TabThumbnailRepository(application)
    val controller = BrowserTabsController(
        store = BrowserStore(
            middleware = EngineMiddleware.create(engine, viewModelScope),
        ),
        defaultPrivateBrowsing = defaultPrivateBrowsing,
        snapshotStorage = snapshotStorage,
        scope = viewModelScope,
        onVisitCompleted = { url, title ->
            viewModelScope.launch {
                daoApplication.browserLibrary.recordVisit(url, title)
            }
        },
        onDownloadRequested = { request ->
            viewModelScope.launch {
                runCatching { daoApplication.downloadRepository.enqueue(request) }
            }
        },
    )

    init {
        snapshotStorage.startAutoSave(controller.store, viewModelScope)
        viewModelScope.launch { controller.initialize() }
    }

    override fun onCleared() {
        controller.close()
    }
}
