package com.msgbyte.dao.browser

import androidx.datastore.preferences.core.PreferenceDataStoreFactory
import java.io.File
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class BrowserPreferencesTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test
    fun exposesRealDefaultsAndPersistsEverySetting() = runBlocking {
        val preferences = BrowserPreferences(
            PreferenceDataStoreFactory.create {
                File(temporaryFolder.root, "browser.preferences_pb")
            },
        )

        val defaults = preferences.state.first()
        assertFalse(defaults.darkTheme)
        assertEquals(BrowserFontScale.MEDIUM, defaults.fontScale)
        assertEquals(BrowserSearchEngine.GOOGLE, defaults.searchEngine)
        assertTrue(defaults.trackingProtectionEnabled)
        assertFalse(defaults.defaultPrivateBrowsing)
        assertFalse(defaults.remoteDebuggingEnabled)
        assertFalse(defaults.remoteDebuggingWarningAcknowledged)

        preferences.setDarkTheme(true)
        preferences.setFontScale(BrowserFontScale.LARGE)
        preferences.setSearchEngine(BrowserSearchEngine.DUCKDUCKGO)
        preferences.setTrackingProtectionEnabled(false)
        preferences.setDefaultPrivateBrowsing(true)

        assertEquals(
            BrowserPreferenceState(
                darkTheme = true,
                fontScale = BrowserFontScale.LARGE,
                searchEngine = BrowserSearchEngine.DUCKDUCKGO,
                trackingProtectionEnabled = false,
                defaultPrivateBrowsing = true,
                remoteDebuggingEnabled = false,
                remoteDebuggingWarningAcknowledged = false,
            ),
            preferences.state.first(),
        )
    }

    @Test
    fun firstRemoteDebuggingEnableAcknowledgesWarningWithoutForgettingItOnDisable() = runBlocking {
        val preferences = BrowserPreferences(
            PreferenceDataStoreFactory.create {
                File(temporaryFolder.root, "remote-debugging.preferences_pb")
            },
        )

        preferences.enableRemoteDebuggingWithAcknowledgement()

        assertTrue(preferences.state.first().remoteDebuggingEnabled)
        assertTrue(preferences.state.first().remoteDebuggingWarningAcknowledged)

        preferences.setRemoteDebuggingEnabled(false)

        assertFalse(preferences.state.first().remoteDebuggingEnabled)
        assertTrue(preferences.state.first().remoteDebuggingWarningAcknowledged)
    }

    @Test
    fun searchEnginesBuildUsableSearchUrls() {
        assertEquals("https://www.google.com/search?q=dao", BrowserSearchEngine.GOOGLE.searchUrl.format("dao"))
        assertEquals("https://duckduckgo.com/?q=dao", BrowserSearchEngine.DUCKDUCKGO.searchUrl.format("dao"))
        assertEquals(
            "https://www.baidu.com/s?wd=dao",
            BrowserSearchEngine.valueOf("BAIDU").searchUrl.format("dao"),
        )
        assertEquals(
            "https://www.bing.com/search?q=dao",
            BrowserSearchEngine.valueOf("BING").searchUrl.format("dao"),
        )
    }
}
