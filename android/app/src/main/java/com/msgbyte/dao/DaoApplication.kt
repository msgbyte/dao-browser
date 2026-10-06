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
import com.msgbyte.dao.browser.AndroidDownloadSink
import com.msgbyte.dao.browser.DownloadForegroundService
import com.msgbyte.dao.browser.DownloadNotifications
import com.msgbyte.dao.browser.PageImageActions
import com.msgbyte.dao.browser.SharedPreferencesStreamedDownloadRecordStore
import com.msgbyte.dao.browser.StreamedDownloadGateway
import com.msgbyte.dao.browser.guessDownloadFileName
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import mozilla.components.browser.engine.gecko.fetch.GeckoViewFetchClient
import mozilla.components.concept.fetch.Request
import mozilla.components.browser.engine.gecko.GeckoEngine
import mozilla.components.concept.engine.DefaultSettings
import mozilla.components.concept.engine.DownloadDelegate
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings

class DaoApplication : Application() {
    val amoCatalogRepository by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        AmoCatalogRepository()
    }

    val browserLibrary by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        BrowserLibraryRepository(applicationContext)
    }

    internal val streamedDownloads by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        val notifications = DownloadNotifications(applicationContext)
        StreamedDownloadGateway(
            sink = AndroidDownloadSink(applicationContext),
            recordStore = SharedPreferencesStreamedDownloadRecordStore(applicationContext),
            fetch = { request ->
                fetchClient.fetch(
                    Request(
                        url = request.url,
                        cookiePolicy = Request.CookiePolicy.INCLUDE,
                        useCaches = false,
                        private = request.isPrivate,
                    ),
                )
            },
            scope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
            onStarted = { DownloadForegroundService.start(applicationContext) },
            onFinished = notifications::showFinished,
            onRemoved = notifications::cancelFinished,
        )
    }

    val downloadRepository by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        SystemDownloadRepository(applicationContext, streamedDownloads)
    }

    val pageImages by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        PageImageActions(applicationContext, fetchClient::fetch, downloadRepository)
    }

    private val fetchClient by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        GeckoViewFetchClient(applicationContext, geckoRuntime)
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

    val engineSettings = DefaultSettings(
        automaticFontSizeAdjustment = false,
        // Without a delegate Gecko reports no file name, so Content-Disposition names are lost.
        downloadDelegate = object : DownloadDelegate {
            override fun guessFileName(contentDisposition: String?, url: String?, mimeType: String?) =
                guessDownloadFileName(contentDisposition, url, mimeType)
        },
    )

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
