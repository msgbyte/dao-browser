package com.msgbyte.dao.about

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AboutAppInfoTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun readsBuildOwnedVersions() {
        val info = readAboutAppInfo(context)

        assertTrue(info.appVersion.isNotBlank())
        assertEquals("153.0.2", info.engineVersion)
    }

    @Test
    fun readsEveryBundledLicenseOffline() {
        BundledLicense.entries.forEach { license ->
            assertTrue(readBundledLicenseText(context, license).getOrThrow().isNotBlank())
        }
    }

    @Test
    fun missingLicenseIsReportedAsFailure() {
        assertTrue(readAssetText(context, "notices/missing.txt").isFailure)
    }
}
