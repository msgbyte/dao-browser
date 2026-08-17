package com.msgbyte.dao

import android.content.ClipboardManager
import android.net.Uri
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.AdaptiveIconDrawable
import android.view.View
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsFocused
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.click
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeRight
import androidx.compose.ui.test.down
import androidx.compose.ui.test.up
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.graphics.toPixelMap
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.msgbyte.dao.ui.NEW_TAB_SCREEN_TEST_TAG
import com.msgbyte.dao.ui.EDGE_BACK_GESTURE_TEST_TAG
import com.msgbyte.dao.ui.TAB_GRID_TEST_TAG
import com.msgbyte.dao.ui.TAB_CARD_TEST_TAG_PREFIX
import com.msgbyte.dao.ui.TAB_THUMBNAIL_TEST_TAG_PREFIX
import com.msgbyte.dao.ui.BOOKMARK_OPTIONS_TEST_TAG_PREFIX
import com.msgbyte.dao.about.readAboutAppInfo
import com.msgbyte.dao.browser.BrowserFontScale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.math.max
import kotlin.math.roundToInt
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.flow.first
import mozilla.components.concept.engine.webextension.WebExtension
import org.junit.Assert.assertFalse
import org.junit.Assert.fail
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test

class MainActivityTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<MainActivity>()

    @Before
    fun resetToBlankTab() {
        val tabs = composeRule.activity.getString(R.string.tab_switcher)
        val closeTab = composeRule.activity.getString(R.string.close_tab)
        val tabCard = SemanticsMatcher("BrowserStore tab card") { node ->
            node.config.contains(SemanticsProperties.TestTag) &&
                node.config[SemanticsProperties.TestTag].startsWith(TAB_CARD_TEST_TAG_PREFIX)
        }

        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodesWithContentDescription(tabs).fetchSemanticsNodes().isNotEmpty() ||
                composeRule.onAllNodesWithTag(TAB_GRID_TEST_TAG).fetchSemanticsNodes().isNotEmpty()
        }
        if (composeRule.onAllNodesWithTag(TAB_GRID_TEST_TAG).fetchSemanticsNodes().isEmpty()) {
            composeRule.onNodeWithContentDescription(tabs).performClick()
        }
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodes(tabCard).fetchSemanticsNodes().isNotEmpty()
        }

        while (composeRule.onAllNodes(tabCard).fetchSemanticsNodes().size > 1) {
            composeRule.onAllNodesWithContentDescription(closeTab)[0].performClick()
        }

        val previousTag = composeRule.onAllNodes(tabCard).fetchSemanticsNodes().single()
            .config[SemanticsProperties.TestTag]
        composeRule.onNodeWithContentDescription(closeTab).performClick()
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodes(tabCard).fetchSemanticsNodes().singleOrNull()
                ?.config?.get(SemanticsProperties.TestTag) != previousTag
        }
        composeRule.onNode(tabCard).performClick()
        composeRule.onNodeWithTag(NEW_TAB_SCREEN_TEST_TAG).assertIsDisplayed()
    }

    @Test
    fun newTabChromeIsVisible() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val greeting = composeRule.activity.getString(R.string.new_tab_question)
        composeRule.onNodeWithText(addressHint).assertIsDisplayed()
        composeRule.onNodeWithText(greeting).assertIsDisplayed()
        composeRule.onNodeWithTag(NEW_TAB_SCREEN_TEST_TAG).assertIsDisplayed()
    }

    @Test
    fun homepageSettingsButtonOpensSettingsAndReturnsHome() {
        val settings = composeRule.activity.getString(R.string.settings)

        composeRule.onNodeWithContentDescription(settings).assertIsDisplayed().performClick()
        composeRule.onNodeWithText(settings).assertIsDisplayed()
        composeRule.onNodeWithContentDescription(
            composeRule.activity.getString(R.string.navigate_back),
        ).performClick()

        composeRule.onNodeWithTag(NEW_TAB_SCREEN_TEST_TAG).assertIsDisplayed()
        composeRule.onNodeWithContentDescription(settings).assertIsDisplayed()
    }

    @Test
    fun homepageSettingsButtonHidesWhileSearchIsExpanded() {
        val settings = composeRule.activity.getString(R.string.settings)
        val addressHint = composeRule.activity.getString(R.string.address_hint)

        composeRule.onNodeWithContentDescription(settings).assertIsDisplayed()
        composeRule.onNodeWithText(addressHint).performClick()

        composeRule.onAllNodesWithContentDescription(settings).assertCountEquals(0)
    }

    @Test
    fun expandedSearchHidesTheTabSwitcher() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val tabs = composeRule.activity.getString(R.string.tab_switcher)

        composeRule.onNodeWithContentDescription(tabs).assertIsDisplayed()
        composeRule.onNodeWithText(addressHint).performClick()

        composeRule.onAllNodesWithContentDescription(tabs).assertCountEquals(0)
    }

    @Test
    fun collapsedSearchBecomesFocusedInputAfterActivation() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)

        composeRule.onAllNodes(hasSetTextAction()).assertCountEquals(0)
        composeRule.onNodeWithText(addressHint).performClick()

        composeRule.onAllNodes(hasSetTextAction()).assertCountEquals(1)
        composeRule.onNode(hasSetTextAction()).assertIsFocused()
    }

    @Test
    fun expandedSearchUsesOnePersistentClearAction() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val clearInput = composeRule.activity.getString(R.string.clear_input)
        val exitSearch = composeRule.activity.getString(R.string.exit_search)

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput("dao")

        composeRule.onAllNodesWithContentDescription(clearInput).assertCountEquals(1)
        composeRule.onAllNodesWithContentDescription(exitSearch).assertCountEquals(0)
        composeRule.onNodeWithContentDescription(clearInput).performClick()

        val input = composeRule.onNode(hasSetTextAction()).assertIsFocused()
        assertEquals("", input.fetchSemanticsNode().config[SemanticsProperties.EditableText].text)
        composeRule.onNodeWithTag(NEW_TAB_SCREEN_TEST_TAG).assertIsDisplayed()
    }

    @Test
    fun emptyExpandedSearchClearActionReturnsToTheHomepage() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val clearInput = composeRule.activity.getString(R.string.clear_input)
        val tabs = composeRule.activity.getString(R.string.tab_switcher)

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).assertIsFocused()
        composeRule.onNodeWithContentDescription(clearInput).performClick()

        composeRule.onAllNodes(hasSetTextAction()).assertCountEquals(0)
        composeRule.onNodeWithText(addressHint).assertIsDisplayed()
        composeRule.onNodeWithContentDescription(tabs).assertIsDisplayed()
    }

    @Test
    fun settingsOmitsUnavailableDownloadLocationAndStartupOptions() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val settings = composeRule.activity.getString(R.string.settings)
        val downloadLocation = "\u4e0b\u8f7d\u4f4d\u7f6e"
        val onStartup = "\u542f\u52a8\u65f6"

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput("about:blank")
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()
        composeRule.onNodeWithText(settings).performClick()

        composeRule.onAllNodesWithText(downloadLocation).assertCountEquals(0)
        composeRule.onAllNodesWithText(onStartup).assertCountEquals(0)
    }

    @Test
    fun settingsShowsUsbRemoteDebuggingAndConnectionGuide() {
        openSettings()

        composeRule.onNodeWithText("\u5f00\u53d1\u8005\u5de5\u5177").assertIsDisplayed()
        composeRule.onNodeWithText("USB \u8fdc\u7a0b\u8c03\u8bd5").assertIsDisplayed()
        composeRule.onNodeWithText("\u8fde\u63a5\u6307\u5357").assertIsDisplayed()
    }

    @Test
    fun settingsAboutFlowShowsVersionsLicensesAndBackNavigation() {
        val settings = composeRule.activity.getString(R.string.settings)
        val menu = composeRule.activity.getString(R.string.menu)

        composeRule.onNodeWithContentDescription(settings).performClick()
        assertAboutVersionIsTrailing()
        openAbout()
        composeRule.onNodeWithTag("about-screen").assertIsDisplayed()
        val expectedInfo = readAboutAppInfo(composeRule.activity)
        val expectedEngine = composeRule.activity.getString(
            R.string.gecko_engine_version,
            expectedInfo.engineVersion,
        )
        composeRule.onNodeWithText(expectedInfo.appVersion).assertIsDisplayed()
        composeRule.onNodeWithText(expectedEngine).assertIsDisplayed()
        composeRule.onNodeWithTag("open-source-licenses-entry", useUnmergedTree = true)
            .performClick()
        composeRule.onNodeWithTag("open-source-licenses-screen").assertIsDisplayed()
        composeRule.onNodeWithTag("bundled-license-u_block_origin", useUnmergedTree = true)
            .assertIsDisplayed()
        pressSystemBack()
        composeRule.onNodeWithTag("about-screen").assertIsDisplayed()
        pressSystemBack()
        composeRule.onNodeWithText(settings).assertIsDisplayed()
        pressSystemBack()
        composeRule.onNodeWithTag(NEW_TAB_SCREEN_TEST_TAG).assertIsDisplayed()

        openSettings()
        openAbout()
        composeRule.onNodeWithTag("open-source-licenses-entry", useUnmergedTree = true)
            .performClick()
        pressSystemBack()
        composeRule.onNodeWithTag("about-screen").assertIsDisplayed()
        pressSystemBack()
        composeRule.onNodeWithText(settings).assertIsDisplayed()
        pressSystemBack()
        composeRule.onNodeWithContentDescription(menu).assertIsDisplayed()
    }

    @Test
    fun settingsOffersAllSupportedSearchEngines() {
        openSettings()

        composeRule.onNodeWithText(
            composeRule.activity.getString(R.string.default_search_engine),
        ).performClick()

        composeRule.onAllNodesWithText(composeRule.activity.getString(R.string.google))[0]
            .assertIsDisplayed()
        composeRule.onNodeWithText(composeRule.activity.getString(R.string.baidu))
            .assertIsDisplayed()
        composeRule.onNodeWithText(composeRule.activity.getString(R.string.bing))
            .assertIsDisplayed()
        composeRule.onNodeWithText(composeRule.activity.getString(R.string.duckduckgo))
            .assertIsDisplayed()
    }

    @Test
    fun settingsShowsLiveFontSizePreview() {
        composeRule.onNodeWithContentDescription(
            composeRule.activity.getString(R.string.settings),
        ).performClick()

        composeRule.onNodeWithText(composeRule.activity.getString(R.string.font_preview_title))
            .assertIsDisplayed()
        composeRule.onNodeWithText(composeRule.activity.getString(R.string.font_preview_body))
            .assertIsDisplayed()

        composeRule.onNodeWithText(composeRule.activity.getString(R.string.large)).performClick()

        val preferences = (composeRule.activity.application as DaoApplication).browserPreferences
        composeRule.waitUntil(timeoutMillis = 5_000) {
            runBlocking { preferences.state.first().fontScale == BrowserFontScale.LARGE }
        }
    }

    @Test
    fun firstUsbRemoteDebuggingEnableCanBeCancelled() {
        val warningTitle = "\u5f00\u542f USB \u8fdc\u7a0b\u8c03\u8bd5\uff1f"
        openSettings()

        composeRule.onNodeWithTag("usb-remote-debugging-switch").performClick()
        composeRule.onNodeWithText(warningTitle).assertIsDisplayed()
        composeRule.onNodeWithText(composeRule.activity.getString(R.string.cancel)).performClick()

        composeRule.onAllNodesWithText(warningTitle).assertCountEquals(0)
        val preferences = (composeRule.activity.application as DaoApplication).browserPreferences
        assertFalse(runBlocking { preferences.state.first().remoteDebuggingEnabled })
    }

    @Test
    fun usbRemoteDebuggingGuideShowsConnectionStepsAndActions() {
        openSettings()

        scrollSettingsTo("settings-connection-guide-entry")
        composeRule.onNodeWithTag("settings-connection-guide-entry", useUnmergedTree = true)
            .performClick()

        composeRule.onNodeWithText("1. \u6253\u5f00 Android \u5f00\u53d1\u8005\u9009\u9879\u548c USB \u8c03\u8bd5").assertIsDisplayed()
        composeRule.onNodeWithText("2. \u8fde\u63a5\u7535\u8111\u5e76\u6388\u6743 ADB \u8c03\u8bd5").assertIsDisplayed()
        composeRule.onNodeWithText("3. \u5728\u684c\u9762 Firefox \u4e2d\u6253\u5f00 about:debugging").assertIsDisplayed()
        composeRule.onNodeWithText("\u6253\u5f00\u5f00\u53d1\u8005\u9009\u9879").assertIsDisplayed()
        composeRule.onNodeWithText("\u590d\u5236 about:debugging").performClick()

        val clipboard = composeRule.activity.getSystemService(ClipboardManager::class.java)
        assertEquals("about:debugging", clipboard.primaryClip?.getItemAt(0)?.text?.toString())
    }

    @Test
    fun tabGridCreatesAndClosesRealBrowserStoreTabs() {
        val tabs = composeRule.activity.getString(R.string.tab_switcher)
        val newTab = composeRule.activity.getString(R.string.new_tab)
        val closeTab = composeRule.activity.getString(R.string.close_tab)

        composeRule.onNodeWithContentDescription(tabs).performClick()
        composeRule.onNodeWithTag(TAB_GRID_TEST_TAG).assertIsDisplayed()
        composeRule.onNodeWithContentDescription(newTab).performClick()
        composeRule.onNodeWithContentDescription(tabs).assertTextEquals("2").performClick()
        composeRule.onAllNodesWithContentDescription(closeTab)[0].performClick()
        composeRule.onNodeWithText(composeRule.activity.getString(R.string.tabs_title, 1))
            .assertIsDisplayed()
    }

    @Test
    fun openingTabGridFromNewTabShowsItsCapturedThumbnail() {
        val tabs = composeRule.activity.getString(R.string.tab_switcher)
        val tabThumbnail = SemanticsMatcher("Captured tab thumbnail") { node ->
            node.config.contains(SemanticsProperties.TestTag) &&
                node.config[SemanticsProperties.TestTag].startsWith(TAB_THUMBNAIL_TEST_TAG_PREFIX)
        }

        composeRule.onNodeWithContentDescription(tabs).performClick()

        composeRule.onNodeWithTag(TAB_GRID_TEST_TAG).assertIsDisplayed()
        composeRule.onNode(tabThumbnail).assertIsDisplayed()
    }

    private fun openSettings() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val settings = composeRule.activity.getString(R.string.settings)

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput("about:blank")
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()
        composeRule.onNodeWithText(settings).performClick()
    }

    private fun openAbout() {
        scrollSettingsTo("settings-about-entry")
        composeRule.onNodeWithTag("settings-about-entry", useUnmergedTree = true)
            .performClick()
    }

    private fun assertAboutVersionIsTrailing() {
        scrollSettingsTo("settings-about-entry")
        val aboutEntry = composeRule.onNodeWithTag("settings-about-entry", useUnmergedTree = true)
        val version = composeRule.onNodeWithTag("settings-about-version", useUnmergedTree = true)
        version.assertIsDisplayed()
        assertTrue(
            version.fetchSemanticsNode().boundsInRoot.center.x >
                aboutEntry.fetchSemanticsNode().boundsInRoot.center.x,
        )
    }

    private fun pressSystemBack() {
        composeRule.runOnIdle {
            composeRule.activity.onBackPressedDispatcher.onBackPressed()
        }
    }

    private fun scrollSettingsTo(testTag: String) {
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodesWithTag("settings-list", useUnmergedTree = true)
                .fetchSemanticsNodes().isNotEmpty()
        }
        composeRule.onNodeWithTag("settings-list", useUnmergedTree = true)
            .performScrollToNode(hasTestTag(testTag))
    }

    @Test
    fun tabGridSwitchesAndSwipeClosesBrowserStoreTabs() {
        val tabs = composeRule.activity.getString(R.string.tab_switcher)
        val newTab = composeRule.activity.getString(R.string.new_tab)
        val tabCard = SemanticsMatcher("BrowserStore tab card") { node ->
            node.config.contains(SemanticsProperties.TestTag) &&
                node.config[SemanticsProperties.TestTag].startsWith(TAB_CARD_TEST_TAG_PREFIX)
        }

        composeRule.onNodeWithContentDescription(tabs).performClick()
        composeRule.onNodeWithContentDescription(newTab).performClick()
        composeRule.onNodeWithContentDescription(tabs).performClick()
        composeRule.onAllNodes(tabCard).assertCountEquals(2)[0].performClick()
        composeRule.onNodeWithContentDescription(tabs).performClick()
        composeRule.onAllNodes(tabCard)[0].assertIsSelected()

        composeRule.onAllNodes(tabCard)[0].performTouchInput { swipeRight() }

        composeRule.onAllNodes(tabCard).assertCountEquals(1)
    }

    @Test
    fun desktopBrandLogoIsVisible() {
        composeRule.onNodeWithContentDescription(
            composeRule.activity.getString(R.string.dao_logo_content_description),
        ).assertIsDisplayed()
    }

    @Test
    fun adaptiveLauncherLogoKeepsOriginalScaleAt48DpLauncherSize() {
        val applicationInfo = composeRule.activity.applicationInfo
        val icon = composeRule.activity.packageManager.getApplicationIcon(applicationInfo)
        assertTrue("Launcher icon must be adaptive on API 26+", icon is AdaptiveIconDrawable)
        icon as AdaptiveIconDrawable

        val density = composeRule.activity.resources.displayMetrics.density
        val canvasSize = (48 * density).roundToInt()
        val bitmap = Bitmap.createBitmap(canvasSize, canvasSize, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        icon.setBounds(0, 0, canvasSize, canvasSize)
        icon.foreground.draw(canvas)

        var darkPixelCount = 0
        var minX = canvasSize
        var maxX = -1
        for (y in 0 until canvasSize) {
            for (x in 0 until canvasSize) {
                val pixel = bitmap.getPixel(x, y)
                if (
                    android.graphics.Color.alpha(pixel) > 8 &&
                    android.graphics.Color.red(pixel) + android.graphics.Color.green(pixel) +
                    android.graphics.Color.blue(pixel) < 384
                ) {
                    darkPixelCount++
                    minX = minOf(minX, x)
                    maxX = maxOf(maxX, x)
                }
            }
        }

        assertTrue(
            "Launcher foreground must remain visible at 48dp",
            darkPixelCount >= canvasSize * canvasSize / 100,
        )
        val visibleWidthDp = (maxX - minX + 1) / density
        assertTrue(
            "Launcher foreground is too small at 48dp: ${visibleWidthDp}dp",
            visibleWidthDp >= 27f,
        )
    }

    @Test
    fun adaptiveLauncherLogoStaysInsideTheOemSafeZone() {
        val applicationInfo = composeRule.activity.applicationInfo
        val icon = composeRule.activity.packageManager.getApplicationIcon(applicationInfo)
        assertTrue("Launcher icon must be adaptive on API 26+", icon is AdaptiveIconDrawable)
        icon as AdaptiveIconDrawable

        val density = composeRule.activity.resources.displayMetrics.density
        val canvasSize = (108 * density).roundToInt()
        val bitmap = Bitmap.createBitmap(canvasSize, canvasSize, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        icon.setBounds(0, 0, canvasSize, canvasSize)
        icon.foreground.draw(canvas)

        var minX = canvasSize
        var minY = canvasSize
        var maxX = -1
        var maxY = -1
        for (y in 0 until canvasSize) {
            for (x in 0 until canvasSize) {
                if (android.graphics.Color.alpha(bitmap.getPixel(x, y)) > 8) {
                    minX = minOf(minX, x)
                    minY = minOf(minY, y)
                    maxX = maxOf(maxX, x)
                    maxY = maxOf(maxY, y)
                }
            }
        }

        assertTrue("Launcher foreground must contain visible logo pixels", maxX >= minX && maxY >= minY)
        val visibleWidthDp = (maxX - minX + 1) / density
        val visibleHeightDp = (maxY - minY + 1) / density
        assertTrue(
            "Launcher logo exceeds the 66dp OEM-safe box: ${visibleWidthDp}x${visibleHeightDp}dp",
            max(visibleWidthDp, visibleHeightDp) <= 66f,
        )
    }

    @Test
    fun contentStaysInsideSafeDrawingBounds() {
        val contentView = composeRule.activity.findViewById<View>(android.R.id.content)
        val safeInsets = requireNotNull(ViewCompat.getRootWindowInsets(contentView)).getInsets(
            WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout(),
        )
        val controls = composeRule.onNodeWithTag(NEW_TAB_SCREEN_TEST_TAG)
            .fetchSemanticsNode()
            .boundsInRoot

        assertTrue(controls.left >= safeInsets.left)
        assertTrue(controls.top >= safeInsets.top)
        assertTrue(controls.right <= contentView.width - safeInsets.right)
        assertTrue(controls.bottom <= contentView.height - safeInsets.bottom)
    }

    @Test
    fun scannerCanBeOpenedAndClosed() {
        val scan = composeRule.activity.getString(R.string.scan_qr)
        val close = composeRule.activity.getString(R.string.close_scanner)

        composeRule.onNodeWithContentDescription(scan).performClick()
        composeRule.onNodeWithContentDescription(close).assertIsDisplayed().performClick()
        composeRule.onNodeWithContentDescription(scan).assertIsDisplayed()
    }

    @Test
    fun scannerBlocksTouchesFromReachingTheTabSwitcher() {
        val scan = composeRule.activity.getString(R.string.scan_qr)
        val close = composeRule.activity.getString(R.string.close_scanner)
        val tabs = composeRule.activity.getString(R.string.tab_switcher)
        val tabSwitcherCenter = composeRule.onNodeWithContentDescription(tabs)
            .fetchSemanticsNode()
            .boundsInRoot
            .center

        composeRule.onNodeWithContentDescription(scan).performClick()
        composeRule.onRoot().performTouchInput { click(tabSwitcherCenter) }

        composeRule.onNodeWithContentDescription(close).assertIsDisplayed()
    }

    @Test
    fun drawerHomeReusesTheCurrentTab() {
        val visitedUrl = "about:blank#drawer-home"
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val home = composeRule.activity.getString(R.string.home)
        val scan = composeRule.activity.getString(R.string.scan_qr)
        val tabs = composeRule.activity.getString(R.string.tab_switcher)

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput(visitedUrl)
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()

        composeRule.onNodeWithText(home).assertIsDisplayed()
        composeRule.onNodeWithText(scan).assertIsDisplayed()
        composeRule.onNodeWithText(home).performClick()

        composeRule.onNodeWithTag(NEW_TAB_SCREEN_TEST_TAG).assertIsDisplayed()
        composeRule.onNodeWithContentDescription(tabs).assertTextEquals("1")

        composeRule.runOnIdle {
            composeRule.activity.onBackPressedDispatcher.onBackPressed()
        }
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodesWithText(visitedUrl, substring = true)
                .fetchSemanticsNodes().isNotEmpty()
        }
    }

    @Test
    fun drawerScanOpensTheExistingScanner() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val scan = composeRule.activity.getString(R.string.scan_qr)
        val close = composeRule.activity.getString(R.string.close_scanner)

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput("about:blank#drawer-scan")
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()
        composeRule.onNodeWithText(scan).performClick()

        composeRule.onNodeWithContentDescription(close).assertIsDisplayed().performClick()
        composeRule.onNodeWithContentDescription(menu).assertIsDisplayed()
    }

    @Test
    fun builtInAdBlockerIsInstalledAndEnabled() {
        val application = composeRule.activity.application as DaoApplication
        val engine = application.browserRuntime.engine
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20)

        while (System.nanoTime() < deadline) {
            val callback = CountDownLatch(1)
            var extensions: List<WebExtension> = emptyList()
            var failure: Throwable? = null
            composeRule.activity.runOnUiThread {
                engine.listInstalledWebExtensions(
                    onSuccess = {
                        extensions = it
                        callback.countDown()
                    },
                    onError = {
                        failure = it
                        callback.countDown()
                    },
                )
            }
            assertTrue("Extension query timed out", callback.await(5, TimeUnit.SECONDS))
            failure?.let { throw AssertionError("Extension query failed", it) }

            extensions.firstOrNull { it.id == "uBlock0@raymondhill.net" }?.let {
                assertTrue("uBlock Origin must be a built-in extension", it.isBuiltIn())
                assertTrue("uBlock Origin must be enabled", it.isEnabled())
                return
            }
            Thread.sleep(100)
        }

        fail("uBlock Origin was not installed within 20 seconds")
    }

    @Test
    fun currentPageCanBeBookmarkedAndOpenedFromTheRealBookmarkList() {
        val url = "https://bookmark-${System.nanoTime()}.example"
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val addBookmark = composeRule.activity.getString(R.string.add_bookmark)
        val bookmarks = composeRule.activity.getString(R.string.bookmarks)

        composeRule.onNodeWithText(addressHint).performClick()
        val addressField = composeRule.onNode(hasSetTextAction())
        addressField.performTextInput(url)
        addressField.performImeAction()
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodesWithText(Uri.parse(url).host.orEmpty(), substring = true)
                .fetchSemanticsNodes().isNotEmpty()
        }
        composeRule.onNodeWithContentDescription(menu).performClick()
        composeRule.onNodeWithText(addBookmark).performClick()
        val application = composeRule.activity.application as DaoApplication
        composeRule.waitUntil(timeoutMillis = 10_000) {
            application.browserLibrary.isBookmarked(url)
        }
        composeRule.onNodeWithText(bookmarks).performClick()

        composeRule.onNodeWithText(url).assertIsDisplayed()
    }

    @Test
    fun drawerDarkModeActionDirectlyTogglesThePersistedTheme() {
        val preferences = (composeRule.activity.application as DaoApplication).browserPreferences
        runBlocking { preferences.setDarkTheme(false) }
        composeRule.waitUntil(timeoutMillis = 5_000) {
            !runBlocking { preferences.state.first().darkTheme }
        }

        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val darkMode = composeRule.activity.getString(R.string.dark_mode)
        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput("about:blank")
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()

        composeRule.onNodeWithText(darkMode).performClick()
        composeRule.waitUntil(timeoutMillis = 5_000) {
            runBlocking { preferences.state.first().darkTheme }
        }

        composeRule.onNodeWithText(darkMode).performClick()
        composeRule.waitUntil(timeoutMillis = 5_000) {
            !runBlocking { preferences.state.first().darkTheme }
        }
    }

    @Test
    fun historyScreenDisplaysPersistedVisitsInsteadOfRepresentativeRows() {
        val application = composeRule.activity.application as DaoApplication
        val url = "https://history-${System.nanoTime()}.example"
        runBlocking {
            application.browserLibrary.recordVisit(url, "Persisted visit", System.currentTimeMillis())
        }
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val history = composeRule.activity.getString(R.string.history)

        composeRule.onNodeWithText(addressHint).performClick()
        val addressField = composeRule.onNode(hasSetTextAction())
        addressField.performTextInput("about:blank")
        addressField.performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()
        composeRule.onNodeWithText(history).performClick()

        composeRule.onNodeWithText("Persisted visit").assertIsDisplayed()
        composeRule.onNodeWithText(url).assertIsDisplayed()
    }

    @Test
    fun bookmarkMenuCanMoveARealItemToTheReadingList() {
        val url = "https://reading-${System.nanoTime()}.example"
        val application = composeRule.activity.application as DaoApplication
        val bookmark = runBlocking { application.browserLibrary.addBookmark(url, "Reading candidate") }
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val bookmarks = composeRule.activity.getString(R.string.bookmarks)
        val move = composeRule.activity.getString(R.string.move_to_reading_list)
        val readingList = composeRule.activity.getString(R.string.reading_list)

        composeRule.onNodeWithText(addressHint).performClick()
        val addressField = composeRule.onNode(hasSetTextAction())
        addressField.performTextInput("about:blank")
        addressField.performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()
        composeRule.onNodeWithText(bookmarks).performClick()
        composeRule.onNodeWithTag("$BOOKMARK_OPTIONS_TEST_TAG_PREFIX${bookmark.id}").performClick()
        composeRule.onNodeWithText(move).performClick()
        composeRule.onNodeWithText(readingList).performClick()

        composeRule.onNodeWithText(url).assertIsDisplayed()
    }

    @Test
    fun findTileOpensAndClosesARealFindInPageBar() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val find = composeRule.activity.getString(R.string.find_in_page)
        val findHint = composeRule.activity.getString(R.string.find_in_page_hint)
        val closeFind = composeRule.activity.getString(R.string.close_find)

        composeRule.onNodeWithText(addressHint).performClick()
        val addressField = composeRule.onNode(hasSetTextAction())
        addressField.performTextInput("about:blank")
        addressField.performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()
        composeRule.onNodeWithText(find).performClick()

        composeRule.onNodeWithText(findHint).assertIsDisplayed()
        composeRule.onNode(hasSetTextAction()).performTextInput("dao")
        composeRule.onNodeWithContentDescription(closeFind).performClick()
        composeRule.onNodeWithContentDescription(menu).assertIsDisplayed()
    }

    @Test
    fun unavailableNavigationActionsExposeTheirDisabledState() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val back = composeRule.activity.getString(R.string.back)
        val forward = composeRule.activity.getString(R.string.forward)
        val reload = composeRule.activity.getString(R.string.reload)

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput("about:blank#navigation-state")
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()

        composeRule.onNodeWithContentDescription(back).assertIsNotEnabled()
        composeRule.onNodeWithContentDescription(forward).assertIsNotEnabled()
        composeRule.onNodeWithContentDescription(reload).assertIsEnabled()
    }

    @Test
    fun reloadFromDrawerClosesTheDrawer() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val reload = composeRule.activity.getString(R.string.reload)

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput("about:blank#reload-closes-drawer")
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()
        composeRule.onNodeWithContentDescription(reload).performClick()

        composeRule.waitForIdle()
        composeRule.onNodeWithContentDescription(reload).assertIsNotDisplayed()
        composeRule.onNodeWithContentDescription(menu).assertIsDisplayed()
    }

    @Test
    fun rightSwipeFromLeftEdgeNavigatesBack() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val firstUrl = "about:blank#edge-back-first"
        val secondUrl = "about:blank#edge-back-second"

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput(firstUrl)
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithText(firstUrl).performClick()
        composeRule.onNode(hasSetTextAction()).performTextReplacement(secondUrl)
        composeRule.onNode(hasSetTextAction()).performImeAction()

        composeRule.waitUntil(timeoutMillis = 5_000) {
            composeRule.onAllNodesWithTag(EDGE_BACK_GESTURE_TEST_TAG).fetchSemanticsNodes().isNotEmpty()
        }
        composeRule.onNodeWithTag(EDGE_BACK_GESTURE_TEST_TAG).performTouchInput {
            swipeRight(startX = 1f, endX = 300f, durationMillis = 250)
        }

        composeRule.onNodeWithText(firstUrl).assertIsDisplayed()
    }

    @Test
    fun drawerNavigationRippleStaysInsideRoundedButton() {
        val addressHint = composeRule.activity.getString(R.string.address_hint)
        val menu = composeRule.activity.getString(R.string.menu)
        val reload = composeRule.activity.getString(R.string.reload)

        composeRule.onNodeWithText(addressHint).performClick()
        composeRule.onNode(hasSetTextAction()).performTextInput("about:blank#ripple-clip")
        composeRule.onNode(hasSetTextAction()).performImeAction()
        composeRule.onNodeWithContentDescription(menu).performClick()

        val reloadButton = composeRule.onNodeWithContentDescription(reload)
        val bounds = reloadButton.fetchSemanticsNode().boundsInRoot
        val idlePixels = composeRule.onRoot().captureToImage().toPixelMap()

        reloadButton.performTouchInput {
            down(center)
            advanceEventTime(300)
        }
        composeRule.mainClock.advanceTimeBy(300)
        composeRule.waitForIdle()
        val pressedPixels = composeRule.onRoot().captureToImage().toPixelMap()

        val cornerInset = 2
        val corners = listOf(
            bounds.left.toInt() + cornerInset to bounds.top.toInt() + cornerInset,
            bounds.right.toInt() - cornerInset - 1 to bounds.top.toInt() + cornerInset,
            bounds.left.toInt() + cornerInset to bounds.bottom.toInt() - cornerInset - 1,
            bounds.right.toInt() - cornerInset - 1 to bounds.bottom.toInt() - cornerInset - 1,
        )
        corners.forEach { (x, y) ->
            assertEquals("Ripple escaped the rounded button at ($x, $y)", idlePixels[x, y], pressedPixels[x, y])
        }

        reloadButton.performTouchInput { up() }
    }

    @Test
    fun tappingTheAddressDisplayOpensTheRealUrlForEditing() {
        val url = "about:blank#edit-${System.nanoTime()}"
        val host = url
        val addressHint = composeRule.activity.getString(R.string.address_hint)

        composeRule.onNodeWithText(addressHint).performClick()
        val addressField = composeRule.onNode(hasSetTextAction())
        addressField.performTextInput(url)
        addressField.performImeAction()
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodesWithText(host, substring = true).fetchSemanticsNodes().isNotEmpty()
        }
        composeRule.onNodeWithText(host, substring = true).performClick()

        composeRule.onNode(hasSetTextAction()).assertTextEquals(url)
    }

}
