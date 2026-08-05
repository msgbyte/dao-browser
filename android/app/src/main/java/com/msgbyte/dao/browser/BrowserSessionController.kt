package com.msgbyte.dao.browser

import java.io.Closeable
import java.security.cert.X509Certificate
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession

data class FindResultState(
    val activeMatchOrdinal: Int,
    val numberOfMatches: Int,
    val isDoneCounting: Boolean,
)

data class BrowserSessionState(
    val url: String = "",
    val title: String = "",
    val canGoBack: Boolean = false,
    val canGoForward: Boolean = false,
    val isLoading: Boolean = false,
    val progress: Int = 0,
    val isSecure: Boolean = false,
    val findResult: FindResultState? = null,
)

class BrowserSessionController(
    engine: Engine,
    initialUrl: String,
    privateBrowsing: Boolean = false,
    private val onVisitCompleted: (url: String, title: String) -> Unit = { _, _ -> },
    private val onDownloadRequested: (DownloadRequestData) -> Unit = {},
) : Closeable {
    private val mutableState = MutableStateFlow(BrowserSessionState(url = initialUrl))
    private var visitPending = false

    val state: StateFlow<BrowserSessionState> = mutableState.asStateFlow()

    private val observer = object : EngineSession.Observer {
        override fun onLocationChange(url: String, hasUserGesture: Boolean) {
            mutableState.value = mutableState.value.copy(url = url, isSecure = false)
        }

        override fun onTitleChange(title: String) {
            mutableState.value = mutableState.value.copy(title = title)
        }

        override fun onProgress(progress: Int) {
            mutableState.value = mutableState.value.copy(progress = progress.coerceIn(0, 100))
        }

        override fun onLoadingStateChange(loading: Boolean) {
            mutableState.value = mutableState.value.copy(isLoading = loading)
            if (loading) {
                visitPending = true
            } else if (visitPending) {
                visitPending = false
                val snapshot = mutableState.value
                onVisitCompleted(snapshot.url, snapshot.title)
            }
        }

        override fun onNavigationStateChange(canGoBack: Boolean?, canGoForward: Boolean?) {
            mutableState.value = mutableState.value.copy(
                canGoBack = canGoBack ?: mutableState.value.canGoBack,
                canGoForward = canGoForward ?: mutableState.value.canGoForward,
            )
        }

        override fun onSecurityChange(
            secure: Boolean,
            host: String?,
            issuer: String?,
            certificate: X509Certificate?,
        ) {
            mutableState.value = mutableState.value.copy(isSecure = secure)
        }

        override fun onFindResult(
            activeMatchOrdinal: Int,
            numberOfMatches: Int,
            isDoneCounting: Boolean,
        ) {
            mutableState.value = mutableState.value.copy(
                findResult = FindResultState(activeMatchOrdinal, numberOfMatches, isDoneCounting),
            )
        }

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

    val session: EngineSession = engine.createSession(private = privateBrowsing).also {
        it.register(observer)
        it.loadUrl(initialUrl)
    }

    fun navigate(url: String) {
        mutableState.value = mutableState.value.copy(
            url = url,
            title = "",
            isSecure = false,
            progress = 0,
        )
        session.loadUrl(url)
    }

    fun goBack() {
        session.goBack(userInteraction = true)
    }

    fun goForward() {
        session.goForward(userInteraction = true)
    }

    fun reload() {
        session.reload()
    }

    fun findAll(query: String) {
        session.findAll(query)
    }

    fun findNext(forward: Boolean) {
        session.findNext(forward)
    }

    fun clearFindMatches() {
        session.clearFindMatches()
        mutableState.value = mutableState.value.copy(findResult = null)
    }

    override fun close() {
        session.unregister(observer)
        session.close()
    }
}
