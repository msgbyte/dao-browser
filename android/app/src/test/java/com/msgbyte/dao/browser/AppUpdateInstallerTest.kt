package com.msgbyte.dao.browser

import android.content.Context
import android.content.pm.PackageInfo
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import java.io.ByteArrayInputStream
import java.security.MessageDigest
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AppUpdateInstallerTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val payload = "APK bytes".toByteArray()
    private val hash = MessageDigest.getInstance("SHA-256").digest(payload).joinToString("") { "%02x".format(it) }
    private val uri = Uri.parse("content://downloads/all_downloads/42")
    private val download = BrowserDownload(
        42, DownloadRequestData("https://example.com/dao.apk", "dao.apk", payload.size.toLong(),
            updateVersion = "0.2.0", updateSha256 = hash),
        DownloadStatus.SUCCESSFUL, payload.size.toLong(), payload.size.toLong(), uri.toString(), 0, 0,
    )

    @Test
    fun stagesVerifiedBytesPrivatelyAndRejectsHashPackageVersionAndDowngradeFailures() = runBlocking {
        fun packageInfo() = PackageInfo().apply {
            packageName = context.packageName
            versionName = "0.2.0"
            setLongVersionCode(100)
        }
        fun input() = shadowOf(context.contentResolver).registerInputStream(uri, ByteArrayInputStream(payload))
        input()
        val file = prepareAppUpdate(context, download) { packageInfo() }
        assertTrue(file.canonicalPath.startsWith(context.cacheDir.canonicalPath + "/updates/"))
        assertArrayEquals(payload, file.readBytes())
        file.delete()

        input()
        assertTrue(runCatching {
            prepareAppUpdate(context, download.copy(request = download.request.copy(updateSha256 = "00".repeat(32)))) {
                error("Corrupt bytes must be rejected before parsing the APK")
            }
        }.isFailure)
        for (invalid in listOf(
            packageInfo().apply { packageName = "another.app" },
            packageInfo().apply { versionName = "0.1.0" },
            packageInfo().apply { setLongVersionCode(1) },
        )) {
            input()
            assertTrue(runCatching { prepareAppUpdate(context, download) { invalid } }.isFailure)
        }
        assertTrue(java.io.File(context.cacheDir, "updates").listFiles().orEmpty().isEmpty())
    }

    @Test
    fun incompleteMissingAndNonContentDownloadsCannotReachPackageParsing() = runBlocking {
        for (invalid in listOf(
            download.copy(status = DownloadStatus.RUNNING),
            download.copy(localUri = null),
            download.copy(localUri = "file:///sdcard/dao.apk"),
            download.copy(request = download.request.copy(updateVersion = null)),
        )) {
            assertTrue(runCatching {
                prepareAppUpdate(context, invalid) { error("Must not parse an invalid download") }
            }.isFailure)
        }
    }
}
