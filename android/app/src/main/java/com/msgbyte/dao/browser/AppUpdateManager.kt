package com.msgbyte.dao.browser

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout

enum class UpdateError { CHECK_FAILED, RATE_LIMITED, INCOMPATIBLE, DOWNLOAD_FAILED }

data class AppUpdateState(
    val checking: Boolean = false,
    val downloading: Boolean = false,
    val lastChecked: Long = 0,
    val release: AppUpdateRelease? = null,
    val error: UpdateError? = null,
)

class AppUpdateManager(
    private val preferences: BrowserPreferences,
    val downloads: SystemDownloadRepository,
    private val installedVersion: String,
    private val supportedAbis: List<String>,
    private val repository: AppUpdateRepository = AppUpdateRepository(),
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate),
    private val now: () -> Long = System::currentTimeMillis,
) {
    private val mutableState = MutableStateFlow(AppUpdateState())
    val state = mutableState.asStateFlow()
    private val checkMutex = Mutex()
    private val downloadMutex = Mutex()

    fun check(manual: Boolean = false) = scope.launch {
        if (!checkMutex.tryLock()) return@launch
        try {
            val prefs = preferences.state.first()
            val cached = runCatching { AppUpdateRelease.fromJson(prefs.cachedAppUpdate) }.getOrNull()
                ?.takeIf { isNewerAppVersion(it.version, installedVersion) }
            mutableState.update { it.copy(release = cached, lastChecked = prefs.lastUpdateCheckSuccess) }
            val time = now()
            if (!manual && (!prefs.automaticUpdateChecks ||
                    (prefs.lastUpdateCheckAttempt > 0 && time >= prefs.lastUpdateCheckAttempt &&
                        time - prefs.lastUpdateCheckAttempt < 86_400_000))) return@launch
            if (time < prefs.updateRetryAt) {
                mutableState.update { it.copy(error = UpdateError.RATE_LIMITED) }
                return@launch
            }
            mutableState.update { it.copy(checking = true, error = null) }
            // Record failures too, so an offline startup cannot hammer the release API.
            preferences.recordUpdateCheckAttempt(time)
            val release = withTimeout(45_000) { repository.findUpdate(installedVersion, supportedAbis) }
            val completedAt = now()
            preferences.recordUpdateCheckResult(completedAt, release?.toJson().orEmpty())
            mutableState.update { it.copy(release = release, lastChecked = completedAt, error = null) }
        } catch (error: Exception) {
            if (error is CancellationException && error !is TimeoutCancellationException) throw error
            val failure = when (error) {
                is UpdateRateLimited -> {
                    preferences.setUpdateRetryAt(error.retryAt)
                    UpdateError.RATE_LIMITED
                }
                is NoCompatibleUpdate -> UpdateError.INCOMPATIBLE
                else -> UpdateError.CHECK_FAILED
            }
            mutableState.update { it.copy(error = failure) }
        } finally {
            mutableState.update { it.copy(checking = false) }
            checkMutex.unlock()
        }
    }

    fun download(retryCompleted: Boolean = false) = scope.launch {
        downloadMutex.withLock {
            val release = state.value.release ?: return@withLock
            mutableState.update { it.copy(downloading = true, error = null) }
            try {
                downloads.refresh()
                val existing = downloads.downloads.value.firstOrNull {
                    it.request.url == release.url && it.request.updateSha256 == release.sha256 &&
                        it.request.updateVersion == release.version
                }
                if (existing?.status == DownloadStatus.FAILED ||
                    (retryCompleted && existing?.status == DownloadStatus.SUCCESSFUL)) {
                    requireNotNull(existing)
                    downloads.retry(existing.id)
                } else if (existing == null) {
                    downloads.enqueue(DownloadRequestData(
                        url = release.url,
                        fileName = release.fileName,
                        contentLength = release.size,
                        contentType = "application/vnd.android.package-archive",
                        updateVersion = release.version,
                        updateSha256 = release.sha256,
                    ))
                }
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                mutableState.update { it.copy(error = UpdateError.DOWNLOAD_FAILED) }
            } finally {
                mutableState.update { it.copy(downloading = false) }
            }
        }
    }

    fun cancelDownload(id: Long) = scope.launch {
        downloadMutex.withLock {
            try {
                downloads.cancel(id)
                mutableState.update { it.copy(error = null) }
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                mutableState.update { it.copy(error = UpdateError.DOWNLOAD_FAILED) }
            }
        }
    }

    fun setAutomaticChecks(enabled: Boolean) = scope.launch {
        preferences.setAutomaticUpdateChecks(enabled)
    }
}
