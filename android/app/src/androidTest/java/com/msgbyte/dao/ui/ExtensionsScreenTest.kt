package com.msgbyte.dao.ui

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.msgbyte.dao.browser.BuiltInAdBlocker
import com.msgbyte.dao.browser.ExtensionRepository
import com.msgbyte.dao.ui.theme.DaoTheme
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.runs
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.webextension.Metadata
import mozilla.components.concept.engine.webextension.WebExtension
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ExtensionsScreenTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun exposesInstallActionAndUninstallOnlyForCustomExtensions() {
        val engine = mockk<Engine>()
        val builtIn = extension(BuiltInAdBlocker.EXTENSION_ID, "uBlock Origin", builtIn = true)
        val custom = extension("custom@example", "Custom", builtIn = false)
        every { engine.registerWebExtensionDelegate(any()) } just runs
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(builtIn, custom))
        }
        val repository = ExtensionRepository(engine)

        composeRule.setContent {
            DaoTheme {
                ExtensionsScreen(repository = repository, onOpenStore = {}, onBack = {})
            }
        }

        assertEquals(1, composeRule.onAllNodesWithTag("install-extension").fetchSemanticsNodes().size)
        assertEquals(
            1,
            composeRule.onAllNodesWithTag("uninstall-extension-custom@example")
                .fetchSemanticsNodes().size,
        )
        assertEquals(
            0,
            composeRule.onAllNodesWithTag("uninstall-extension-${BuiltInAdBlocker.EXTENSION_ID}")
                .fetchSemanticsNodes().size,
        )
    }

    @Test
    fun opensNativeStoreFromStoreAction() {
        val engine = mockk<Engine>()
        every { engine.registerWebExtensionDelegate(any()) } just runs
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(emptyList())
        }
        val repository = ExtensionRepository(engine)
        var storeOpened = false

        composeRule.setContent {
            DaoTheme {
                ExtensionsScreen(
                    repository = repository,
                    onOpenStore = { storeOpened = true },
                    onBack = {},
                )
            }
        }

        composeRule.onNodeWithTag("open-extension-store").performClick()

        composeRule.runOnIdle { assertEquals(true, storeOpened) }
    }

    private fun extension(id: String, name: String, builtIn: Boolean): WebExtension {
        val metadata = mockk<Metadata>()
        every { metadata.name } returns name
        every { metadata.description } returns "Description"
        every { metadata.version } returns "1.0"
        return mockk {
            every { this@mockk.id } returns id
            every { getMetadata() } returns metadata
            every { isEnabled() } returns true
            every { isBuiltIn() } returns builtIn
        }
    }
}
