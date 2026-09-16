package com.msgbyte.dao

import android.app.Application
import android.os.Build
import com.msgbyte.dao.browser.BrowserLibraryRepository
import com.msgbyte.dao.browser.BrowserPreferences
import com.msgbyte.dao.browser.BrowserRuntime
import com.msgbyte.dao.browser.AmoCatalogRepository
import com.msgbyte.dao.browser.ExtensionRepository
import com.msgbyte.dao.browser.SystemDownloadRepository
import com.msgbyte.dao.browser.AppUpdateManager
import mozilla.components.browser.engine.gecko.GeckoEngine
import mozilla.components.concept.engine.DefaultSettings
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings

class DaoApplication : Application() {
    val amoCatalogRepository by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        AmoCatalogRepository()
    }

    val browserLibrary by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        BrowserLibraryRepository(applicationContext)
    }

    val downloadRepository by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        SystemDownloadRepository(applicationContext)
    }

    val browserPreferences by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        BrowserPreferences(applicationContext)
    }

    val appUpdates by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        AppUpdateManager(
            browserPreferences,
            downloadRepository,
            packageManager.getPackageInfo(packageName, 0).versionName.orEmpty(),
            Build.SUPPORTED_ABIS.toList(),
        )
    }

    private val geckoRuntime by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        GeckoRuntime.create(
            applicationContext,
            GeckoRuntimeSettings.Builder()
                .remoteDebuggingEnabled(false)
                .build(),
        )
    }

    val engineSettings = DefaultSettings(automaticFontSizeAdjustment = false)

    val browserRuntime = BrowserRuntime(
        createEngine = {
            GeckoEngine(applicationContext, defaultSettings = engineSettings, runtime = geckoRuntime)
        },
        setRemoteDebugging = { enabled ->
            geckoRuntime.settings.setRemoteDebuggingEnabled(enabled)
        },
    )

    val extensionRepository by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        ExtensionRepository(browserRuntime.engine)
    }
}
