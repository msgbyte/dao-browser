package com.msgbyte.dao.ui

import android.content.res.Configuration
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.graphics.toPixelMap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.espresso.Espresso.pressBack
import androidx.test.platform.app.InstrumentationRegistry
import com.msgbyte.dao.R
import com.msgbyte.dao.about.AboutAppInfo
import com.msgbyte.dao.ui.theme.DaoTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AboutScreensTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun aboutScreenShowsIdentityAndVersions() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val engineDisplay = context.getString(R.string.gecko_engine_version, "153.0.2")
        composeRule.setContent {
            DaoTheme {
                AboutScreen(
                    appInfo = AboutAppInfo("0.1.0", "153.0.2"),
                    onOpenLicenses = {},
                    onBack = {},
                )
            }
        }

        composeRule.onNodeWithTag("about-screen").assertIsDisplayed()
        composeRule.onNodeWithText("Dao Browser").assertIsDisplayed()
        composeRule.onNodeWithText("0.1.0").assertIsDisplayed()
        composeRule.onNodeWithText(engineDisplay).assertIsDisplayed()
    }

    @Test
    fun aboutScreenUsesLightLogoInDarkTheme() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val nightConfiguration = Configuration(context.resources.configuration).apply {
            uiMode = (uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or
                Configuration.UI_MODE_NIGHT_YES
        }
        val nightContext = context.createConfigurationContext(nightConfiguration)

        composeRule.setContent {
            CompositionLocalProvider(LocalContext provides nightContext) {
                DaoTheme(darkTheme = true) {
                    AboutScreen(
                        appInfo = AboutAppInfo("0.1.0", "153.0.2"),
                        onOpenLicenses = {},
                        onBack = {},
                    )
                }
            }
        }

        val pixels = composeRule.onNodeWithTag(DAO_BRAND_LOGO_TEST_TAG)
            .captureToImage()
            .toPixelMap()
        val containsLightLogoPixel = (0 until pixels.height).any { y ->
            (0 until pixels.width).any { x -> pixels[x, y].luminance() > 0.8f }
        }

        assertTrue(containsLightLogoPixel)
    }

    @Test
    fun aboutScreenOpensLicensesOnce() {
        var openCount = 0
        composeRule.setContent {
            DaoTheme {
                AboutScreen(
                    appInfo = AboutAppInfo("0.1.0", "153.0.2"),
                    onOpenLicenses = { openCount++ },
                    onBack = {},
                )
            }
        }

        composeRule.onNodeWithTag("open-source-licenses-entry").performClick()

        assertEquals(1, openCount)
    }

    @Test
    fun aboutScreenScrollsToAndOpensLicensesInConstrainedHeight() {
        var openCount = 0
        composeRule.setContent {
            DaoTheme {
                Box(Modifier.width(360.dp).height(180.dp)) {
                    AboutScreen(
                        appInfo = AboutAppInfo("0.1.0", "153.0.2"),
                        onOpenLicenses = { openCount++ },
                        onBack = {},
                    )
                }
            }
        }

        composeRule.onNodeWithTag("about-content")
            .performScrollToNode(hasTestTag("open-source-licenses-entry"))
        composeRule.onNodeWithTag("open-source-licenses-entry").assertIsDisplayed().performClick()

        assertEquals(1, openCount)
    }

    @Test
    fun licenseListShowsBundledExtensionsAndOpensLocalDetail() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val uBlockSummary = "${context.getString(R.string.extension_version, "1.72.2")} · " +
            context.getString(R.string.gpl_v3_license)
        val kissSummary = "${context.getString(R.string.extension_version, "2.0.29")} · " +
            context.getString(R.string.gpl_v3_only_license)
        composeRule.setContent { DaoTheme { OpenSourceLicensesScreen(onBack = {}) } }

        composeRule.onNodeWithTag("bundled-license-u_block_origin").assertIsDisplayed()
        composeRule.onNodeWithTag("bundled-license-kiss_translator").assertIsDisplayed()
        composeRule.onNodeWithText(uBlockSummary).assertIsDisplayed()
        composeRule.onNodeWithText(kissSummary).assertIsDisplayed()
        composeRule.onNodeWithTag("bundled-license-kiss_translator").performClick()
        composeRule.onNodeWithTag("license-detail-screen").assertIsDisplayed()
        composeRule.onNodeWithText("GNU GENERAL PUBLIC LICENSE", substring = true)
            .assertIsDisplayed()
    }

    @Test
    fun licenseDetailBackReturnsToList() {
        composeRule.setContent { DaoTheme { OpenSourceLicensesScreen(onBack = {}) } }
        composeRule.onNodeWithTag("bundled-license-u_block_origin").performClick()

        composeRule.onNodeWithTag("license-detail-screen").assertIsDisplayed()
        pressBack()

        composeRule.onAllNodesWithTag("license-detail-screen").assertCountEquals(0)
        composeRule.onNodeWithTag("bundled-license-u_block_origin").assertIsDisplayed()
    }

    @Test
    fun licenseDetailShowsFallbackWhenLoadingFails() {
        val unavailable = InstrumentationRegistry.getInstrumentation().targetContext
            .getString(R.string.license_unavailable)
        composeRule.setContent {
            DaoTheme {
                OpenSourceLicensesScreen(
                    onBack = {},
                    loadLicenseText = { _, _ ->
                        Result.failure(IllegalStateException("Asset unavailable"))
                    },
                )
            }
        }

        composeRule.onNodeWithTag("bundled-license-u_block_origin").performClick()

        composeRule.onNodeWithText(unavailable).assertIsDisplayed()
    }
}
