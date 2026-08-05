package com.msgbyte.dao.ui

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextInput
import androidx.annotation.StringRes
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.msgbyte.dao.R
import com.msgbyte.dao.browser.AmoAddon
import com.msgbyte.dao.browser.AmoStoreState
import com.msgbyte.dao.browser.ExtensionInstallState
import com.msgbyte.dao.browser.ExtensionInstallFailure
import com.msgbyte.dao.browser.PendingExtensionPermission
import com.msgbyte.dao.ui.theme.DaoTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AmoStoreScreenTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun showsRecommendedCatalogAndSelectedAddonDetails() {
        val addon = addon(guid = "addon@example", name = "Privacy Guard")

        setStoreContent(
            state = AmoStoreState.Content(query = "", items = listOf(addon), nextPage = null),
        )

        composeRule.onNodeWithTag("amo-store-screen").assertIsDisplayed()
        composeRule.onNodeWithText(string(R.string.amo_recommended)).assertIsDisplayed()
        composeRule.onNodeWithTag("amo-addon-addon@example").performClick()
        composeRule.onNodeWithTag("amo-addon-detail-addon@example", useUnmergedTree = true)
            .performScrollTo()
            .assertIsDisplayed()
        composeRule.onNodeWithText("A focused privacy extension.", useUnmergedTree = true)
            .assertIsDisplayed()
        composeRule.onNodeWithText(
            string(R.string.amo_author, "Dao Labs"),
            useUnmergedTree = true,
        ).assertIsDisplayed()
        composeRule.onNodeWithText(
            string(R.string.amo_version, "2.4.1"),
            useUnmergedTree = true,
        ).assertIsDisplayed()
        composeRule.onNodeWithText(
            string(R.string.amo_rating, "4.7"),
            useUnmergedTree = true,
        ).assertIsDisplayed()
    }

    @Test
    fun submitsTrimmedSearchQuery() {
        var submittedQuery: String? = null

        setStoreContent(
            state = AmoStoreState.Content(query = "", items = listOf(addon()), nextPage = null),
            onSearch = { submittedQuery = it },
        )

        composeRule.onNodeWithTag("amo-search-field").performTextInput("  privacy  ")
        composeRule.onNodeWithTag("amo-search-submit").performClick()

        composeRule.runOnIdle { assertEquals("privacy", submittedQuery) }
    }

    @Test
    fun retriesEmptyAndFailedCatalogStates() {
        var retries = 0
        var state by mutableStateOf<AmoStoreState>(AmoStoreState.Empty("privacy"))
        composeRule.setContent {
            DaoTheme {
                AmoStoreScreen(
                    state = state,
                    installedExtensionGuids = emptySet(),
                    installState = ExtensionInstallState.Idle,
                    transientInstallGuid = null,
                    onTransientInstallGuidChange = {},
                    onSearch = {},
                    onRetry = { retries++ },
                    onLoadNext = {},
                    onInstall = { false },
                    onDismissInstallResult = {},
                    onResolvePermission = {},
                    onBack = {},
                )
            }
        }

        composeRule.onNodeWithText(string(R.string.amo_empty)).assertIsDisplayed()
        composeRule.onNodeWithTag("amo-retry").performClick()
        composeRule.runOnIdle {
            assertEquals(1, retries)
            state = AmoStoreState.Failed("privacy", IllegalStateException("offline"))
        }
        composeRule.onNodeWithText(string(R.string.amo_failure)).assertIsDisplayed()
        composeRule.onNodeWithTag("amo-retry").performClick()
        composeRule.runOnIdle { assertEquals(2, retries) }
    }

    @Test
    fun showsInstallInstallingAndInstalledLabels() {
        val available = addon(guid = "available@example", name = "Available")
        val installed = addon(guid = "installed@example", name = "Installed Add-on")
        var installState by mutableStateOf<ExtensionInstallState>(ExtensionInstallState.Idle)
        var transientInstallGuid by mutableStateOf<String?>(null)
        var requestedUrl: String? = null
        composeRule.setContent {
            DaoTheme {
                AmoStoreScreen(
                    state = AmoStoreState.Content(
                        query = "privacy",
                        items = listOf(available, installed),
                        nextPage = null,
                    ),
                    installedExtensionGuids = setOf(installed.guid),
                    installState = installState,
                    transientInstallGuid = transientInstallGuid,
                    onTransientInstallGuidChange = { transientInstallGuid = it },
                    onSearch = {},
                    onRetry = {},
                    onLoadNext = {},
                    onInstall = {
                        requestedUrl = it.installUrl
                        installState = ExtensionInstallState.Installing
                        true
                    },
                    onDismissInstallResult = {},
                    onResolvePermission = {},
                    onBack = {},
                )
            }
        }

        composeRule.onNodeWithTag("amo-install-available@example")
            .assertTextEquals(string(R.string.amo_install))
        composeRule.onNodeWithTag("amo-install-installed@example")
            .assertTextEquals(string(R.string.amo_installed))
        composeRule.onNodeWithTag("amo-install-available@example").performClick()
        composeRule.onNodeWithTag("amo-install-available@example")
            .assertTextEquals(string(R.string.amo_installing))
        composeRule.runOnIdle { assertEquals(available.installUrl, requestedUrl) }
    }

    @Test
    fun keepsInstalledLabelUntilInstalledIdsRefresh() {
        val addon = addon(guid = "pending-refresh@example")
        var clearedGuid: String? = "not-called"

        setStoreContent(
            state = AmoStoreState.Content(query = "privacy", items = listOf(addon), nextPage = null),
            installedExtensionGuids = emptySet(),
            installState = ExtensionInstallState.Installed(addon.name),
            transientInstallGuid = addon.guid,
            onTransientInstallGuidChange = { clearedGuid = it },
        )

        composeRule.onNodeWithTag("amo-install-pending-refresh@example")
            .assertTextEquals(string(R.string.amo_installed))
            .assertIsNotEnabled()
        composeRule.runOnIdle { assertEquals("not-called", clearedGuid) }
    }

    @Test
    fun keepsInstallingLabelWhenStoreLeavesAndReenters() {
        val addon = addon(guid = "reentry@example")
        var showStore by mutableStateOf(true)
        var installState by mutableStateOf<ExtensionInstallState>(ExtensionInstallState.Idle)
        var transientInstallGuid by mutableStateOf<String?>(null)
        composeRule.setContent {
            DaoTheme {
                if (showStore) {
                    AmoStoreScreen(
                        state = AmoStoreState.Content(
                            query = "privacy",
                            items = listOf(addon),
                            nextPage = null,
                        ),
                        installedExtensionGuids = emptySet(),
                        installState = installState,
                        transientInstallGuid = transientInstallGuid,
                        onTransientInstallGuidChange = { transientInstallGuid = it },
                        onSearch = {},
                        onRetry = {},
                        onLoadNext = {},
                        onInstall = {
                            installState = ExtensionInstallState.Installing
                            true
                        },
                        onDismissInstallResult = {},
                        onResolvePermission = {},
                        onBack = {},
                    )
                }
            }
        }

        composeRule.onNodeWithTag("amo-install-reentry@example").performClick()
        composeRule.onNodeWithTag("amo-install-reentry@example")
            .assertTextEquals(string(R.string.amo_installing))
        composeRule.runOnIdle { showStore = false }
        composeRule.runOnIdle { showStore = true }
        composeRule.onNodeWithTag("amo-install-reentry@example")
            .assertTextEquals(string(R.string.amo_installing))
            .assertIsNotEnabled()
    }

    @Test
    fun clearsTransientGuidAfterInstalledIdsRefresh() {
        val addon = addon(guid = "synced@example")
        var transientInstallGuid by mutableStateOf<String?>(addon.guid)

        setStoreContent(
            state = AmoStoreState.Content(query = "privacy", items = listOf(addon), nextPage = null),
            installedExtensionGuids = setOf(addon.guid),
            installState = ExtensionInstallState.Installed(addon.name),
            transientInstallGuid = transientInstallGuid,
            onTransientInstallGuidChange = { transientInstallGuid = it },
        )

        composeRule.waitForIdle()
        composeRule.runOnIdle { assertEquals(null, transientInstallGuid) }
        composeRule.onNodeWithTag("amo-install-synced@example")
            .assertTextEquals(string(R.string.amo_installed))
            .assertIsNotEnabled()
    }

    @Test
    fun clearsTransientGuidAfterInstallFailure() {
        val addon = addon(guid = "failed@example")
        var transientInstallGuid by mutableStateOf<String?>(addon.guid)

        setStoreContent(
            state = AmoStoreState.Content(query = "privacy", items = listOf(addon), nextPage = null),
            installState = ExtensionInstallState.Failed(ExtensionInstallFailure.USER_CANCELLED),
            transientInstallGuid = transientInstallGuid,
            onTransientInstallGuidChange = { transientInstallGuid = it },
        )

        composeRule.waitForIdle()
        composeRule.runOnIdle { assertEquals(null, transientInstallGuid) }
    }

    @Test
    fun clearsFailedInstallWhileStoreIsAwayBeforeResultIsDismissed() {
        val addon = addon(guid = "failed-away@example")
        var showStore by mutableStateOf(true)
        var installState by mutableStateOf<ExtensionInstallState>(ExtensionInstallState.Installing)
        var transientInstallGuid by mutableStateOf<String?>(addon.guid)
        composeRule.setContent {
            DaoTheme {
                AmoInstallTrackingEffect(
                    transientInstallGuid = transientInstallGuid,
                    installedExtensionGuids = emptySet(),
                    installState = installState,
                    onTransientInstallGuidChange = { transientInstallGuid = it },
                )
                if (showStore) {
                    AmoStoreScreen(
                        state = AmoStoreState.Content("privacy", listOf(addon), null),
                        installedExtensionGuids = emptySet(),
                        installState = installState,
                        transientInstallGuid = transientInstallGuid,
                        onTransientInstallGuidChange = { transientInstallGuid = it },
                        onSearch = {},
                        onRetry = {},
                        onLoadNext = {},
                        onInstall = { false },
                        onDismissInstallResult = {},
                        onResolvePermission = {},
                        onBack = {},
                    )
                }
            }
        }

        composeRule.runOnIdle { showStore = false }
        composeRule.runOnIdle { installState = ExtensionInstallState.Failed(ExtensionInstallFailure.NETWORK_FAILURE) }
        composeRule.waitForIdle()
        composeRule.runOnIdle {
            installState = ExtensionInstallState.Idle
            showStore = true
        }

        composeRule.onNodeWithTag("amo-install-failed-away@example")
            .assertTextEquals(string(R.string.amo_install))
        composeRule.runOnIdle { assertEquals(null, transientInstallGuid) }
    }

    @Test
    fun storeActionsExposeButtonSemantics() {
        setStoreContent(
            state = AmoStoreState.Content("", listOf(addon()), null),
        )

        composeRule.onNodeWithTag("amo-search-submit")
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
        composeRule.onNodeWithTag("amo-install-addon@example")
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
    }

    @Test
    fun showsFailedInstallReasonAndDismissesResult() {
        var dismissed = false

        setStoreContent(
            state = AmoStoreState.Content(query = "privacy", items = listOf(addon()), nextPage = null),
            installState = ExtensionInstallState.Failed(ExtensionInstallFailure.NOT_SIGNED),
            onDismissInstallResult = { dismissed = true },
        )

        composeRule.onNodeWithText(string(R.string.extension_error_not_signed))
            .assertIsDisplayed()
        composeRule.onNodeWithText(string(R.string.done)).performClick()
        composeRule.runOnIdle { assertTrue(dismissed) }
    }

    @Test
    fun keepsInstalledRowAfterDismissingStatusBeforeIdsRefresh() {
        val addon = addon(guid = "dismissed-before-refresh@example")
        var installState by mutableStateOf<ExtensionInstallState>(
            ExtensionInstallState.Installed(addon.name),
        )

        composeRule.setContent {
            DaoTheme {
                AmoStoreScreen(
                    state = AmoStoreState.Content(
                        query = "privacy",
                        items = listOf(addon),
                        nextPage = null,
                    ),
                    installedExtensionGuids = emptySet(),
                    installState = installState,
                    transientInstallGuid = addon.guid,
                    onTransientInstallGuidChange = {},
                    onSearch = {},
                    onRetry = {},
                    onLoadNext = {},
                    onInstall = { false },
                    onDismissInstallResult = { installState = ExtensionInstallState.Idle },
                    onResolvePermission = {},
                    onBack = {},
                )
            }
        }

        composeRule.onNodeWithText(string(R.string.done)).performClick()
        composeRule.onNodeWithTag("amo-install-dismissed-before-refresh@example")
            .assertTextEquals(string(R.string.amo_installed))
            .assertIsNotEnabled()
    }

    @Test
    fun requestsTheNextCatalogPage() {
        var loadNextCalled = false
        setStoreContent(
            state = AmoStoreState.Content(query = "privacy", items = listOf(addon()), nextPage = 2),
            onLoadNext = { loadNextCalled = true },
        )

        composeRule.onNodeWithTag("amo-load-more").performClick()

        composeRule.runOnIdle { assertTrue(loadNextCalled) }
    }

    @Test
    fun resolvesPermissionFromTheSharedSheet() {
        var granted: Boolean? = null
        setStoreContent(
            state = AmoStoreState.Content(query = "", items = listOf(addon()), nextPage = null),
            installState = ExtensionInstallState.AwaitingPermission(
                PendingExtensionPermission(
                    id = "addon@example",
                    name = "Privacy Guard",
                    version = "2.4.1",
                    permissions = listOf("tabs"),
                    origins = listOf("https://example.com/*"),
                    dataCollectionPermissions = emptyList(),
                ),
            ),
            onResolvePermission = { granted = it },
        )

        composeRule.onNodeWithTag("extension-permission-sheet").assertIsDisplayed()
        composeRule.onNodeWithTag("approve-extension").performClick()

        composeRule.runOnIdle { assertEquals(true, granted) }
    }

    @Test
    fun invokesBackCallback() {
        var backCalled = false
        setStoreContent(
            state = AmoStoreState.Content(query = "", items = listOf(addon()), nextPage = null),
            onBack = { backCalled = true },
        )

        composeRule.onNodeWithContentDescription(string(R.string.navigate_back)).performClick()

        composeRule.runOnIdle { assertTrue(backCalled) }
    }

    private fun setStoreContent(
        state: AmoStoreState,
        installedExtensionGuids: Set<String> = emptySet(),
        installState: ExtensionInstallState = ExtensionInstallState.Idle,
        transientInstallGuid: String? = null,
        onTransientInstallGuidChange: (String?) -> Unit = {},
        onSearch: (String) -> Unit = {},
        onRetry: () -> Unit = {},
        onLoadNext: () -> Unit = {},
        onInstall: (AmoAddon) -> Boolean = { false },
        onDismissInstallResult: () -> Unit = {},
        onResolvePermission: (Boolean) -> Unit = {},
        onBack: () -> Unit = {},
    ) {
        composeRule.setContent {
            DaoTheme {
                AmoInstallTrackingEffect(
                    transientInstallGuid = transientInstallGuid,
                    installedExtensionGuids = installedExtensionGuids,
                    installState = installState,
                    onTransientInstallGuidChange = onTransientInstallGuidChange,
                )
                AmoStoreScreen(
                    state = state,
                    installedExtensionGuids = installedExtensionGuids,
                    installState = installState,
                    transientInstallGuid = transientInstallGuid,
                    onTransientInstallGuidChange = onTransientInstallGuidChange,
                    onSearch = onSearch,
                    onRetry = onRetry,
                    onLoadNext = onLoadNext,
                    onInstall = onInstall,
                    onDismissInstallResult = onDismissInstallResult,
                    onResolvePermission = onResolvePermission,
                    onBack = onBack,
                )
            }
        }
    }

    private fun addon(
        guid: String = "addon@example",
        name: String = "Privacy Guard",
    ) = AmoAddon(
        id = 42,
        guid = guid,
        slug = "privacy-guard",
        name = name,
        summary = "A focused privacy extension.",
        author = "Dao Labs",
        iconUrl = null,
        rating = 4.7,
        version = "2.4.1",
        detailUrl = "https://addons.mozilla.org/addon/privacy-guard/",
        installUrl = "https://addons.mozilla.org/firefox/downloads/latest/privacy-guard/addon.xpi",
    )

    private fun string(@StringRes id: Int, vararg args: Any): String =
        InstrumentationRegistry.getInstrumentation().targetContext.getString(id, *args)
}
