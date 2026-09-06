package com.msgbyte.dao.browser

import android.app.Application
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.pm.ApplicationInfo
import android.content.pm.ResolveInfo
import android.net.Uri
import android.os.Looper
import android.util.TypedValue
import android.widget.Button
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.FragmentActivity
import androidx.test.core.app.ApplicationProvider
import com.msgbyte.dao.R
import io.mockk.mockk
import mozilla.components.browser.state.state.AppIntentState
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.request.RequestInterceptor.InterceptionResponse
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Robolectric
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
class BrowserAppLinksInterceptorTest {
    private val context = ApplicationProvider.getApplicationContext<Application>()
    private val interceptor = BrowserAppLinksInterceptor(context, BrowserStore())
    private val session = mockk<EngineSession>(relaxed = true)

    @Test
    fun `MakerWorld custom scheme requests confirmation without launching immediately`() {
        val url = "bambulab://bbl/design/model/detail?design_id=2375306&instance_id=2695113"
        registerApp(url)

        val response = intercept(url) as InterceptionResponse.AppIntent

        assertEquals(url, response.appIntent.dataString)
        assertEquals("Bambu Handy", response.appName)
        assertTrue(response.appIntent.hasCategory(Intent.CATEGORY_BROWSABLE))
        assertFalse(response.appIntent.flags and Intent.FLAG_GRANT_READ_URI_PERMISSION != 0)
        assertNull(shadowOf(context).nextStartedActivity)
    }

    @Test
    fun `installed intent target takes priority over its web fallback`() {
        val url = "intent://bbl/model/1#Intent;scheme=bambulab;" +
            "S.browser_fallback_url=https%3A%2F%2Fmakerworld.com.cn%2Ffallback;end"
        registerApp("bambulab://bbl/model/1")

        val response = intercept(url) as InterceptionResponse.AppIntent

        assertEquals("bambulab://bbl/model/1", response.appIntent.dataString)
        assertEquals("https://makerworld.com.cn/fallback", response.fallbackUrl)
    }

    @Test
    fun `missing app loads a web fallback and never executes an unsafe fallback`() {
        val prefix = "intent://missing/model#Intent;scheme=uninstalled;S.browser_fallback_url="

        assertEquals(
            "https://makerworld.com.cn/fallback",
            (intercept(prefix + "https%3A%2F%2Fmakerworld.com.cn%2Ffallback;end")
                as InterceptionResponse.Url).url,
        )
        assertEquals(
            InterceptionResponse.Deny,
            intercept(prefix + "javascript%3Aalert(1);end"),
        )
    }

    @Test
    fun `ordinary web links and silent subframes do not request an external app`() {
        registerApp("https://example.com/model/1")
        assertNull(intercept("https://example.com/model/1"))
        registerApp("bambulab://bbl/model/2")
        assertNull(intercept("bambulab://bbl/model/2", subframe = true))
    }

    @Test
    fun `unsafe intent fallbacks are removed before confirmation`() {
        registerApp("bambulab://bbl/model/3")
        val response = intercept(
            "intent://bbl/model/3#Intent;scheme=bambulab;" +
                "S.browser_fallback_url=file%3A%2F%2F%2Fsdcard%2Fsecret;end",
        ) as InterceptionResponse.AppIntent

        assertNull(response.fallbackUrl)
        assertNull(response.appIntent.getStringExtra("browser_fallback_url"))
    }

    @Test
    @Config(qualifiers = "night")
    fun `light Dao confirmation overrides a dark system theme and requires approval`() {
        assertConfirmation(darkTheme = false)
    }

    @Test
    @Config(qualifiers = "notnight")
    fun `dark Dao confirmation overrides a light system theme and requires approval`() {
        assertConfirmation(darkTheme = true)
    }

    private fun assertConfirmation(darkTheme: Boolean) {
        val url = "bambulab://bbl/model/theme-$darkTheme"
        registerApp(url)
        val response = intercept(url) as InterceptionResponse.AppIntent
        val tab = TabSessionState(
            id = "selected",
            content = ContentState(
                url = "https://makerworld.com.cn/open-app",
                appIntent = AppIntentState(url, response.appIntent, response.fallbackUrl, response.appName),
            ),
        )
        val store = BrowserStore(BrowserState(tabs = listOf(tab), selectedTabId = tab.id))
        val controller = Robolectric.buildActivity(FragmentActivity::class.java)
        controller.get().setTheme(R.style.Theme_Dao)
        val activity = controller.setup().get()
        var currentDarkTheme = !darkTheme
        val feature = BrowserAppLinksInterceptor(context, store).createFeature(activity) { currentDarkTheme }
        try {
            currentDarkTheme = darkTheme
            feature.start()
            shadowOf(Looper.getMainLooper()).idle()
            activity.supportFragmentManager.executePendingTransactions()
            val dialog = (activity.supportFragmentManager.fragments.single() as DialogFragment).requireDialog()
            assertTrue(dialog.isShowing)
            val lightTheme = TypedValue()
            assertTrue(
                dialog.context.theme.resolveAttribute(androidx.appcompat.R.attr.isLightTheme, lightTheme, true),
            )
            assertEquals("The dialog must use Dao's current theme", !darkTheme, lightTheme.data != 0)
            assertNull(shadowOf(context).nextStartedActivity)

            // Mozilla's click protection uses java.util.Date, not the Robolectric clock.
            Thread.sleep(1100)
            dialog.findViewById<Button>(android.R.id.button1).performClick()
            shadowOf(Looper.getMainLooper()).idle()

            assertEquals(url, shadowOf(context).nextStartedActivity.dataString)
        } finally {
            feature.stop()
            controller.pause().stop().destroy()
        }
    }

    private fun intercept(url: String, subframe: Boolean = false) = interceptor.onLoadRequest(
        session,
        url,
        lastUri = "https://makerworld.com.cn/open-app/with-android",
        hasUserGesture = !subframe,
        isSameDomain = false,
        isRedirect = true,
        isDirectNavigation = false,
        isSubframeRequest = subframe,
    )

    private fun registerApp(url: String) {
        val applicationInfo = ApplicationInfo().apply {
            // Mozilla keeps its per-package launch throttle across tests.
            packageName = "com.example.handy${url.hashCode().toUInt()}"
            nonLocalizedLabel = "Bambu Handy"
        }
        val info = ResolveInfo().apply {
            activityInfo = ActivityInfo().apply {
                packageName = applicationInfo.packageName
                name = "HandyActivity"
                this.applicationInfo = applicationInfo
                exported = true
            }
        }
        shadowOf(context.packageManager).addResolveInfoForIntent(
            Intent(Intent.ACTION_VIEW, Uri.parse(url)).addCategory(Intent.CATEGORY_BROWSABLE),
            info,
        )
    }
}
