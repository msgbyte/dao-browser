package com.msgbyte.dao.browser

import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.verify
import java.io.File
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.concept.engine.webextension.EnableSource
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.Metadata
import mozilla.components.concept.engine.webextension.PermissionPromptResponse
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionDelegate
import mozilla.components.concept.engine.webextension.WebExtensionInstallException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class ExtensionRepositoryTest {
    private val engine = mockk<Engine>()
    private val extension = mockk<WebExtension>()
    private val metadata = mockk<Metadata>()

    @Test
    fun exposesOnlyExtensionsReportedByTheEngine() {
        every { engine.registerWebExtensionDelegate(any()) } returns Unit
        stubExtension(extension, enabled = true, builtIn = true)
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(extension))
        }

        val repository = ExtensionRepository(engine)
        repository.refresh()

        assertEquals(1, repository.extensions.value.size)
        assertEquals("uBlock Origin", repository.extensions.value.single().name)
        assertEquals("1.72.2", repository.extensions.value.single().version)
        assertTrue(repository.extensions.value.single().enabled)
        assertTrue(repository.extensions.value.single().builtIn)
    }

    @Test
    fun togglesTheActualEngineExtensionAndRefreshesItsState() {
        every { engine.registerWebExtensionDelegate(any()) } returns Unit
        val disabled = mockk<WebExtension>()
        val success = slot<(WebExtension) -> Unit>()
        stubExtension(extension, enabled = true, builtIn = false)
        stubExtension(disabled, enabled = false, builtIn = false)
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(extension))
        }
        every {
            engine.disableWebExtension(extension, EnableSource.USER, capture(success), any())
        } answers { success.captured(disabled) }

        val repository = ExtensionRepository(engine)
        repository.refresh()
        repository.setEnabled(BuiltInAdBlocker.EXTENSION_ID, false)

        verify(exactly = 1) {
            engine.disableWebExtension(extension, EnableSource.USER, any(), any())
        }
        assertFalse(repository.extensions.value.single().enabled)
    }

    @Test
    fun exposesAndInvokesTheBrowserActionReportedByGecko() {
        val delegate = slot<WebExtensionDelegate>()
        var clickCount = 0
        val action = browserAction { clickCount += 1 }
        stubExtension(extension, enabled = true, builtIn = true)
        every { engine.registerWebExtensionDelegate(capture(delegate)) } returns Unit
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(extension))
        }
        val repository = ExtensionRepository(engine)
        repository.refresh()

        delegate.captured.onBrowserActionDefined(extension, action)

        assertTrue(repository.extensions.value.single().actionAvailable)
        assertTrue(repository.invokeAction(BuiltInAdBlocker.EXTENSION_ID))
        assertEquals(1, clickCount)
    }

    @Test
    fun handsTheExtensionPopupSessionToTheBrowserHost() {
        val delegate = slot<WebExtensionDelegate>()
        val popupSession = mockk<EngineSession>()
        var openedSession: EngineSession? = null
        every { engine.registerWebExtensionDelegate(capture(delegate)) } returns Unit
        val repository = ExtensionRepository(engine)
        repository.setPopupSessionHandler { openedSession = it }

        val returned = delegate.captured.onToggleActionPopup(
            extension,
            popupSession,
            browserAction {},
        )

        assertSame(popupSession, returned)
        assertSame(popupSession, openedSession)
    }

    @Test
    fun installsFromFileAfterExplicitPermissionApprovalAndCleansThePackage() {
        val delegate = slot<WebExtensionDelegate>()
        var permissionResponse: PermissionPromptResponse? = null
        val installSuccess = slot<(WebExtension) -> Unit>()
        val stagedFile = File.createTempFile("dao-extension", ".xpi")
        val staged = StagedExtensionPackage(stagedFile.toURI().toString(), stagedFile)
        stubExtension(extension, enabled = true, builtIn = false)
        every { engine.registerWebExtensionDelegate(capture(delegate)) } returns Unit
        every {
            engine.installWebExtension(
                staged.uri,
                InstallationMethod.FROM_FILE,
                capture(installSuccess),
                any(),
            )
        } returns mockk()
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(extension))
        }
        val repository = ExtensionRepository(engine)

        assertTrue(repository.install(staged))
        delegate.captured.onInstallPermissionRequest(
            extension,
            listOf("storage"),
            listOf("https://example.com/*"),
            listOf("technicalAndInteraction"),
        ) { response ->
            permissionResponse = response
        }

        val awaiting = repository.installState.value as ExtensionInstallState.AwaitingPermission
        assertEquals("uBlock Origin", awaiting.prompt.name)
        assertEquals(listOf("storage"), awaiting.prompt.permissions)
        repository.resolvePermission(granted = true)
        assertTrue(permissionResponse?.isPermissionsGranted == true)
        installSuccess.captured(extension)

        assertTrue(repository.installState.value is ExtensionInstallState.Installed)
        assertFalse(stagedFile.exists())
        assertEquals(1, repository.extensions.value.size)
    }

    @Test
    fun installsAmoUrlThroughRtamoAndSharedPermissionPrompt() {
        val delegate = slot<WebExtensionDelegate>()
        val installSuccess = slot<(WebExtension) -> Unit>()
        var permissionResponse: PermissionPromptResponse? = null
        var responseCount = 0
        val unrelatedLocalFile = File.createTempFile("dao-extension", ".xpi")
        stubExtension(extension, enabled = true, builtIn = false)
        every { engine.registerWebExtensionDelegate(capture(delegate)) } returns Unit
        every {
            engine.installWebExtension(
                AMO_XPI_URL,
                InstallationMethod.RTAMO,
                capture(installSuccess),
                any(),
            )
        } returns mockk()
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(extension))
        }
        val repository = ExtensionRepository(engine)

        assertTrue(repository.installFromAmo(AMO_XPI_URL))
        verify(exactly = 1) {
            engine.installWebExtension(AMO_XPI_URL, InstallationMethod.RTAMO, any(), any())
        }
        delegate.captured.onInstallPermissionRequest(
            extension,
            listOf("storage"),
            listOf("https://example.com/*"),
            emptyList(),
        ) { response ->
            responseCount += 1
            permissionResponse = response
        }

        assertTrue(repository.installState.value is ExtensionInstallState.AwaitingPermission)
        repository.resolvePermission(granted = true)
        repository.resolvePermission(granted = true)

        assertEquals(1, responseCount)
        assertTrue(permissionResponse?.isPermissionsGranted == true)
        assertTrue(repository.installState.value is ExtensionInstallState.Installing)
        installSuccess.captured(extension)

        assertTrue(repository.installState.value is ExtensionInstallState.Installed)
        assertTrue(unrelatedLocalFile.exists())
        verify(exactly = 1) { engine.listInstalledWebExtensions(any(), any()) }
        unrelatedLocalFile.delete()
    }

    @Test
    fun rejectsAmoPermissionOnceWithoutDeletingLocalFiles() {
        val delegate = slot<WebExtensionDelegate>()
        var responseCount = 0
        var granted: Boolean? = null
        val unrelatedLocalFile = File.createTempFile("dao-extension", ".xpi")
        stubExtension(extension, enabled = true, builtIn = false)
        every { engine.registerWebExtensionDelegate(capture(delegate)) } returns Unit
        every {
            engine.installWebExtension(AMO_XPI_URL, InstallationMethod.RTAMO, any(), any())
        } returns mockk()
        val repository = ExtensionRepository(engine)

        assertTrue(repository.installFromAmo(AMO_XPI_URL))
        delegate.captured.onInstallPermissionRequest(
            extension,
            emptyList(),
            emptyList(),
            emptyList(),
        ) { response ->
            responseCount += 1
            granted = response.isPermissionsGranted
        }

        repository.resolvePermission(granted = false)
        repository.resolvePermission(granted = true)

        assertEquals(1, responseCount)
        assertEquals(false, granted)
        assertTrue(unrelatedLocalFile.exists())
        assertTrue(repository.installState.value is ExtensionInstallState.Failed)
        unrelatedLocalFile.delete()
    }

    @Test
    fun rejectsInvalidAmoUrlsBeforeCallingGecko() {
        every { engine.registerWebExtensionDelegate(any()) } returns Unit
        val repository = ExtensionRepository(engine)

        assertFalse(repository.installFromAmo("http://addons.mozilla.org/addon.xpi"))
        assertFalse(repository.installFromAmo("https://addons.mozilla.org/addon.zip"))
        assertFalse(repository.installFromAmo("https:///addon.xpi"))
        assertFalse(repository.installFromAmo("not a uri"))

        verify(exactly = 0) { engine.installWebExtension(any(), any(), any(), any()) }
    }

    @Test
    fun rejectsAmoInstallWhileAnotherInstallIsActive() {
        val local = stagedPackage()
        every { engine.registerWebExtensionDelegate(any()) } returns Unit
        every { engine.installWebExtension(any(), any(), any(), any()) } returns mockk()
        val repository = ExtensionRepository(engine)

        assertTrue(repository.install(local))
        assertFalse(repository.installFromAmo(AMO_XPI_URL))

        local.delete()
    }

    @Test
    fun rejectsSecondAmoInstallWhileAnAmoInstallIsActive() {
        every { engine.registerWebExtensionDelegate(any()) } returns Unit
        every { engine.installWebExtension(any(), any(), any(), any()) } returns mockk()
        val repository = ExtensionRepository(engine)

        assertTrue(repository.installFromAmo(AMO_XPI_URL))
        assertFalse(repository.installFromAmo(AMO_XPI_URL))
        verify(exactly = 1) {
            engine.installWebExtension(AMO_XPI_URL, InstallationMethod.RTAMO, any(), any())
        }
    }

    @Test
    fun ignoresTerminalCallbackFromAnEarlierInstall() {
        val delegate = slot<WebExtensionDelegate>()
        val first = stagedPackage()
        val firstExtension = mockk<WebExtension>()
        val secondExtension = mockk<WebExtension>()
        val successCallbacks = mutableListOf<(WebExtension) -> Unit>()
        val errorCallbacks = mutableListOf<(Throwable) -> Unit>()
        stubExtension(firstExtension, enabled = true, builtIn = false, id = "first@example")
        stubExtension(secondExtension, enabled = true, builtIn = false, id = "second@example")
        every { engine.registerWebExtensionDelegate(capture(delegate)) } returns Unit
        every { engine.installWebExtension(any(), any(), any(), any()) } answers {
            successCallbacks += thirdArg<(WebExtension) -> Unit>()
            errorCallbacks += arg<(Throwable) -> Unit>(3)
            mockk()
        }
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(firstExtension))
        }
        val repository = ExtensionRepository(engine)

        assertTrue(repository.install(first))
        successCallbacks[0](firstExtension)
        assertFalse(first.file.exists())
        repository.dismissResult()
        assertTrue(repository.installFromAmo(AMO_XPI_URL))

        errorCallbacks[0](WebExtensionInstallException.NetworkFailure(null, IllegalStateException()))
        delegate.captured.onInstallationFailedRequest(
            firstExtension,
            WebExtensionInstallException.NetworkFailure(null, IllegalStateException()),
        )

        assertTrue(repository.installState.value is ExtensionInstallState.Installing)
        assertFalse(repository.installFromAmo(AMO_XPI_URL))
        successCallbacks[1](secondExtension)
        val installed = repository.installState.value as ExtensionInstallState.Installed
        assertEquals("uBlock Origin", installed.name)
    }

    @Test
    fun rejectsASecondInstallWithoutReplacingThePendingPrompt() {
        val delegate = slot<WebExtensionDelegate>()
        val first = stagedPackage()
        val second = stagedPackage()
        stubExtension(extension, enabled = true, builtIn = false)
        every { engine.registerWebExtensionDelegate(capture(delegate)) } returns Unit
        every { engine.installWebExtension(any(), any(), any(), any()) } returns mockk()
        val repository = ExtensionRepository(engine)

        assertTrue(repository.install(first))
        delegate.captured.onInstallPermissionRequest(
            extension,
            emptyList(),
            emptyList(),
            emptyList(),
        ) {}

        assertFalse(repository.install(second))
        assertTrue(repository.installState.value is ExtensionInstallState.AwaitingPermission)
        assertTrue(second.file.exists())
        first.delete()
        second.delete()
    }

    @Test
    fun denialRespondsOnceAndCleansThePackage() {
        val delegate = slot<WebExtensionDelegate>()
        var responseCount = 0
        var granted: Boolean? = null
        val staged = stagedPackage()
        stubExtension(extension, enabled = true, builtIn = false)
        every { engine.registerWebExtensionDelegate(capture(delegate)) } returns Unit
        every { engine.installWebExtension(any(), any(), any(), any()) } returns mockk()
        val repository = ExtensionRepository(engine)
        repository.install(staged)
        delegate.captured.onInstallPermissionRequest(
            extension,
            emptyList(),
            emptyList(),
            emptyList(),
        ) { response ->
            responseCount += 1
            granted = response.isPermissionsGranted
        }

        repository.resolvePermission(granted = false)
        repository.resolvePermission(granted = true)

        assertEquals(1, responseCount)
        assertEquals(false, granted)
        assertFalse(staged.file.exists())
        assertTrue(repository.installState.value is ExtensionInstallState.Failed)
    }

    @Test
    fun mapsKnownGeckoFailuresToStableReasons() {
        assertEquals(
            ExtensionInstallFailure.NOT_SIGNED,
            ExtensionRepository.mapInstallFailure(WebExtensionInstallException.NotSigned(IllegalStateException())),
        )
        assertEquals(
            ExtensionInstallFailure.CORRUPT_FILE,
            ExtensionRepository.mapInstallFailure(WebExtensionInstallException.CorruptFile(IllegalStateException())),
        )
        assertEquals(
            ExtensionInstallFailure.INCOMPATIBLE,
            ExtensionRepository.mapInstallFailure(WebExtensionInstallException.Incompatible(null, IllegalStateException())),
        )
        assertEquals(
            ExtensionInstallFailure.BLOCKLISTED,
            ExtensionRepository.mapInstallFailure(WebExtensionInstallException.Blocklisted(null, null, null, IllegalStateException())),
        )
        assertEquals(
            ExtensionInstallFailure.SOFT_BLOCKED,
            ExtensionRepository.mapInstallFailure(WebExtensionInstallException.SoftBlocked(null, null, null, IllegalStateException())),
        )
        assertEquals(
            ExtensionInstallFailure.NETWORK_FAILURE,
            ExtensionRepository.mapInstallFailure(WebExtensionInstallException.NetworkFailure(null, IllegalStateException())),
        )
        assertEquals(
            ExtensionInstallFailure.UNSUPPORTED_TYPE,
            ExtensionRepository.mapInstallFailure(WebExtensionInstallException.UnsupportedAddonType(null, IllegalStateException())),
        )
        assertEquals(
            ExtensionInstallFailure.USER_CANCELLED,
            ExtensionRepository.mapInstallFailure(WebExtensionInstallException.UserCancelled(null, IllegalStateException())),
        )
        assertEquals(
            ExtensionInstallFailure.UNKNOWN,
            ExtensionRepository.mapInstallFailure(IllegalStateException("unexpected")),
        )
    }

    @Test
    fun uninstallsOnlyCustomExtensions() {
        val custom = mockk<WebExtension>()
        stubExtension(custom, enabled = true, builtIn = false, id = "custom@example")
        every { engine.registerWebExtensionDelegate(any()) } returns Unit
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(custom))
        }
        every { engine.uninstallWebExtension(custom, any(), any()) } answers {
            secondArg<() -> Unit>().invoke()
        }
        val repository = ExtensionRepository(engine)
        repository.refresh()

        assertTrue(repository.uninstall("custom@example"))

        verify(exactly = 1) { engine.uninstallWebExtension(custom, any(), any()) }
    }

    @Test
    fun rejectsUninstallForBuiltInExtensions() {
        stubExtension(extension, enabled = true, builtIn = true)
        every { engine.registerWebExtensionDelegate(any()) } returns Unit
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            firstArg<(List<WebExtension>) -> Unit>().invoke(listOf(extension))
        }
        val repository = ExtensionRepository(engine)
        repository.refresh()

        assertFalse(repository.uninstall(BuiltInAdBlocker.EXTENSION_ID))

        verify(exactly = 0) { engine.uninstallWebExtension(any(), any(), any()) }
    }

    private fun stubExtension(
        value: WebExtension,
        enabled: Boolean,
        builtIn: Boolean,
        id: String = BuiltInAdBlocker.EXTENSION_ID,
    ) {
        every { value.id } returns id
        every { value.getMetadata() } returns metadata
        every { value.isEnabled() } returns enabled
        every { value.isBuiltIn() } returns builtIn
        every { metadata.name } returns "uBlock Origin"
        every { metadata.description } returns "An efficient content blocker"
        every { metadata.version } returns "1.72.2"
    }

    private fun stagedPackage(): StagedExtensionPackage {
        val file = File.createTempFile("dao-extension", ".xpi")
        return StagedExtensionPackage(file.toURI().toString(), file)
    }

    private fun browserAction(onClick: () -> Unit) = Action(
        title = "Open",
        enabled = true,
        loadIcon = null,
        badgeText = null,
        badgeTextColor = null,
        badgeBackgroundColor = null,
        onClick = onClick,
    )

    private companion object {
        const val AMO_XPI_URL = "https://addons.mozilla.org/firefox/downloads/file/123456/addon.xpi"
    }
}
