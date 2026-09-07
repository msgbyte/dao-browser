package com.msgbyte.dao.browser

import android.Manifest
import android.app.DownloadManager
import android.content.Context
import android.content.pm.PackageManager
import android.database.MatrixCursor
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class CompletedDownloadTest {
    private val application = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun completedDownloadExposesTheSystemShareUriInsteadOfItsFilePath() = runBlocking {
        val manager = mockk<DownloadManager>()
        val context = mockk<Context>()
        every { context.applicationContext } returns context
        every { context.getSystemService(DownloadManager::class.java) } returns manager
        every { context.getSharedPreferences(any(), any()) } answers {
            application.getSharedPreferences(firstArg(), secondArg())
        }
        every { manager.enqueue(any()) } returns 42L
        every { manager.query(any()) } answers {
            MatrixCursor(arrayOf(
                DownloadManager.COLUMN_ID,
                DownloadManager.COLUMN_STATUS,
                DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR,
                DownloadManager.COLUMN_TOTAL_SIZE_BYTES,
                DownloadManager.COLUMN_LOCAL_URI,
                DownloadManager.COLUMN_REASON,
                DownloadManager.COLUMN_LAST_MODIFIED_TIMESTAMP,
            )).apply {
                addRow(arrayOf<Any>(42L, DownloadManager.STATUS_SUCCESSFUL, 100L, 100L,
                    "file:///storage/emulated/0/Download/dao.apk", 0, 1L))
            }
        }
        every { manager.getUriForDownloadedFile(42L) } returns Uri.parse("content://downloads/all_downloads/42")
        val repository = SystemDownloadRepository(context)
        repository.enqueue(DownloadRequestData("https://example.com/dao.apk", "dao.apk"))

        repository.refresh()

        assertEquals("content://downloads/all_downloads/42", repository.find(42L)?.localUri)
    }

    @Test
    fun appDeclaresPermissionToRequestPackageInstallation() {
        val info = application.packageManager.getPackageInfo(application.packageName, PackageManager.GET_PERMISSIONS)
        assertTrue(Manifest.permission.REQUEST_INSTALL_PACKAGES in info.requestedPermissions.orEmpty())
    }
}
