package com.msgbyte.dao.ui

import androidx.compose.runtime.getValue
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import androidx.core.content.FileProvider
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.msgbyte.dao.R
import com.msgbyte.dao.about.AboutAppInfo
import com.msgbyte.dao.browser.*
import com.msgbyte.dao.ui.theme.DaoTheme
import java.io.File
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AppUpdateScreenTest {
    @get:Rule val composeRule = createComposeRule()
    @get:Rule val folder = TemporaryFolder()
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val preferences by lazy {
        BrowserPreferences(PreferenceDataStoreFactory.create(scope = scope) {
            File(folder.root, "updates.preferences_pb")
        })
    }

    @After fun close() { scope.cancel() }

    @Test
    fun installerProviderReadsStagedBytesAndRejectsOtherPrivateFiles() {
        val directory = File(context.cacheDir, "updates").apply { mkdirs() }
        val staged = File.createTempFile("provider-test-", ".apk", directory)
        try {
            staged.writeText("Verified update bytes")
            val uri = FileProvider.getUriForFile(context, "${context.packageName}.updates", staged)
            assertEquals("content", uri.scheme)
            assertEquals("Verified update bytes", context.contentResolver.openInputStream(uri)!!.bufferedReader().use { it.readText() })
            assertTrue(runCatching {
                FileProvider.getUriForFile(context, "${context.packageName}.updates", File(context.filesDir, "private.txt"))
            }.exceptionOrNull() is IllegalArgumentException)
        } finally {
            staged.delete()
        }
    }

    @Test
    fun manualCheckShowsFailureThenSuccessAndAutomaticTogglePersists() {
        val offline = AtomicBoolean(true)
        val manager = AppUpdateManager(preferences, SystemDownloadRepository(context), "0.1.2",
            listOf("arm64-v8a"), AppUpdateRepository {
                if (offline.get()) throw IOException("Offline")
                "[]"
            }, scope)
        runBlocking { preferences.setAutomaticUpdateChecks(false) }
        composeRule.setContent {
            val prefs by preferences.state.collectAsStateWithLifecycle(BrowserPreferenceState())
            DaoTheme {
                AboutScreen(AboutAppInfo("0.1.2", "153.0.2"), {}, {}, updateContent = {
                    AppUpdateCard(manager, prefs.automaticUpdateChecks, DownloadOpenAction(false, null) {})
                })
            }
        }
        composeRule.onNodeWithTag("about-content").performScrollToNode(hasTestTag("check-app-update"))
        composeRule.onNodeWithTag("check-app-update").performClick()
        composeRule.waitUntil(5_000) { manager.state.value.error == UpdateError.CHECK_FAILED }
        composeRule.onNodeWithText(context.getString(R.string.update_check_failed)).assertIsDisplayed()
        composeRule.onNodeWithText(context.getString(R.string.update_current)).assertDoesNotExist()

        offline.set(false)
        composeRule.onNodeWithTag("check-app-update").performClick()
        composeRule.waitUntil(5_000) { manager.state.value.lastChecked > 0 && !manager.state.value.checking }
        composeRule.onNodeWithText(context.getString(R.string.update_current)).assertIsDisplayed()
        composeRule.onNodeWithTag("about-content").performScrollToNode(hasTestTag("automatic-app-updates"))
        composeRule.onNodeWithTag("automatic-app-updates").performClick()
        composeRule.waitUntil(5_000) { runBlocking { preferences.state.first().automaticUpdateChecks } }
        assertTrue(runBlocking { preferences.state.first().automaticUpdateChecks })
    }

    @Test
    fun cachedCompletedUpdateShowsNotesAndInvokesSharedInstallActionInDarkTheme() {
        val release = AppUpdateRelease("0.2.0", "dao.apk", "https://example.com/dao.apk", 123,
            "ab".repeat(32), "Improved browsing stability.")
        val request = DownloadRequestData(release.url, release.fileName, release.size,
            updateVersion = release.version, updateSha256 = release.sha256)
        val downloads = SystemDownloadRepository(object : DownloadGateway {
            override fun enqueue(request: DownloadRequestData) = error("Must reuse the existing download")
            override fun remove(id: Long) = Unit
            override fun query(ids: Set<Long>) = listOf(DownloadGatewayRecord(42,
                DownloadGatewayStatus.SUCCESSFUL, 123, 123, "content://downloads/all_downloads/42", 0, 1))
        }, object : DownloadMetadataStore {
            override fun readAll() = mapOf(42L to request)
            override fun writeAll(values: Map<Long, DownloadRequestData>) = Unit
        })
        val manager = AppUpdateManager(preferences, downloads, "0.1.2", listOf("arm64-v8a"),
            AppUpdateRepository { error("Automatic checks are disabled") }, scope)
        runBlocking {
            preferences.setAutomaticUpdateChecks(false)
            preferences.recordUpdateCheckResult(System.currentTimeMillis(), release.toJson())
            manager.check().join()
            downloads.refresh()
        }
        var opened: Long? = null
        composeRule.setContent {
            DaoTheme(darkTheme = true) {
                AboutScreen(AboutAppInfo("0.1.2", "153.0.2"), {}, {}, updateContent = {
                    AppUpdateCard(manager, false, DownloadOpenAction(false, null) { opened = it.id })
                })
            }
        }
        composeRule.onNodeWithTag("about-content").performScrollToNode(hasTestTag("install-app-update"))
        composeRule.onNodeWithText(context.getString(R.string.update_available_version, "0.2.0")).assertIsDisplayed()
        composeRule.onNodeWithText(release.notes).assertIsDisplayed()
        composeRule.onNodeWithTag("install-app-update").assertIsDisplayed().performClick()
        assertEquals(42L, opened)
    }
}
