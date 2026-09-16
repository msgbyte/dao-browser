package com.msgbyte.dao.browser

import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.test.core.app.ApplicationProvider
import android.content.Context
import java.io.File
import java.io.IOException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.StandardTestDispatcher
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AppUpdateManagerTest {
    @get:Rule val folder = TemporaryFolder()

    @Test
    fun systemDownloadMetadataRetainsUpdateIdentityAcrossRepositoryRecreation() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val repository = SystemDownloadRepository(context)
        val request = DownloadRequestData("https://example.com/dao.apk", "dao.apk", 123,
            updateVersion = "0.2.0", updateSha256 = "ab".repeat(32))
        val id = repository.enqueue(request)
        assertEquals(request, SystemDownloadRepository(context).find(id)?.request)
    }

    @Test
    fun failedAutomaticCheckIsThrottledButManualCheckCanRetryAndDisabledChecksStayOff() = runTest {
        val preferences = BrowserPreferences(PreferenceDataStoreFactory.create(scope = backgroundScope) {
            File(folder.root, "updates.preferences_pb")
        })
        var requests = 0
        val manager = AppUpdateManager(preferences, downloads(), "0.1.2", listOf("arm64-v8a"),
            AppUpdateRepository(StandardTestDispatcher(testScheduler)) { requests++; throw IOException("Offline") }, backgroundScope, { 100_000_000L })
        manager.check().join()
        assertEquals(UpdateError.CHECK_FAILED, manager.state.value.error)
        manager.check().join()
        assertEquals(1, requests)
        manager.check(manual = true).join()
        assertEquals(2, requests)
        preferences.setAutomaticUpdateChecks(false)
        preferences.recordUpdateCheckAttempt(0)
        manager.check().join()
        assertEquals(2, requests)
        assertFalse(preferences.state.first().automaticUpdateChecks)
    }

    @Test
    fun concurrentChecksShareOneRequestAndRateLimitsAlsoApplyToManualChecks() = runTest {
        val preferences = BrowserPreferences(PreferenceDataStoreFactory.create(scope = backgroundScope) {
            File(folder.root, "concurrent.preferences_pb")
        })
        val entered = CompletableDeferred<Unit>()
        val finish = CompletableDeferred<Unit>()
        var requests = 0
        val manager = AppUpdateManager(preferences, downloads(), "0.1.2", listOf("arm64-v8a"),
            AppUpdateRepository(StandardTestDispatcher(testScheduler)) {
                requests++
                entered.complete(Unit)
                finish.await()
                throw UpdateRateLimited(200_000_000L)
            }, backgroundScope, { 100_000_000L })
        val first = manager.check(manual = true)
        entered.await()
        manager.check(manual = true).join()
        assertEquals(1, requests)
        finish.complete(Unit)
        first.join()
        manager.check(manual = true).join()
        assertEquals(1, requests)
        assertEquals(UpdateError.RATE_LIMITED, manager.state.value.error)
    }

    @Test
    fun cachedUpdateRestoresAndRepeatedDownloadActionsReusePersistedTask() = runTest {
        val preferences = BrowserPreferences(PreferenceDataStoreFactory.create(scope = backgroundScope) {
            File(folder.root, "download.preferences_pb")
        })
        val release = AppUpdateRelease("0.1.10", "dao.apk", "https://example.com/dao.apk", 123, "ab".repeat(32), "Notes")
        preferences.recordUpdateCheckResult(99_000_000L, release.toJson())
        preferences.recordUpdateCheckAttempt(99_000_000L)
        val downloads = downloads()
        val manager = AppUpdateManager(preferences, downloads, "0.1.2", listOf("arm64-v8a"),
            AppUpdateRepository(StandardTestDispatcher(testScheduler)) { error("Cached check must be throttled") }, backgroundScope, { 100_000_000L })
        manager.check().join()
        assertEquals(release, manager.state.value.release)
        manager.download().join()
        val id = downloads.downloads.value.single().id
        manager.download().join()
        assertEquals(id, downloads.downloads.value.single().id)
        assertEquals(release.sha256, downloads.find(id)?.request?.updateSha256)
        val afterUpgrade = AppUpdateManager(preferences, downloads, "0.1.10", listOf("arm64-v8a"),
            AppUpdateRepository(StandardTestDispatcher(testScheduler)) { "[]" }, backgroundScope, { 100_000_000L })
        afterUpgrade.check().join()
        assertNull(afterUpgrade.state.value.release)
    }

    private fun downloads(): SystemDownloadRepository = SystemDownloadRepository(
        object : DownloadGateway {
            var nextId = 1L
            override fun enqueue(request: DownloadRequestData) = nextId++
            override fun query(ids: Set<Long>) = ids.map {
                DownloadGatewayRecord(it, DownloadGatewayStatus.PENDING, 0, 123, null, 0, 0)
            }
            override fun remove(id: Long) = Unit
        },
        object : DownloadMetadataStore {
            override fun readAll() = emptyMap<Long, DownloadRequestData>()
            override fun writeAll(values: Map<Long, DownloadRequestData>) = Unit
        },
    )
}
