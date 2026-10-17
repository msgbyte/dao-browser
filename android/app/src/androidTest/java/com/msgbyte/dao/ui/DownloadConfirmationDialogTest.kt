package com.msgbyte.dao.ui

import android.content.Context
import android.text.format.Formatter
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ApplicationProvider
import androidx.test.espresso.Espresso.pressBack
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.msgbyte.dao.R
import com.msgbyte.dao.browser.DownloadRequestData
import com.msgbyte.dao.ui.theme.DaoTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class DownloadConfirmationDialogTest {
    @get:Rule
    val composeRule = createComposeRule()
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private var confirmed = 0
    private var dismissed = 0

    @Test
    fun showsTheFileAndKnownSizeAndStartsOnlyWhenDownloadIsPressed() {
        showDialog(size = 1024)

        composeRule.onNodeWithText("report.csv").assertIsDisplayed()
        composeRule.onNodeWithText(Formatter.formatShortFileSize(context, 1024)).assertIsDisplayed()
        assertEquals(0, confirmed)
        composeRule.onNodeWithText(context.getString(R.string.download)).performClick()

        assertEquals(1, confirmed)
        assertEquals(0, dismissed)
        composeRule.onNodeWithText("report.csv").assertDoesNotExist()
    }

    @Test
    fun cancelDoesNotStartAnUnknownSizeDownload() {
        showDialog(size = null)
        composeRule.onNodeWithText(context.getString(R.string.cancel)).performClick()

        assertEquals(0, confirmed)
        assertEquals(1, dismissed)
        composeRule.onNodeWithText("report.csv").assertDoesNotExist()
    }

    @Test
    fun backDismissesWithoutDownloading() {
        showDialog(size = null)
        composeRule.onNodeWithText("report.csv").assertIsDisplayed()
        pressBack()
        composeRule.waitForIdle()

        assertEquals(0, confirmed)
        assertEquals(1, dismissed)
    }

    private fun showDialog(size: Long?) {
        composeRule.setContent {
            var visible by remember { mutableStateOf(true) }
            DaoTheme(darkTheme = true) {
                if (visible) DownloadConfirmationDialog(
                    request = DownloadRequestData("https://example.com/report.csv", "report.csv", size),
                    onConfirm = { confirmed++; visible = false },
                    onDismiss = { dismissed++; visible = false },
                )
            }
        }
    }
}
