package com.msgbyte.dao.browser

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.fragment.app.FragmentActivity
import com.msgbyte.dao.R
import com.msgbyte.dao.httpNavigationUrl
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.request.RequestInterceptor
import mozilla.components.concept.engine.request.RequestInterceptor.InterceptionResponse
import mozilla.components.feature.app.links.AppLinksFeature
import mozilla.components.feature.app.links.AppLinksInterceptor
import mozilla.components.feature.app.links.SimpleRedirectDialogFragment
import mozilla.components.feature.session.SessionUseCases

class BrowserAppLinksInterceptor(context: Context, private val store: BrowserStore) : RequestInterceptor {
    private val delegate = AppLinksInterceptor(context, launchInApp = { true }, store = store)

    override fun onLoadRequest(
        engineSession: EngineSession,
        uri: String,
        lastUri: String?,
        hasUserGesture: Boolean,
        isSameDomain: Boolean,
        isRedirect: Boolean,
        isDirectNavigation: Boolean,
        isSubframeRequest: Boolean,
    ): InterceptionResponse? {
        val scheme = Uri.parse(uri).scheme
        if (scheme.equals("http", true) || scheme.equals("https", true)) return null

        return when (val response = delegate.onLoadRequest(
            engineSession, uri, lastUri, hasUserGesture, isSameDomain,
            isRedirect, isDirectNavigation, isSubframeRequest,
        )) {
            is InterceptionResponse.Url -> {
                if (response.url.webFallbackUrl() != null) response else InterceptionResponse.Deny
            }
            is InterceptionResponse.AppIntent -> {
                val fallback = response.fallbackUrl.webFallbackUrl()
                // Mozilla also reads this extra when launching fails.
                response.appIntent.removeExtra("browser_fallback_url")
                fallback?.let { response.appIntent.putExtra("browser_fallback_url", it) }
                if (response.appIntent.data?.scheme == "market" && fallback != null) {
                    InterceptionResponse.Url(fallback)
                } else {
                    response.copy(fallbackUrl = fallback)
                }
            }
            else -> response
        }
    }

    fun createFeature(activity: FragmentActivity, darkTheme: () -> Boolean): AppLinksFeature {
        val loadUrl = SessionUseCases(store).loadUrl
        return AppLinksFeature(
            context = activity,
            store = store,
            fragmentManager = activity.supportFragmentManager,
            launchInApp = { true },
            loadUrlUseCase = loadUrl,
            dialog = { data ->
                SimpleRedirectDialogFragment.newInstance(
                    dialogTitleString = data.title,
                    dialogMessageString = data.message,
                    themeResId = if (darkTheme()) {
                        R.style.Theme_Dao_AppLinkDialog_Dark
                    } else {
                        R.style.Theme_Dao_AppLinkDialog
                    },
                )
            },
            failedToLaunchAction = { fallback ->
                val url = fallback.webFallbackUrl()
                if (url != null) {
                    loadUrl(url)
                } else {
                    Toast.makeText(activity, R.string.external_app_unavailable, Toast.LENGTH_SHORT).show()
                }
            },
        )
    }
}

private fun String?.webFallbackUrl(): String? =
    this?.let { Intent(Intent.ACTION_VIEW, Uri.parse(it)).httpNavigationUrl() }
