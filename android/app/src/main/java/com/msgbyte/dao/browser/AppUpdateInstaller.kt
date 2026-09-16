package com.msgbyte.dao.browser

import android.content.Context
import android.content.pm.PackageInfo
import android.net.Uri
import androidx.core.content.FileProvider
import androidx.core.content.pm.PackageInfoCompat
import java.io.File
import java.io.IOException
import java.security.MessageDigest
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext

class AppUpdateFileProvider : FileProvider()

/** Stage immutable verified bytes, so a public Downloads file cannot change after validation. */
internal suspend fun prepareAppUpdate(
    context: Context,
    download: BrowserDownload,
    readArchive: (String) -> PackageInfo? = { context.packageManager.getPackageArchiveInfo(it, 0) },
): File = withContext(Dispatchers.IO) {
    val request = download.request
    val uri = download.localUri?.let(Uri::parse)
    val installed = context.packageManager.getPackageInfo(context.packageName, 0)
    if (download.status != DownloadStatus.SUCCESSFUL || uri?.scheme != "content" ||
        request.updateVersion == null || !isNewerAppVersion(request.updateVersion, installed.versionName.orEmpty()) ||
        !Regex("[0-9a-f]{64}").matches(request.updateSha256.orEmpty()) ||
        (request.contentLength ?: 0) <= 0) throw IOException("Invalid update download")

    val directory = File(context.cacheDir, "updates").apply { mkdirs() }
    val expiredBefore = System.currentTimeMillis() - 86_400_000
    directory.listFiles()?.filter { it.lastModified() < expiredBefore }?.forEach { it.delete() }
    val staged = File.createTempFile("update-${download.id}-", ".apk", directory)
    try {
        val digest = MessageDigest.getInstance("SHA-256")
        var length = 0L
        context.contentResolver.openInputStream(uri)?.use { input ->
            staged.outputStream().use { output ->
                val buffer = ByteArray(65_536)
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val count = input.read(buffer)
                    if (count < 0) break
                    length += count
                    if (length > request.contentLength!!) throw IOException("APK exceeds expected size")
                    digest.update(buffer, 0, count)
                    output.write(buffer, 0, count)
                }
            }
        } ?: throw IOException("Missing APK")
        val hash = digest.digest().joinToString("") { "%02x".format(it) }
        if (length != request.contentLength || hash != request.updateSha256) throw IOException("APK checksum mismatch")
        val archive = readArchive(staged.absolutePath) ?: throw IOException("Invalid APK")
        if (archive.packageName != context.packageName || archive.versionName != request.updateVersion ||
            PackageInfoCompat.getLongVersionCode(archive) <= PackageInfoCompat.getLongVersionCode(installed)) {
            throw IOException("APK package or version mismatch")
        }
        // The system installer enforces the existing signing certificate and device compatibility.
        staged
    } catch (error: Exception) {
        staged.delete()
        throw error
    }
}
