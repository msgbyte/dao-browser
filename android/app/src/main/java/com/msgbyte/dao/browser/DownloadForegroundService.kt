package com.msgbyte.dao.browser

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationChannelCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import com.msgbyte.dao.DaoApplication
import com.msgbyte.dao.MainActivity
import com.msgbyte.dao.R
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/** Keeps the process alive and shows progress while [StreamedDownloadGateway] transfers files. */
class DownloadForegroundService : Service() {
    private val scope = MainScope()
    private var watching: Job? = null
    private var lastStartId = 0

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        lastStartId = startId
        val downloads = (application as DaoApplication).streamedDownloads
        val notifications = DownloadNotifications(this)
        ServiceCompat.startForeground(
            this,
            DownloadNotifications.PROGRESS_ID,
            notifications.progress(downloads.active.value),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC else 0,
        )
        if (watching == null) {
            watching = scope.launch {
                downloads.active.collect { active ->
                    if (active.isEmpty()) {
                        ServiceCompat.stopForeground(this@DownloadForegroundService, ServiceCompat.STOP_FOREGROUND_REMOVE)
                        // Only the latest start may stop the service, so a download queued meanwhile keeps it.
                        stopSelf(lastStartId)
                    } else {
                        notifications.showProgress(active)
                    }
                }
            }
        }
        return START_NOT_STICKY
    }

    // Android 15 caps dataSync services; transfers keep running until the process is reclaimed.
    override fun onTimeout(startId: Int, fgsType: Int) = stopSelf()

    override fun onDestroy() {
        scope.cancel()
        super.onDestroy()
    }

    companion object {
        fun start(context: Context) {
            ContextCompat.startForegroundService(context, Intent(context, DownloadForegroundService::class.java))
        }
    }
}

internal class DownloadNotifications(context: Context) {
    private val context = context.applicationContext
    private val manager = NotificationManagerCompat.from(this.context)

    init {
        manager.createNotificationChannel(
            NotificationChannelCompat.Builder(CHANNEL_ID, NotificationManagerCompat.IMPORTANCE_LOW)
                .setName(this.context.getString(R.string.downloads))
                .build(),
        )
    }

    fun progress(active: List<ActiveDownload>): Notification {
        val first = active.firstOrNull()
        val total = first?.totalBytes
        val percent = if (total == null) 0 else (first.bytesDownloaded * 100 / total).toInt().coerceIn(0, 100)
        return NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_notification_download)
            .setContentTitle(first?.fileName ?: context.getString(R.string.downloads))
            .setContentText(context.getString(R.string.downloading))
            .setProgress(100, percent, total == null)
            .setNumber(active.size)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setContentIntent(openApp())
            .build()
    }

    @SuppressLint("MissingPermission") // canPost() checks POST_NOTIFICATIONS.
    fun showProgress(active: List<ActiveDownload>) {
        if (canPost()) manager.notify(PROGRESS_ID, progress(active))
    }

    @SuppressLint("MissingPermission") // canPost() checks POST_NOTIFICATIONS.
    fun showFinished(request: DownloadRequestData, record: DownloadGatewayRecord) {
        if (!canPost()) return
        val success = record.status == DownloadGatewayStatus.SUCCESSFUL
        val open = record.localUri?.takeIf { success }?.let { uri ->
            PendingIntent.getActivity(
                context,
                record.id.hashCode(),
                downloadViewIntent(Uri.parse(uri), request.fileName, request.contentType)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
            )
        }
        manager.notify(
            FINISHED_TAG,
            record.id.hashCode(),
            NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(if (success) R.drawable.ic_notification_download_done else R.drawable.ic_notification_download_failed)
                .setContentTitle(request.fileName)
                .setContentText(context.getString(if (success) R.string.download_complete else R.string.download_failed))
                .setContentIntent(open ?: openApp())
                .setAutoCancel(true)
                .build(),
        )
    }

    fun cancelFinished(id: Long) = manager.cancel(FINISHED_TAG, id.hashCode())

    private fun canPost() = Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
        ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) ==
        PackageManager.PERMISSION_GRANTED

    private fun openApp() = PendingIntent.getActivity(
        context,
        0,
        Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        PendingIntent.FLAG_IMMUTABLE,
    )

    companion object {
        const val PROGRESS_ID = 0x0D0
        private const val CHANNEL_ID = "downloads"
        private const val FINISHED_TAG = "download-finished"
    }
}

/** Opens a finished download with a temporary read grant; APKs go to the package installer. */
internal fun downloadViewIntent(uri: Uri, fileName: String, contentType: String?): Intent {
    val mimeType = if (fileName.endsWith(".apk", ignoreCase = true)) {
        "application/vnd.android.package-archive"
    } else {
        contentType?.takeIf(String::isNotBlank) ?: "*/*"
    }
    return Intent(Intent.ACTION_VIEW).apply {
        setDataAndType(uri, mimeType)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
}
