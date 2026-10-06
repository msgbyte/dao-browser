package com.msgbyte.dao.browser

import android.content.Context
import android.net.Uri
import android.os.Environment
import androidx.test.core.app.ApplicationProvider
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [28])
class AndroidDownloadSinkTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val sink = AndroidDownloadSink(context)

    @Test
    fun beforeAndroid10DownloadsAreSharedFromTheAppDownloadFolderThroughFileProvider() {
        val first = sink.create("report.pdf")
        first.open().use { it.write("one".toByteArray()) }
        val second = sink.create("report.pdf")

        val directory = context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)
        assertEquals("one", File(directory, "report.pdf").readText())
        assertTrue(File(directory, "report (1).pdf").exists())
        assertEquals("${context.packageName}.updates", Uri.parse(first.uri).authority)

        assertTrue(sink.delete(first.uri))
        assertFalse(File(directory, "report.pdf").exists())
        assertTrue(second.delete())
    }
}
