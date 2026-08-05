package com.msgbyte.dao.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.toPixelMap
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotSelected
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.msgbyte.dao.browser.SiteSecurityDetails
import com.msgbyte.dao.browser.SiteSecurityState
import com.msgbyte.dao.ui.theme.DaoTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class BrowserChromeTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun loadingProgressChangesOnlyTheAddressBackground() {
        val loading = mutableStateOf(false)

        composeRule.setContent {
            DaoTheme {
                Column(Modifier.fillMaxSize()) {
                    AddressBar(
                        url = "about:blank",
                        isSecure = false,
                        tabCount = 3,
                        loading = loading.value,
                        progress = 50,
                        onSecurityClick = {},
                        onAddressClick = {},
                        onTabs = {},
                        onMenu = {},
                    )
                    Box(Modifier.fillMaxSize().testTag(CONTENT_TEST_TAG))
                }
            }
        }
        composeRule.waitForIdle()

        val idleAddressBounds = composeRule.onNodeWithTag(BROWSER_CONTROLS_TEST_TAG)
            .fetchSemanticsNode().boundsInRoot
        val idleContentBounds = composeRule.onNodeWithTag(CONTENT_TEST_TAG)
            .fetchSemanticsNode().boundsInRoot
        val idleAddress = composeRule.onNodeWithTag(BROWSER_CONTROLS_TEST_TAG).captureToImage()

        composeRule.runOnIdle { loading.value = true }
        composeRule.mainClock.advanceTimeBy(250)
        composeRule.waitForIdle()

        val loadingAddressBounds = composeRule.onNodeWithTag(BROWSER_CONTROLS_TEST_TAG)
            .fetchSemanticsNode().boundsInRoot
        val loadingContentBounds = composeRule.onNodeWithTag(CONTENT_TEST_TAG)
            .fetchSemanticsNode().boundsInRoot
        val loadingAddress = composeRule.onNodeWithTag(BROWSER_CONTROLS_TEST_TAG).captureToImage()

        assertEquals(idleAddressBounds, loadingAddressBounds)
        assertEquals(idleContentBounds, loadingContentBounds)
        assertTrue("Loading must paint inside the address bar", imagesDiffer(idleAddress, loadingAddress))
    }

    @Test
    fun securityIconUsesAnIndependentAddressBarAction() {
        var securityClicks = 0
        var addressClicks = 0
        composeRule.setContent {
            DaoTheme {
                AddressBar(
                    url = "https://example.com/path",
                    isSecure = true,
                    tabCount = 1,
                    loading = false,
                    progress = 0,
                    onSecurityClick = { securityClicks++ },
                    onAddressClick = { addressClicks++ },
                    onTabs = {},
                    onMenu = {},
                )
            }
        }

        composeRule.onNodeWithContentDescription("\u5b89\u5168\u8fde\u63a5").performClick()

        composeRule.runOnIdle {
            assertEquals(1, securityClicks)
            assertEquals(0, addressClicks)
        }
    }

    @Test
    fun securityRippleCarrierStaysInsideTheAddressBar() {
        composeRule.setContent {
            DaoTheme {
                AddressBar(
                    url = "https://example.com",
                    isSecure = true,
                    tabCount = 1,
                    loading = false,
                    progress = 0,
                    onSecurityClick = {},
                    onAddressClick = {},
                    onTabs = {},
                    onMenu = {},
                )
            }
        }

        val addressBounds = composeRule.onNodeWithTag(BROWSER_CONTROLS_TEST_TAG)
            .fetchSemanticsNode().boundsInRoot
        val rippleBounds = composeRule.onNodeWithTag(SECURITY_RIPPLE_TEST_TAG, useUnmergedTree = true)
            .fetchSemanticsNode().boundsInRoot

        assertEquals(rippleBounds.width, rippleBounds.height, 0.5f)
        assertTrue(rippleBounds.height < addressBounds.height * 0.6f)
    }

    @Test
    fun secureBottomSheetDisplaysRealCertificateRows() {
        val details = SiteSecurityDetails(
            host = "www.example.com",
            state = SiteSecurityState.SECURE,
            subject = "example.com",
            issuer = "Example CA",
            validFrom = "Jan 1, 2026, 12:00:00 AM",
            validUntil = "Jan 1, 2027, 12:00:00 AM",
            serialNumber = "FF",
            sha256Fingerprint = "03:90:58:C6",
        )
        composeRule.setContent {
            DaoTheme {
                SiteSecurityBottomSheet(details = details, onDismiss = {})
            }
        }

        composeRule.onNodeWithText("www.example.com", useUnmergedTree = true).assertIsDisplayed()
        composeRule.onNodeWithText("\u8fde\u63a5\u5b89\u5168").assertIsDisplayed()
        composeRule.onNodeWithText("Example CA").assertIsDisplayed()
        composeRule.onNodeWithText("Jan 1, 2027, 12:00:00 AM").fetchSemanticsNode()
        composeRule.onNodeWithText("FF").fetchSemanticsNode()
        composeRule.onNodeWithText("03:90:58:C6").fetchSemanticsNode()
    }

    @Test
    fun bookmarkTileChangesFromOutlinedToFilledSelectedState() {
        val bookmarked = mutableStateOf(false)
        composeRule.setContent {
            DaoTheme {
                DrawerBookmarkTile(
                    isBookmarked = bookmarked.value,
                    onToggleBookmark = { bookmarked.value = !bookmarked.value },
                )
            }
        }

        composeRule.onNodeWithTag(BOOKMARK_DRAWER_TILE_TEST_TAG).assertIsNotSelected()
        val outlined = composeRule.onNodeWithTag(
            BOOKMARK_DRAWER_ICON_TEST_TAG,
            useUnmergedTree = true,
        ).captureToImage()

        composeRule.onNodeWithTag(BOOKMARK_DRAWER_TILE_TEST_TAG).performClick()

        composeRule.onNodeWithTag(BOOKMARK_DRAWER_TILE_TEST_TAG).assertIsSelected()
        val filled = composeRule.onNodeWithTag(
            BOOKMARK_DRAWER_ICON_TEST_TAG,
            useUnmergedTree = true,
        ).captureToImage()
        assertTrue(
            "Bookmark icon must visibly change when selected",
            imagesDiffer(outlined, filled),
        )
    }

    private fun imagesDiffer(first: ImageBitmap, second: ImageBitmap): Boolean {
        if (first.width != second.width || first.height != second.height) return true
        val firstPixels = first.toPixelMap()
        val secondPixels = second.toPixelMap()
        return (0 until first.height).any { y ->
            (0 until first.width).any { x -> firstPixels[x, y] != secondPixels[x, y] }
        }
    }

    private companion object {
        const val CONTENT_TEST_TAG = "browser-chrome-test-content"
    }
}
