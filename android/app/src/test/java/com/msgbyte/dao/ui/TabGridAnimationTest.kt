package com.msgbyte.dao.ui

import android.content.Context
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipe
import androidx.test.core.app.ApplicationProvider
import com.msgbyte.dao.R
import com.msgbyte.dao.browser.TabThumbnailRepository
import com.msgbyte.dao.ui.theme.DaoTheme
import io.mockk.coEvery
import io.mockk.mockk
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class TabGridAnimationTest {
    @get:Rule val compose = createComposeRule()
    private val closed = mutableListOf<String>()

    @Test
    fun swipeClosesOnlyAfterTheCardFinishesSliding() {
        showGrid()
        swipeFirstCard(toFraction = 0.65f)

        compose.runOnIdle { assertTrue("The card must finish sliding before closing", closed.isEmpty()) }
        compose.mainClock.advanceTimeBy(1_000)
        compose.runOnIdle { assertEquals(listOf("first"), closed) }
    }

    @Test
    fun shortSwipeReturnsWithoutClosing() {
        showGrid()
        swipeFirstCard(toFraction = 0.25f)

        compose.mainClock.advanceTimeBy(1_000)
        compose.runOnIdle { assertTrue(closed.isEmpty()) }
        compose.onNodeWithTag("${TAB_CARD_TEST_TAG_PREFIX}first").assertExists()
    }

    @Test
    fun closeButtonMovesTheRemainingCardGraduallyIntoTheGap() {
        showGrid()
        val remaining = compose.onNodeWithTag("${TAB_CARD_TEST_TAG_PREFIX}second")
        val initialLeft = remaining.fetchSemanticsNode().boundsInRoot.left
        val context = ApplicationProvider.getApplicationContext<Context>()
        compose.onNode(
            hasContentDescription(context.getString(R.string.close_tab)) and
                hasAnyAncestor(hasTestTag("${TAB_CARD_TEST_TAG_PREFIX}first")),
        ).performClick()

        compose.mainClock.advanceTimeByFrame()
        compose.waitForIdle()
        compose.mainClock.advanceTimeBy(80)
        val movingLeft = remaining.fetchSemanticsNode().boundsInRoot.left
        compose.mainClock.advanceTimeBy(1_000)
        val finalLeft = remaining.fetchSemanticsNode().boundsInRoot.left

        assertTrue("The remaining card must move before settling", movingLeft < initialLeft)
        assertTrue("The remaining card must not jump directly into place", movingLeft > finalLeft)
        compose.runOnIdle { assertEquals(listOf("first"), closed) }
    }

    private fun showGrid() {
        val tabs = mutableStateOf(listOf("first", "second").map { id ->
            TabSessionState(id = id, content = ContentState(url = "https://$id.example"))
        })
        val thumbnails = mockk<TabThumbnailRepository>()
        coEvery { thumbnails.load(any(), any()) } returns null
        compose.setContent {
            DaoTheme(darkTheme = false) {
                TabGridScreen(
                    tabs = tabs.value,
                    selectedTabId = "second",
                    thumbnailRepository = thumbnails,
                    onSelect = {},
                    onClose = { id ->
                        closed += id
                        tabs.value = tabs.value.filterNot { it.id == id }
                    },
                    onModeChange = {},
                    onCloseAllPrivateTabs = {},
                    onNewTab = {},
                    onBack = {},
                )
            }
        }
        compose.waitForIdle()
        compose.mainClock.autoAdvance = false
    }

    private fun swipeFirstCard(toFraction: Float) {
        compose.onNodeWithTag("${TAB_CARD_TEST_TAG_PREFIX}first").performTouchInput {
            swipe(
                start = Offset(width * 0.1f, height / 2f),
                end = Offset(width * toFraction, height / 2f),
                durationMillis = 400,
            )
        }
    }
}
