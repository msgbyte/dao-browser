package com.msgbyte.dao.ui

import android.app.Activity
import android.app.role.RoleManager
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.ApplicationInfo
import android.content.pm.ResolveInfo
import android.net.Uri
import android.os.Process
import android.provider.Settings
import com.msgbyte.dao.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowRoleManager
import org.robolectric.shadows.ShadowToast

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class DefaultBrowserSettingsTest {
    private val activity = Robolectric.buildActivity(Activity::class.java).setup().get()

    @Test
    @Config(sdk = [29, 35])
    fun defaultBrowserStatusTracksRoleGrantsAndRemoval() {
        registerDefaultWebHandler(activity.packageName)
        ShadowRoleManager.addRoleHolder(RoleManager.ROLE_BROWSER, "other.browser", Process.myUserHandle())
        assertFalse(isDefaultBrowser(activity))

        ShadowRoleManager.addRoleHolder(RoleManager.ROLE_BROWSER, activity.packageName, Process.myUserHandle())
        assertTrue(isDefaultBrowser(activity))

        ShadowRoleManager.removeRoleHolder(RoleManager.ROLE_BROWSER, activity.packageName, Process.myUserHandle())
        assertFalse(isDefaultBrowser(activity))
    }

    @Test
    @Config(sdk = [26, 28, 35])
    fun daoIsRecognizedByTheDefaultWebHandlerWhenTheRoleIsUnavailable() {
        registerDefaultWebHandler(activity.packageName)

        assertTrue(isDefaultBrowser(activity))
    }

    @Test
    @Config(sdk = [26, 28, 35])
    fun anotherBrowserOrTheSystemChooserDoesNotHideTheEntry() {
        for (packageName in listOf("other.browser", "android")) {
            registerDefaultWebHandler(packageName)

            assertFalse(isDefaultBrowser(activity))
        }
    }

    @Test
    @Config(sdk = [29, 35])
    fun requestsTheBrowserRoleWhenAvailable() {
        ShadowRoleManager.addRoleHolder(RoleManager.ROLE_BROWSER, "", Process.myUserHandle())
        val requests = mutableListOf<Intent>()

        openDefaultBrowserSettings(activity) { requests.add(it) }

        assertEquals(1, requests.size)
        assertEquals("android.app.role.action.REQUEST_ROLE", requests.single().action)
        assertEquals(RoleManager.ROLE_BROWSER, requests.single().getStringExtra(RoleManager.EXTRA_ROLE_NAME))
    }

    @Test
    @Config(sdk = [26, 28])
    fun olderAndroidVersionsOpenDefaultAppSettings() {
        openDefaultBrowserSettings(activity, activity::startActivity)

        assertEquals(Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS, shadowOf(activity).nextStartedActivity.action)
    }

    @Test
    fun unavailableBrowserRoleOpensDefaultAppSettings() {
        openDefaultBrowserSettings(activity, activity::startActivity)

        assertEquals(Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS, shadowOf(activity).nextStartedActivity.action)
    }

    @Test
    fun daoAlreadyBeingDefaultOpensSettingsWithoutRequestingTheRoleAgain() {
        ShadowRoleManager.addRoleHolder(RoleManager.ROLE_BROWSER, activity.packageName, Process.myUserHandle())

        openDefaultBrowserSettings(activity, activity::startActivity)

        assertEquals(Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS, shadowOf(activity).nextStartedActivity.action)
    }

    @Test
    fun missingOrForbiddenRoleDialogsFallBackToDefaultAppSettings() {
        ShadowRoleManager.addRoleHolder(RoleManager.ROLE_BROWSER, "", Process.myUserHandle())
        for (failure in listOf(ActivityNotFoundException(), SecurityException())) {
            val actions = mutableListOf<String?>()

            openDefaultBrowserSettings(activity) { intent ->
                actions.add(intent.action)
                if (intent.action == "android.app.role.action.REQUEST_ROLE") throw failure
                activity.startActivity(intent)
            }

            assertEquals(
                listOf("android.app.role.action.REQUEST_ROLE", Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS),
                actions,
            )
            assertEquals(Settings.ACTION_MANAGE_DEFAULT_APPS_SETTINGS, shadowOf(activity).nextStartedActivity.action)
            assertNull(ShadowToast.getTextOfLatestToast())
        }
    }

    @Test
    fun missingOrForbiddenSettingsShowLocalizedFeedback() {
        for (failure in listOf(ActivityNotFoundException(), SecurityException())) {
            ShadowToast.reset()

            openDefaultBrowserSettings(activity) { throw failure }

            assertNull(shadowOf(activity).nextStartedActivity)
            assertEquals(
                activity.getString(R.string.default_browser_settings_unavailable),
                ShadowToast.getTextOfLatestToast(),
            )
        }
    }

    private fun registerDefaultWebHandler(packageName: String) {
        shadowOf(activity.packageManager).setResolveInfosForIntent(
            Intent(Intent.ACTION_VIEW, Uri.parse("http://")).addCategory(Intent.CATEGORY_BROWSABLE),
            listOf(ResolveInfo().apply {
                isDefault = true
                activityInfo = ActivityInfo().apply {
                    this.packageName = packageName
                    name = "BrowserActivity"
                    applicationInfo = ApplicationInfo().apply { this.packageName = packageName }
                    exported = true
                }
            }),
        )
    }
}
