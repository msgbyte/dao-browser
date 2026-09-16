package com.msgbyte.dao.ui

import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.text.format.Formatter
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.FileProvider
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import com.msgbyte.dao.R
import com.msgbyte.dao.browser.AppUpdateManager
import com.msgbyte.dao.browser.BrowserDownload
import com.msgbyte.dao.browser.DownloadStatus
import com.msgbyte.dao.browser.SystemDownloadRepository
import com.msgbyte.dao.browser.UpdateError
import com.msgbyte.dao.browser.prepareAppUpdate
import com.msgbyte.dao.ui.theme.LocalNovaColors
import java.text.DateFormat
import java.util.Date
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

internal data class DownloadOpenAction(
    val preparing: Boolean,
    val error: Int?,
    val open: (BrowserDownload) -> Unit,
)

/** Both About and Downloads use this path for update APKs, including after process recreation. */
@Composable
internal fun rememberDownloadOpenAction(repository: SystemDownloadRepository): DownloadOpenAction {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var pendingId by rememberSaveable { mutableStateOf<Long?>(null) }
    var preparing by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<Int?>(null) }

    fun showError(message: Int) {
        error = message
        Toast.makeText(context, message, Toast.LENGTH_LONG).show()
    }

    val install: (Long) -> Unit = { id ->
        if (!preparing) {
            preparing = true
            error = null
            scope.launch {
                try {
                    repository.refresh()
                    val download = requireNotNull(repository.find(id))
                    val file = prepareAppUpdate(context, download)
                    val uri = FileProvider.getUriForFile(context, "${context.packageName}.updates", file)
                    context.startActivity(Intent(Intent.ACTION_VIEW).apply {
                        setDataAndType(uri, "application/vnd.android.package-archive")
                        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    })
                } catch (failure: Exception) {
                    if (failure is CancellationException) throw failure
                    showError(R.string.update_install_failed)
                } finally {
                    preparing = false
                }
            }
        }
    }
    val permissionLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        val id = pendingId
        pendingId = null
        if (id != null) {
            if (context.packageManager.canRequestPackageInstalls()) install(id)
            else showError(R.string.update_install_permission)
        }
    }
    return DownloadOpenAction(preparing, error) { download ->
        if (download.request.updateVersion == null && download.request.updateSha256 == null) {
            openCompletedDownload(context, download)
        } else if (!preparing && pendingId == null) {
            error = null
            if (context.packageManager.canRequestPackageInstalls()) {
                install(download.id)
            } else {
                pendingId = download.id
                try {
                    permissionLauncher.launch(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:${context.packageName}")))
                } catch (_: Exception) {
                    pendingId = null
                    showError(R.string.update_install_permission)
                }
            }
        }
    }
}

@Composable
internal fun AppUpdateCard(
    manager: AppUpdateManager,
    automaticChecks: Boolean,
    opener: DownloadOpenAction,
) {
    val state by manager.state.collectAsStateWithLifecycle()
    val downloads by manager.downloads.downloads.collectAsStateWithLifecycle()
    val colors = LocalNovaColors.current
    val context = LocalContext.current
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val automaticLabel = stringResource(R.string.update_automatic)
    val release = state.release
    val download = downloads.firstOrNull {
        release != null && it.request.url == release.url && it.request.updateSha256 == release.sha256 &&
            it.request.updateVersion == release.version
    }
    var refreshFailed by remember { mutableStateOf(false) }
    LaunchedEffect(manager, lifecycle) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            while (true) {
                try {
                    manager.downloads.refresh()
                    refreshFailed = false
                } catch (error: Exception) {
                    if (error is CancellationException) throw error
                    refreshFailed = true
                }
                delay(1_000)
            }
        }
    }

    NovaCard(Modifier.fillMaxWidth().testTag("app-update-card")) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(stringResource(R.string.app_updates), color = colors.foreground, fontSize = 16.sp,
                fontWeight = FontWeight.SemiBold)
            val checkError = when (state.error) {
                UpdateError.CHECK_FAILED -> R.string.update_check_failed
                UpdateError.RATE_LIMITED -> R.string.update_rate_limited
                UpdateError.INCOMPATIBLE -> R.string.update_incompatible
                UpdateError.DOWNLOAD_FAILED -> R.string.update_download_failed
                null -> null
            }
            when {
                state.checking -> Text(stringResource(R.string.update_checking), color = colors.muted)
                checkError != null -> Text(stringResource(checkError), color = colors.danger)
                release == null -> Text(stringResource(if (state.lastChecked > 0) R.string.update_current
                    else R.string.update_not_checked), color = colors.muted)
            }
            if (release != null) {
                Text(stringResource(R.string.update_available_version, release.version),
                    color = colors.foreground, fontWeight = FontWeight.Medium)
                Text(Formatter.formatFileSize(context, release.size), color = colors.muted, fontSize = 13.sp)
                if (release.notes.isNotBlank()) {
                    SelectionContainer { Text(release.notes, color = colors.muted, fontSize = 14.sp) }
                }
                if (opener.error != null) Text(stringResource(opener.error), color = colors.danger)
                when (download?.status) {
                    DownloadStatus.SUCCESSFUL -> {
                        Text(stringResource(R.string.download_complete), color = colors.muted)
                        Button(onClick = { opener.open(download) }, enabled = !opener.preparing,
                            modifier = Modifier.fillMaxWidth().testTag("install-app-update")) {
                            Text(stringResource(if (opener.preparing) R.string.update_verifying else R.string.update_install))
                        }
                        if (opener.error != null) {
                            TextButton(onClick = { manager.download(retryCompleted = true) },
                                enabled = !state.downloading && !opener.preparing) {
                                Text(stringResource(R.string.update_download_again))
                            }
                        }
                    }
                    DownloadStatus.PENDING, DownloadStatus.RUNNING, DownloadStatus.PAUSED -> {
                        Text(stringResource(when (download.status) {
                            DownloadStatus.PAUSED -> R.string.download_waiting
                            DownloadStatus.PENDING -> R.string.download_pending
                            else -> R.string.downloading
                        }), color = colors.muted)
                        LinearProgressIndicator(progress = { download.progress ?: 0f }, modifier = Modifier.fillMaxWidth())
                        Text(stringResource(R.string.update_download_progress,
                            Formatter.formatFileSize(context, download.bytesDownloaded),
                            Formatter.formatFileSize(context, release.size)), color = colors.muted, fontSize = 13.sp)
                        TextButton(onClick = { manager.cancelDownload(download.id) }) { Text(stringResource(R.string.cancel_download)) }
                    }
                    else -> {
                        if (download?.status == DownloadStatus.FAILED) Text(stringResource(R.string.download_failed), color = colors.danger)
                        Button(onClick = { manager.download() }, enabled = !state.downloading,
                            modifier = Modifier.fillMaxWidth().testTag("download-app-update")) {
                            Text(stringResource(if (download?.status == DownloadStatus.FAILED) R.string.retry else R.string.update_download))
                        }
                    }
                }
                if (refreshFailed) Text(stringResource(R.string.update_download_failed), color = colors.danger)
            }
            if (state.lastChecked > 0) {
                Text(stringResource(R.string.update_last_checked,
                    DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(state.lastChecked))),
                    color = colors.muted, fontSize = 12.sp)
            }
            TextButton(onClick = { manager.check(manual = true) }, enabled = !state.checking,
                modifier = Modifier.testTag("check-app-update")) {
                Text(stringResource(R.string.update_check))
            }
        }
        RowDivider()
        Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Column(Modifier.weight(1f)) {
                Text(automaticLabel, color = colors.foreground, fontSize = 14.sp)
                Text(stringResource(R.string.update_automatic_summary), color = colors.muted, fontSize = 12.sp)
            }
            NovaSwitch(automaticChecks, { manager.setAutomaticChecks(it) },
                modifier = Modifier.testTag("automatic-app-updates").semantics { contentDescription = automaticLabel })
        }
    }
}
