package com.msgbyte.dao.ui

import android.graphics.Bitmap
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import com.msgbyte.dao.ui.theme.DaoTheme
import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.CompletableDeferred
import mozilla.components.browser.icons.BrowserIcons
import mozilla.components.browser.icons.Icon
import mozilla.components.browser.icons.IconRequest
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class SuggestionFaviconUiTest {
    @get:Rule val compose = createComposeRule()
    private val icons = mockk<BrowserIcons>()

    @Test
    fun aLoadedFaviconReplacesTheLetterAndChangingSitesClearsItImmediately() {
        val url = mutableStateOf("https://first.example")
        val first = CompletableDeferred<Icon>()
        val second = CompletableDeferred<Icon>()
        every { icons.loadIcon(any()) } answers {
            if (firstArg<IconRequest>().url == "https://first.example") first else second
        }
        compose.setContent {
            DaoTheme(darkTheme = false) {
                SuggestionFavicon(icons, url.value, false, "F", Color.Gray, Modifier.testTag("avatar"))
            }
        }
        compose.onNodeWithText("F").assertIsDisplayed()

        compose.runOnIdle { first.complete(icon(Icon.Source.DOWNLOAD)) }
        compose.onNodeWithText("F").assertDoesNotExist()
        compose.onNodeWithTag("avatar").assertIsDisplayed()

        compose.runOnIdle { url.value = "https://second.example" }
        compose.onNodeWithText("F").assertIsDisplayed()
    }

    @Test
    fun aGeneratedPlaceholderKeepsDaosLetterAvatar() {
        every { icons.loadIcon(any()) } returns CompletableDeferred(icon(Icon.Source.GENERATOR))
        compose.setContent {
            DaoTheme(darkTheme = false) {
                SuggestionFavicon(icons, "https://missing.example", false, "M", Color.Gray)
            }
        }
        compose.onNodeWithText("M").assertIsDisplayed()
    }

    private fun icon(source: Icon.Source) = Icon(
        Bitmap.createBitmap(48, 48, Bitmap.Config.ARGB_8888).apply { eraseColor(android.graphics.Color.RED) },
        source = source,
    )
}
