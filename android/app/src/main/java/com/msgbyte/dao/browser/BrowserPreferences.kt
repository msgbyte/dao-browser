package com.msgbyte.dao.browser

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import java.io.IOException
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.map

private val Context.browserPreferencesDataStore by preferencesDataStore(name = "appearance")

enum class BrowserFontScale(val factor: Float) {
    SMALL(0.85f),
    MEDIUM(1.0f),
    LARGE(1.2f),
}

enum class BrowserSearchEngine(val searchUrl: String) {
    GOOGLE("https://www.google.com/search?q=%s"),
    BAIDU("https://www.baidu.com/s?wd=%s"),
    BING("https://www.bing.com/search?q=%s"),
    DUCKDUCKGO("https://duckduckgo.com/?q=%s"),
}

data class BrowserPreferenceState(
    val darkTheme: Boolean = false,
    val fontScale: BrowserFontScale = BrowserFontScale.MEDIUM,
    val searchEngine: BrowserSearchEngine = BrowserSearchEngine.GOOGLE,
    val trackingProtectionEnabled: Boolean = true,
    val defaultPrivateBrowsing: Boolean = false,
    val remoteDebuggingEnabled: Boolean = false,
    val remoteDebuggingWarningAcknowledged: Boolean = false,
    val automaticUpdateChecks: Boolean = true,
    val lastUpdateCheckAttempt: Long = 0,
    val lastUpdateCheckSuccess: Long = 0,
    val updateRetryAt: Long = 0,
    val cachedAppUpdate: String = "",
)

class BrowserPreferences(
    private val dataStore: DataStore<Preferences>,
) {
    constructor(context: Context) : this(context.browserPreferencesDataStore)

    val state: Flow<BrowserPreferenceState> = dataStore.data
        .catch { exception ->
            if (exception is IOException) emit(emptyPreferences()) else throw exception
        }
        .map { preferences ->
            BrowserPreferenceState(
                darkTheme = preferences[DarkThemeKey] ?: false,
                fontScale = enumValueOrDefault(
                    preferences[FontScaleKey],
                    BrowserFontScale.MEDIUM,
                ),
                searchEngine = enumValueOrDefault(
                    preferences[SearchEngineKey],
                    BrowserSearchEngine.GOOGLE,
                ),
                trackingProtectionEnabled = preferences[TrackingProtectionKey] ?: true,
                defaultPrivateBrowsing = preferences[DefaultPrivateBrowsingKey] ?: false,
                remoteDebuggingEnabled = preferences[RemoteDebuggingEnabledKey] ?: false,
                remoteDebuggingWarningAcknowledged =
                    preferences[RemoteDebuggingWarningAcknowledgedKey] ?: false,
                automaticUpdateChecks = preferences[AutomaticUpdateChecksKey] ?: true,
                lastUpdateCheckAttempt = preferences[LastUpdateCheckAttemptKey] ?: 0,
                lastUpdateCheckSuccess = preferences[LastUpdateCheckSuccessKey] ?: 0,
                updateRetryAt = preferences[UpdateRetryAtKey] ?: 0,
                cachedAppUpdate = preferences[CachedAppUpdateKey] ?: "",
            )
        }

    suspend fun setDarkTheme(enabled: Boolean) = update(DarkThemeKey, enabled)

    suspend fun setAutomaticUpdateChecks(enabled: Boolean) = update(AutomaticUpdateChecksKey, enabled)

    suspend fun recordUpdateCheckAttempt(time: Long) {
        dataStore.edit { it[LastUpdateCheckAttemptKey] = time }
    }

    suspend fun recordUpdateCheckResult(time: Long, release: String) {
        dataStore.edit {
            it[LastUpdateCheckSuccessKey] = time
            it[CachedAppUpdateKey] = release
            it[UpdateRetryAtKey] = 0
        }
    }

    suspend fun setUpdateRetryAt(time: Long) {
        dataStore.edit { it[UpdateRetryAtKey] = time }
    }

    suspend fun setFontScale(scale: BrowserFontScale) = update(FontScaleKey, scale.name)

    suspend fun setSearchEngine(engine: BrowserSearchEngine) = update(SearchEngineKey, engine.name)

    suspend fun setTrackingProtectionEnabled(enabled: Boolean) =
        update(TrackingProtectionKey, enabled)

    suspend fun setDefaultPrivateBrowsing(enabled: Boolean) =
        update(DefaultPrivateBrowsingKey, enabled)

    suspend fun setRemoteDebuggingEnabled(enabled: Boolean) =
        update(RemoteDebuggingEnabledKey, enabled)

    suspend fun enableRemoteDebuggingWithAcknowledgement() {
        dataStore.edit { preferences ->
            preferences[RemoteDebuggingEnabledKey] = true
            preferences[RemoteDebuggingWarningAcknowledgedKey] = true
        }
    }

    private suspend fun update(key: Preferences.Key<Boolean>, value: Boolean) {
        dataStore.edit { preferences -> preferences[key] = value }
    }

    private suspend fun update(key: Preferences.Key<String>, value: String) {
        dataStore.edit { preferences -> preferences[key] = value }
    }

    private companion object {
        val AutomaticUpdateChecksKey = booleanPreferencesKey("automatic_update_checks")
        val LastUpdateCheckAttemptKey = longPreferencesKey("last_update_check_attempt")
        val LastUpdateCheckSuccessKey = longPreferencesKey("last_update_check_success")
        val UpdateRetryAtKey = longPreferencesKey("update_retry_at")
        val CachedAppUpdateKey = stringPreferencesKey("cached_app_update")
        val DarkThemeKey = booleanPreferencesKey("dark_theme")
        val FontScaleKey = stringPreferencesKey("font_scale")
        val SearchEngineKey = stringPreferencesKey("search_engine")
        val TrackingProtectionKey = booleanPreferencesKey("tracking_protection")
        val DefaultPrivateBrowsingKey = booleanPreferencesKey("default_private_browsing")
        val RemoteDebuggingEnabledKey = booleanPreferencesKey("remote_debugging_enabled")
        val RemoteDebuggingWarningAcknowledgedKey =
            booleanPreferencesKey("remote_debugging_warning_acknowledged")

        inline fun <reified T : Enum<T>> enumValueOrDefault(value: String?, default: T): T =
            value?.let { runCatching { enumValueOf<T>(it) }.getOrNull() } ?: default
    }
}
