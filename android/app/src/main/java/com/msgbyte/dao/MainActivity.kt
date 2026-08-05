package com.msgbyte.dao

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.platform.LocalConfiguration
import androidx.core.view.WindowCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.msgbyte.dao.browser.AmoStoreViewModel
import com.msgbyte.dao.browser.BrowserPreferenceState
import com.msgbyte.dao.browser.BrowserSessionViewModel
import com.msgbyte.dao.browser.NavigationTargetResolver
import com.msgbyte.dao.ui.BrowserScreen
import com.msgbyte.dao.ui.theme.DaoTheme
import kotlinx.coroutines.launch
import mozilla.components.concept.engine.EngineSession

class MainActivity : ComponentActivity() {
    private val browserSessionViewModel by viewModels<BrowserSessionViewModel>()
    private val amoStoreViewModel by viewModels<AmoStoreViewModel> {
        viewModelFactory {
            initializer {
                val daoApplication = application as DaoApplication
                AmoStoreViewModel(
                    source = daoApplication.amoCatalogRepository,
                    locale = daoApplication.resources.configuration.locales[0].toLanguageTag(),
                    geckoMajor = daoApplication.browserRuntime.engine.version.major,
                )
            }
        }
    }
    private var darkThemeEnabled = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val application = application as DaoApplication
        setContent {
            val preferences = application.browserPreferences
            val preferenceState by preferences.state.collectAsStateWithLifecycle(
                initialValue = BrowserPreferenceState(),
            )
            val browserState by browserSessionViewModel.controller.state.collectAsStateWithLifecycle()
            val resolver = remember(preferenceState.searchEngine) {
                NavigationTargetResolver(preferenceState.searchEngine.searchUrl)
            }
            val scope = rememberCoroutineScope()
            val storeLocale = LocalConfiguration.current.locales[0].toLanguageTag()
            LaunchedEffect(storeLocale) {
                amoStoreViewModel.updateLocale(storeLocale)
            }
            LaunchedEffect(
                preferenceState.fontScale,
                preferenceState.trackingProtectionEnabled,
                browserState.tabs.map { it.engineState.engineSession },
            ) {
                val policy = if (preferenceState.trackingProtectionEnabled) {
                    EngineSession.TrackingProtectionPolicy.recommended()
                } else {
                    EngineSession.TrackingProtectionPolicy.none()
                }
                application.browserRuntime.engine.settings.fontSizeFactor =
                    preferenceState.fontScale.factor
                application.browserRuntime.engine.settings.trackingProtectionPolicy = policy
                browserState.tabs.forEach { tab ->
                    tab.engineState.engineSession?.updateTrackingProtection(policy)
                }
            }
            LaunchedEffect(preferenceState.remoteDebuggingEnabled) {
                application.browserRuntime.setRemoteDebuggingEnabled(
                    preferenceState.remoteDebuggingEnabled,
                )
            }
            SideEffect {
                darkThemeEnabled = preferenceState.darkTheme
                window.decorView.post(::updateSystemBarAppearance)
            }
            DaoTheme(darkTheme = preferenceState.darkTheme) {
                BrowserScreen(
                    engine = application.browserRuntime.engine,
                    controller = browserSessionViewModel.controller,
                    thumbnailRepository = browserSessionViewModel.thumbnailRepository,
                    library = application.browserLibrary,
                    downloads = application.downloadRepository,
                    extensions = application.extensionRepository,
                    amoStoreViewModel = amoStoreViewModel,
                    resolver = resolver,
                    preferences = preferenceState,
                    onDarkThemeChange = { enabled ->
                        scope.launch { preferences.setDarkTheme(enabled) }
                    },
                    onFontScaleChange = { value -> scope.launch { preferences.setFontScale(value) } },
                    onSearchEngineChange = { value -> scope.launch { preferences.setSearchEngine(value) } },
                    onTrackingProtectionChange = { enabled ->
                        scope.launch { preferences.setTrackingProtectionEnabled(enabled) }
                    },
                    onDefaultPrivateBrowsingChange = { enabled ->
                        scope.launch { preferences.setDefaultPrivateBrowsing(enabled) }
                    },
                    onRemoteDebuggingChange = { enabled ->
                        scope.launch { preferences.setRemoteDebuggingEnabled(enabled) }
                    },
                    onEnableRemoteDebuggingWithAcknowledgement = {
                        scope.launch { preferences.enableRemoteDebuggingWithAcknowledgement() }
                    },
                )
            }
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) updateSystemBarAppearance()
    }

    override fun onPause() {
        browserSessionViewModel.controller.flushRegularSessionStates()
        super.onPause()
    }

    private fun updateSystemBarAppearance() {
        WindowCompat.getInsetsController(window, window.decorView).apply {
            isAppearanceLightStatusBars = !darkThemeEnabled
            isAppearanceLightNavigationBars = !darkThemeEnabled
        }
    }
}
