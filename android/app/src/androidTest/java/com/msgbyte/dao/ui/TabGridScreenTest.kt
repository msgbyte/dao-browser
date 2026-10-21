package com.msgbyte.dao.ui

import android.content.Context
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.msgbyte.dao.R
import com.msgbyte.dao.browser.BrowserTabsController
import com.msgbyte.dao.browser.TabThumbnailRepository
import com.msgbyte.dao.ui.theme.DaoTheme
import mozilla.components.browser.state.store.BrowserStore
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TabGridScreenTest {
    @get:Rule
    val composeRule = createComposeRule()
    private val context = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun switchingGroupsFiltersCardsAndBulkClosePreservesRegularTabs() {
        lateinit var controller: BrowserTabsController
        lateinit var regularId: String
        lateinit var privateId: String
        composeRule.runOnIdle {
            controller = BrowserTabsController(BrowserStore(), defaultPrivateBrowsing = false)
            regularId = controller.createTab(private = false)
            privateId = controller.createTab(private = true)
            controller.selectTab(regularId)
        }
        val thumbnails = TabThumbnailRepository(context)
        composeRule.setContent {
            val state by controller.state.collectAsState()
            DaoTheme {
                TabGridScreen(
                    tabs = state.tabs,
                    selectedTabId = state.selectedTabId,
                    thumbnailRepository = thumbnails,
                    onSelect = controller::selectTab,
                    onClose = controller::closeTab,
                    onModeChange = controller::switchBrowsingMode,
                    onCloseAllPrivateTabs = controller::closeAllPrivateTabs,
                    onNewTab = { controller.createTab() },
                    onBack = {},
                )
            }
        }

        composeRule.onNodeWithText(context.getString(R.string.regular_tabs, 1)).assertIsSelected()
        composeRule.onNodeWithTag("$TAB_CARD_TEST_TAG_PREFIX$regularId").assertIsDisplayed()
        composeRule.onNodeWithTag("$TAB_CARD_TEST_TAG_PREFIX$privateId").assertDoesNotExist()
        composeRule.onNodeWithContentDescription(context.getString(R.string.close_all_private_tabs)).assertDoesNotExist()

        composeRule.onNodeWithText(context.getString(R.string.private_tabs, 1)).performClick().assertIsSelected()
        composeRule.onNodeWithTag("$TAB_CARD_TEST_TAG_PREFIX$privateId").assertIsDisplayed()
        composeRule.onNodeWithTag("$TAB_CARD_TEST_TAG_PREFIX$regularId").assertDoesNotExist()
        composeRule.onNodeWithContentDescription(context.getString(R.string.new_private_tab)).performClick()
        composeRule.onNodeWithText(context.getString(R.string.private_tabs, 2)).assertIsSelected()

        composeRule.onNodeWithContentDescription(context.getString(R.string.close_all_private_tabs)).performClick()
        composeRule.onNodeWithText(context.getString(R.string.private_tabs, 0)).assertIsDisplayed()
        composeRule.onNodeWithText(context.getString(R.string.regular_tabs, 1)).assertIsSelected()
        composeRule.onNodeWithTag("$TAB_CARD_TEST_TAG_PREFIX$regularId").assertIsDisplayed()
        composeRule.runOnIdle {
            assertEquals(listOf(regularId), controller.state.value.tabs.map { it.id })
        }
    }
}
