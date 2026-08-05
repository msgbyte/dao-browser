package com.msgbyte.dao.browser

import android.content.Context
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.browser.session.storage.RecoverableBrowserState
import mozilla.components.browser.session.storage.SessionStorage
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine

class MozillaBrowserSessionSnapshotStorage(
    context: Context,
    engine: Engine,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : BrowserSessionSnapshotStorage {
    private val storage = SessionStorage(context.applicationContext, engine)

    override suspend fun restore(): RecoverableBrowserState? = withContext(ioDispatcher) {
        storage.restore()
    }

    override suspend fun clear() {
        withContext(ioDispatcher) { storage.clear() }
    }

    override fun startAutoSave(store: BrowserStore, scope: CoroutineScope) {
        storage.autoSave(store)
            .whenSessionsChange(scope)
            .whenGoingToBackground()
    }
}
