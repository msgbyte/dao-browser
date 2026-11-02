package com.msgbyte.dao.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.click
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.isPopup
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.unit.dp
import com.msgbyte.dao.ui.theme.DaoTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [27]) // Robolectric has no window surface for the platform magnifier on API 28+.
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class AddressCursorUiTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun cursorDraggingScrollsToBothEndsOfALongAddressAndEditsAtThatPosition() {
        val address = "https://example.com/" + "long-path/".repeat(12) + "last-part"
        compose.setContent {
            DaoTheme(darkTheme = false) {
                val state = remember { TextFieldState(address, TextRange.Zero) }
                Box(Modifier.width(240.dp)) {
                    SearchField(
                        state = state,
                        expanded = true,
                        onActivate = {},
                        onTrailingClick = {},
                        onSubmit = {},
                        privateBrowsing = false,
                        tabCount = 1,
                        onTabs = {},
                        focusRequester = remember { FocusRequester() },
                    )
                }
            }
        }
        val field = compose.onNode(hasSetTextAction())
        field.performTouchInput { click(Offset(8f, centerY)) }
        val handle = compose.onNode(isPopup())
        handle.performTouchInput { down(center) }
        // Small movements beyond the edge keep bringing offscreen text into view.
        val dragDistance = field.fetchSemanticsNode().boundsInRoot.width + 40f
        repeat(40) {
            handle.performTouchInput { moveTo(Offset(centerX + dragDistance + it % 2, centerY)) }
            compose.mainClock.advanceTimeBy(32)
        }
        handle.performTouchInput { up() }
        assertEquals(TextRange(address.length), field.fetchSemanticsNode().config[SemanticsProperties.TextSelectionRange])
        field.performTextInput("?end")
        field.assertTextEquals("$address?end")

        field.performTouchInput { click(Offset(width - 8f, centerY)) }
        handle.performTouchInput { down(center) }
        repeat(40) {
            handle.performTouchInput { moveTo(Offset(centerX - dragDistance + it % 2, centerY)) }
            compose.mainClock.advanceTimeBy(32)
        }
        handle.performTouchInput { up() }
        assertEquals(TextRange.Zero, field.fetchSemanticsNode().config[SemanticsProperties.TextSelectionRange])
        field.performTextInput("start/")
        field.assertTextEquals("start/$address?end")
    }
}
