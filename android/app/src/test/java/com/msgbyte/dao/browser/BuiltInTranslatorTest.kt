package com.msgbyte.dao.browser

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.concept.engine.CancellableOperation
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.webextension.EnableSource
import mozilla.components.concept.engine.webextension.WebExtension
import org.junit.Test

class BuiltInTranslatorTest {
    @Test
    fun installsThePinnedTranslationExtension() {
        val engine = mockk<Engine>()
        val operation = mockk<CancellableOperation>()
        val kiss = extension(BuiltInTranslator.EXTENSION_ID)
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            arg<(List<WebExtension>) -> Unit>(0)(listOf(kiss))
        }
        every { engine.installBuiltInWebExtension(any(), any(), any(), any()) } answers {
            arg<(WebExtension) -> Unit>(2)(kiss)
            operation
        }

        BuiltInTranslator.install(engine) {}

        verify(exactly = 1) {
            engine.installBuiltInWebExtension(
                id = "{fb25c100-22ce-4d5a-be7e-75f3d6f0fc13}",
                url = "resource://android/assets/extensions/kiss_translator/",
                onSuccess = any(),
                onError = any(),
            )
        }
    }

    @Test
    fun newlyInstalledTranslatorStartsDisabled() {
        val engine = mockk<Engine>()
        val operation = mockk<CancellableOperation>()
        val kiss = extension(BuiltInTranslator.EXTENSION_ID)
        var inventoryCalls = 0
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            inventoryCalls += 1
            arg<(List<WebExtension>) -> Unit>(0)(
                if (inventoryCalls == 1) emptyList() else listOf(kiss),
            )
        }
        every { engine.installBuiltInWebExtension(any(), any(), any(), any()) } answers {
            arg<(WebExtension) -> Unit>(2)(kiss)
            operation
        }
        every {
            engine.disableWebExtension(kiss, EnableSource.USER, any(), any())
        } answers {
            arg<(WebExtension) -> Unit>(2)(kiss)
        }

        BuiltInTranslator.install(engine) {}

        verify(exactly = 1) {
            engine.disableWebExtension(kiss, EnableSource.USER, any(), any())
        }
        verify(exactly = 0) {
            engine.enableWebExtension(any(), any(), any(), any())
        }
    }

    @Test
    fun existingTranslatorKeepsItsEnabledState() {
        val engine = mockk<Engine>()
        val operation = mockk<CancellableOperation>()
        val kiss = extension(BuiltInTranslator.EXTENSION_ID)
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            arg<(List<WebExtension>) -> Unit>(0)(listOf(kiss))
        }
        every { engine.installBuiltInWebExtension(any(), any(), any(), any()) } answers {
            arg<(WebExtension) -> Unit>(2)(kiss)
            operation
        }

        BuiltInTranslator.install(engine) {}

        verify(exactly = 0) {
            engine.disableWebExtension(any(), any(), any(), any())
        }
        verify(exactly = 0) {
            engine.enableWebExtension(any(), any(), any(), any())
        }
    }

    @Test
    fun removesTheLegacyTranslatorAfterKissIsInstalled() {
        val engine = mockk<Engine>()
        val operation = mockk<CancellableOperation>()
        val kiss = extension(BuiltInTranslator.EXTENSION_ID)
        val legacy = extension("{036a55b4-5e72-4d05-a06c-cba2dfcc134a}")
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            arg<(List<WebExtension>) -> Unit>(0)(listOf(kiss, legacy))
        }
        every {
            engine.installBuiltInWebExtension(any(), any(), any(), any())
        } answers {
            arg<(WebExtension) -> Unit>(2)(kiss)
            operation
        }
        every { engine.uninstallWebExtension(any(), any(), any()) } returns Unit

        BuiltInTranslator.install(engine) {}

        verify(exactly = 1) {
            engine.uninstallWebExtension(
                ext = legacy,
                onSuccess = any(),
                onError = any(),
            )
        }
    }

    private fun extension(extensionId: String) = mockk<WebExtension> {
        every { id } returns extensionId
    }
}
