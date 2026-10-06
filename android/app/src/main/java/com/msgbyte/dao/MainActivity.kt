package com.msgbyte.dao

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.platform.LocalConfiguration
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import com.msgbyte.dao.browser.AmoStoreViewModel
import com.msgbyte.dao.browser.BrowserPreferenceState
import com.msgbyte.dao.browser.BrowserSessionViewModel
import com.msgbyte.dao.browser.NavigationTargetResolver
import com.msgbyte.dao.ui.BrowserScreen
import com.msgbyte.dao.ui.theme.DaoTheme
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.mediaquery.PreferredColorScheme

class MainActivity : FragmentActivity() {
    private val browserSessionViewModel by viewModels<BrowserSessionViewModel>()
    private val appLinksFeature by lazy {
        browserSessionViewModel.appLinks.createFeature(this) { darkThemeEnabled }
    }
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
    private val notificationPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) {}
    private var externalNavigationUrl by mutableStateOf<String?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState == null) {
            externalNavigationUrl = intent.httpNavigationUrl()
        }
        enableEdgeToEdge()
        val application = application as DaoApplication
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            lifecycleScope.launch {
                repeatOnLifecycle(Lifecycle.State.STARTED) {
                    application.streamedDownloads.active
                        .map { it.isNotEmpty() }
                        .distinctUntilChanged()
                        .collect { downloading ->
                            // Download progress and completion only reach the shade with this permission.
                            // Ask once per process so rotation or returning to Dao does not prompt again.
                            if (downloading && !notificationPermissionRequested && ContextCompat.checkSelfPermission(
                                    this@MainActivity,
                                    Manifest.permission.POST_NOTIFICATIONS,
                                ) != PackageManager.PERMISSION_GRANTED
                            ) {
                                notificationPermissionRequested = true
                                notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
                            }
                        }
                }
            }
        }
        setContent {
            val preferences = application.browserPreferences
            val preferenceState by preferences.state.collectAsStateWithLifecycle(
                initialValue = BrowserPreferenceState(),
            )
            val darkTheme = preferenceState.darkTheme ?: isSystemInDarkTheme()
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
            LaunchedEffect(darkTheme) {
                application.browserRuntime.engine.settings.preferredColorScheme =
                    if (darkTheme) PreferredColorScheme.Dark else PreferredColorScheme.Light
            }
            SideEffect {
                darkThemeEnabled = darkTheme
                window.decorView.post(::updateSystemBarAppearance)
            }
            DaoTheme(darkTheme = darkTheme) {
                BrowserScreen(
                    engine = application.browserRuntime.engine,
                    controller = browserSessionViewModel.controller,
                    thumbnailRepository = browserSessionViewModel.thumbnailRepository,
                    library = application.browserLibrary,
                    downloads = application.downloadRepository,
                    updates = application.appUpdates,
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
                    externalNavigationUrl = externalNavigationUrl,
                    onExternalNavigationConsumed = { externalNavigationUrl = null },
                )
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        intent.httpNavigationUrl()?.let { externalNavigationUrl = it }
    }

    override fun onStart() {
        super.onStart()
        appLinksFeature.start()
        (application as DaoApplication).appUpdates.check()
    }

    override fun onStop() {
        appLinksFeature.stop()
        super.onStop()
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

    private companion object {
        var notificationPermissionRequested = false
    }
}
