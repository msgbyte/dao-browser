package com.msgbyte.dao.browser

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.webkit.MimeTypeMap
import com.msgbyte.dao.R
import java.util.UUID
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

internal class PendingFileUpload(
    val tabId: String,
    val session: EngineSession,
    val url: String,
    val prompt: PromptRequest.File,
) {
    val key = "web-file-${UUID.randomUUID()}"
    var launched = false
    var receiving = false
    var completed = false
    var reading: Job? = null
}

internal class BrowserFileUpload(
    private val context: Context,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val storage: FileUploadStorage = FileUploadStorage(context),
) : Middleware<BrowserState, BrowserAction> {
    private lateinit var store: Store<BrowserState, BrowserAction>
    private val mutablePending = MutableStateFlow<PendingFileUpload?>(null)
    val pending = mutablePending.asStateFlow()
    private val messages = Channel<Int>(Channel.BUFFERED)
    val errors = messages.receiveAsFlow()
    var isBrowsing = false
    var isForeground = false

    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        this.store = store
        val current = mutablePending.value
        if (current != null && action.navigates(current.tabId)) dismiss(current)
        next(action)
        mutablePending.value?.let { if (!isLive(it)) dismiss(it) }
        storage.removeClosedSessions(store.state.tabs.mapNotNull { it.engineState.engineSession })

        if (action is ContentAction.UpdatePromptRequestAction) {
            val prompt = action.promptRequest as? PromptRequest.File ?: return
            if (mutablePending.value?.prompt === prompt) return
            val tab = store.state.tabs.firstOrNull { it.id == action.sessionId }
            val session = tab?.engineState?.engineSession
            if (tab == null || session == null || tab.id != store.state.selectedTabId ||
                !isBrowsing || !isForeground || mutablePending.value != null
            ) {
                prompt.onDismiss()
                store.dispatch(ContentAction.ConsumePromptRequestAction(action.sessionId, prompt))
            } else {
                mutablePending.value = PendingFileUpload(tab.id, session, tab.content.url, prompt)
            }
        }
    }

    fun launchIntent(request: PendingFileUpload): Intent? {
        if (mutablePending.value !== request || request.launched || request.completed) return null
        if (!isLive(request) || !isForeground || !isBrowsing) {
            dismiss(request)
            return null
        }
        request.launched = true
        val types = request.prompt.mimeTypes.map { value ->
            val type = value.trim().lowercase()
            if (type.contains('/')) type else {
                MimeTypeMap.getSingleton().getMimeTypeFromExtension(type.removePrefix(".")) ?: "*/*"
            }
        }.distinct().ifEmpty { listOf("*/*") }.toTypedArray()
        return Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = types.singleOrNull() ?: "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, types)
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, request.prompt.isMultipleFilesSelection)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
    }

    fun onLaunchFailed(key: String) {
        val request = mutablePending.value?.takeIf { it.key == key } ?: return
        if (!request.completed) {
            dismiss(request)
            messages.trySend(R.string.file_picker_unavailable)
        }
        mutablePending.value = null
    }

    fun onResult(key: String, resultCode: Int, data: Intent?) {
        val request = mutablePending.value?.takeIf { it.key == key && !it.receiving } ?: return
        request.receiving = true
        if (request.completed || !isLive(request) || resultCode != Activity.RESULT_OK) {
            dismiss(request)
            mutablePending.value = null
            return
        }
        val uris = buildList {
            data?.data?.let(::add)
            data?.clipData?.let { clip ->
                repeat(clip.itemCount) { clip.getItemAt(it).uri?.let(::add) }
            }
        }.distinct()
        if (uris.isEmpty()) {
            dismiss(request)
            mutablePending.value = null
            return
        }
        request.reading = scope.launch {
            var staged: StagedUpload? = null
            var retained = false
            try {
                val upload = withContext(ioDispatcher) {
                    storage.stage(uris, request.prompt.isMultipleFilesSelection).also { staged = it }
                }
                if (!request.completed && isLive(request)) {
                    // Reserve the terminal outcome before callbacks can synchronously change the store.
                    request.completed = true
                    try {
                        if (request.prompt.isMultipleFilesSelection) {
                            request.prompt.onMultipleFilesSelected(context, upload.uris.toTypedArray())
                        } else {
                            request.prompt.onSingleFileSelected(context, upload.uris.single())
                        }
                        storage.retain(request.session, upload)
                        retained = true
                    } catch (error: Exception) {
                        request.prompt.onDismiss()
                        messages.trySend(R.string.file_picker_read_failed)
                    } finally {
                        consume(request)
                    }
                } else {
                    dismiss(request)
                }
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                if (!request.completed && isLive(request)) {
                    dismiss(request)
                    messages.trySend(R.string.file_picker_read_failed)
                } else {
                    dismiss(request)
                }
            } finally {
                if (!retained) staged?.let(storage::discard)
                if (mutablePending.value === request) mutablePending.value = null
            }
        }
    }

    fun close() {
        mutablePending.value?.let(::dismiss)
        mutablePending.value = null
        storage.removeClosedSessions(emptyList())
    }

    private fun isLive(request: PendingFileUpload): Boolean {
        val tab = store.state.tabs.firstOrNull { it.id == request.tabId } ?: return false
        return store.state.selectedTabId == request.tabId &&
            tab.engineState.engineSession === request.session &&
            tab.content.url == request.url &&
            tab.content.promptRequests.any { it === request.prompt }
    }

    private fun dismiss(request: PendingFileUpload) {
        if (request.completed) return
        request.completed = true
        request.reading?.cancel()
        request.prompt.onDismiss()
        consume(request)
        // Keep a launched picker's registration until its result arrives, even after invalidation.
        if ((!request.launched || request.receiving) && mutablePending.value === request) mutablePending.value = null
    }

    private fun consume(request: PendingFileUpload) {
        store.dispatch(ContentAction.ConsumePromptRequestAction(request.tabId, request.prompt))
    }
}

private fun BrowserAction.navigates(tabId: String): Boolean = when (this) {
    is ContentAction.UpdateUrlAction -> sessionId == tabId
    is ContentAction.UpdateLoadingStateAction -> sessionId == tabId && loading
    is ContentAction.UpdateLoadRequestAction -> sessionId == tabId
    is EngineAction.LoadUrlAction -> this.tabId == tabId
    is EngineAction.LoadDataAction -> this.tabId == tabId
    is EngineAction.ReloadAction -> this.tabId == tabId
    is EngineAction.GoBackAction -> this.tabId == tabId
    is EngineAction.GoForwardAction -> this.tabId == tabId
    is EngineAction.GoToHistoryIndexAction -> this.tabId == tabId
    else -> false
}
