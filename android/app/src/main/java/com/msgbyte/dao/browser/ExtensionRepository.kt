package com.msgbyte.dao.browser

import java.net.URI
import java.net.URISyntaxException
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.concept.engine.webextension.EnableSource
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.PermissionPromptResponse
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionDelegate
import mozilla.components.concept.engine.webextension.WebExtensionInstallException

data class InstalledExtension(
    val id: String,
    val name: String,
    val description: String,
    val version: String,
    val enabled: Boolean,
    val builtIn: Boolean,
    val actionAvailable: Boolean,
)

data class PendingExtensionPermission(
    val id: String,
    val name: String,
    val version: String,
    val permissions: List<String>,
    val origins: List<String>,
    val dataCollectionPermissions: List<String>,
)

enum class ExtensionInstallFailure {
    INVALID_FILE_NAME,
    EMPTY_FILE,
    TOO_LARGE,
    UNAVAILABLE,
    BUSY,
    NOT_SIGNED,
    CORRUPT_FILE,
    INCOMPATIBLE,
    BLOCKLISTED,
    SOFT_BLOCKED,
    NETWORK_FAILURE,
    UNSUPPORTED_TYPE,
    USER_CANCELLED,
    UNKNOWN,
}

sealed interface ExtensionInstallState {
    data object Idle : ExtensionInstallState
    data object Staging : ExtensionInstallState
    data object Installing : ExtensionInstallState
    data class AwaitingPermission(val prompt: PendingExtensionPermission) : ExtensionInstallState
    data class Installed(val name: String) : ExtensionInstallState
    data class Failed(val reason: ExtensionInstallFailure) : ExtensionInstallState
}

class ExtensionRepository(
    private val engine: Engine,
) {
    private val mutableExtensions = MutableStateFlow<List<InstalledExtension>>(emptyList())
    private val mutableInstallState = MutableStateFlow<ExtensionInstallState>(ExtensionInstallState.Idle)
    private val engineExtensions = mutableMapOf<String, WebExtension>()
    private val browserActions = mutableMapOf<String, Action>()
    private var activeInstall: ActiveInstall? = null
    private var nextInstallToken = 0L
    private var permissionCallback: ((PermissionPromptResponse) -> Unit)? = null
    private var popupSessionHandler: ((EngineSession) -> Unit)? = null

    val extensions: StateFlow<List<InstalledExtension>> = mutableExtensions.asStateFlow()
    val installState: StateFlow<ExtensionInstallState> = mutableInstallState.asStateFlow()

    init {
        engine.registerWebExtensionDelegate(object : WebExtensionDelegate {
            override fun onInstalled(extension: WebExtension) {
                refresh()
            }

            override fun onUninstalled(extension: WebExtension) {
                refresh()
            }

            override fun onExtensionListUpdated() {
                refresh()
            }

            override fun onBrowserActionDefined(extension: WebExtension, action: Action) {
                browserActions[extension.id] = action
                updateActionAvailability(extension.id)
            }

            override fun onToggleActionPopup(
                extension: WebExtension,
                engineSession: EngineSession,
                action: Action,
            ): EngineSession? {
                val handler = popupSessionHandler ?: return null
                handler(engineSession)
                return engineSession
            }

            override fun onInstallPermissionRequest(
                extension: WebExtension,
                permissions: List<String>,
                origins: List<String>,
                dataCollectionPermissions: List<String>,
                onConfirm: (PermissionPromptResponse) -> Unit,
            ) {
                if (activeInstall == null || permissionCallback != null) {
                    onConfirm(PermissionPromptResponse(false, false, false))
                    return
                }
                val metadata = extension.getMetadata()
                permissionCallback = onConfirm
                mutableInstallState.value = ExtensionInstallState.AwaitingPermission(
                    PendingExtensionPermission(
                        id = extension.id,
                        name = metadata?.name?.takeIf(String::isNotBlank) ?: extension.id,
                        version = metadata?.version.orEmpty(),
                        permissions = permissions,
                        origins = origins,
                        dataCollectionPermissions = dataCollectionPermissions,
                    ),
                )
            }

        })
    }

    fun refresh(onError: (Throwable) -> Unit = {}) {
        engine.listInstalledWebExtensions(
            onSuccess = { values -> replaceExtensions(values) },
            onError = onError,
        )
    }

    fun setPopupSessionHandler(handler: ((EngineSession) -> Unit)?) {
        popupSessionHandler = handler
    }

    fun invokeAction(id: String): Boolean {
        val action = browserActions[id]?.takeUnless { it.enabled == false } ?: return false
        action.onClick()
        return true
    }

    fun setStaging() {
        if (activeInstall == null) mutableInstallState.value = ExtensionInstallState.Staging
    }

    fun reportStagingFailure(error: ExtensionPackageException) {
        mutableInstallState.value = ExtensionInstallState.Failed(
            when (error) {
                is ExtensionPackageException.InvalidFileName -> ExtensionInstallFailure.INVALID_FILE_NAME
                is ExtensionPackageException.EmptyFile -> ExtensionInstallFailure.EMPTY_FILE
                is ExtensionPackageException.TooLarge -> ExtensionInstallFailure.TOO_LARGE
                is ExtensionPackageException.Unavailable -> ExtensionInstallFailure.UNAVAILABLE
            },
        )
    }

    fun install(stagedPackage: StagedExtensionPackage): Boolean {
        return beginInstall(
            url = stagedPackage.uri,
            installationMethod = InstallationMethod.FROM_FILE,
            cleanup = stagedPackage::delete,
        )
    }

    fun installFromAmo(url: String): Boolean {
        if (!isHttpsXpiUrl(url)) return false
        return beginInstall(
            url = url,
            installationMethod = InstallationMethod.RTAMO,
            cleanup = null,
        )
    }

    private fun beginInstall(
        url: String,
        installationMethod: InstallationMethod,
        cleanup: (() -> Unit)?,
    ): Boolean {
        if (activeInstall != null || permissionCallback != null) return false
        val token = ++nextInstallToken
        activeInstall = ActiveInstall(token, cleanup)
        mutableInstallState.value = ExtensionInstallState.Installing
        try {
            engine.installWebExtension(
                url = url,
                installationMethod = installationMethod,
                onSuccess = { extension -> finishSuccess(token, extension) },
                onError = { error -> finishFailure(token, mapInstallFailure(error)) },
            )
        } catch (error: Throwable) {
            finishFailure(token, mapInstallFailure(error))
        }
        return true
    }

    fun resolvePermission(granted: Boolean) {
        val token = activeInstall?.token ?: return
        val callback = permissionCallback ?: return
        permissionCallback = null
        if (granted) {
            mutableInstallState.value = ExtensionInstallState.Installing
            callback(PermissionPromptResponse(true, false, false))
        } else {
            callback(PermissionPromptResponse(false, false, false))
            finishFailure(token, ExtensionInstallFailure.USER_CANCELLED)
        }
    }

    fun dismissResult() {
        if (mutableInstallState.value is ExtensionInstallState.Installed ||
            mutableInstallState.value is ExtensionInstallState.Failed
        ) {
            mutableInstallState.value = ExtensionInstallState.Idle
        }
    }

    fun uninstall(id: String, onError: (Throwable) -> Unit = {}): Boolean {
        val extension = engineExtensions[id] ?: return false
        if (extension.isBuiltIn()) return false
        engine.uninstallWebExtension(
            ext = extension,
            onSuccess = { refresh(onError) },
            onError = { _, error -> onError(error) },
        )
        return true
    }

    fun setEnabled(id: String, enabled: Boolean, onError: (Throwable) -> Unit = {}) {
        val extension = engineExtensions[id] ?: return
        val onSuccess: (WebExtension) -> Unit = { updated ->
            engineExtensions[id] = updated
            mutableExtensions.value = mutableExtensions.value.map { current ->
                if (current.id == id) mapExtension(updated) else current
            }
        }
        if (enabled) {
            engine.enableWebExtension(extension, EnableSource.USER, onSuccess, onError)
        } else {
            engine.disableWebExtension(extension, EnableSource.USER, onSuccess, onError)
        }
    }

    private fun finishSuccess(token: Long, extension: WebExtension) {
        if (activeInstall?.token != token) return
        permissionCallback = null
        activeInstall?.cleanup?.invoke()
        activeInstall = null
        mutableInstallState.value = ExtensionInstallState.Installed(
            extension.getMetadata()?.name?.takeIf(String::isNotBlank) ?: extension.id,
        )
        refresh()
    }

    private fun finishFailure(token: Long, reason: ExtensionInstallFailure) {
        if (activeInstall?.token != token) return
        permissionCallback = null
        activeInstall?.cleanup?.invoke()
        activeInstall = null
        mutableInstallState.value = ExtensionInstallState.Failed(reason)
    }

    private fun replaceExtensions(values: List<WebExtension>) {
        engineExtensions.clear()
        values.forEach { engineExtensions[it.id] = it }
        browserActions.keys.retainAll(engineExtensions.keys)
        mutableExtensions.value = values.map(::mapExtension)
    }

    private fun updateActionAvailability(id: String) {
        mutableExtensions.value = mutableExtensions.value.map { current ->
            if (current.id == id) current.copy(actionAvailable = true) else current
        }
    }

    private fun mapExtension(extension: WebExtension): InstalledExtension {
        val metadata = extension.getMetadata()
        return InstalledExtension(
            id = extension.id,
            name = metadata?.name?.takeIf(String::isNotBlank) ?: extension.id,
            description = metadata?.description.orEmpty(),
            version = metadata?.version.orEmpty(),
            enabled = extension.isEnabled(),
            builtIn = extension.isBuiltIn(),
            actionAvailable = browserActions.containsKey(extension.id),
        )
    }

    private fun isHttpsXpiUrl(value: String): Boolean = try {
        URI(value).let { uri ->
            uri.scheme == "https" &&
                !uri.host.isNullOrEmpty() &&
                uri.path.endsWith(".xpi")
        }
    } catch (_: URISyntaxException) {
        false
    }

    private data class ActiveInstall(
        val token: Long,
        val cleanup: (() -> Unit)?,
    )

    companion object {
        internal fun mapInstallFailure(error: Throwable): ExtensionInstallFailure = when (error) {
            is WebExtensionInstallException.NotSigned -> ExtensionInstallFailure.NOT_SIGNED
            is WebExtensionInstallException.CorruptFile -> ExtensionInstallFailure.CORRUPT_FILE
            is WebExtensionInstallException.Incompatible -> ExtensionInstallFailure.INCOMPATIBLE
            is WebExtensionInstallException.Blocklisted -> ExtensionInstallFailure.BLOCKLISTED
            is WebExtensionInstallException.SoftBlocked -> ExtensionInstallFailure.SOFT_BLOCKED
            is WebExtensionInstallException.NetworkFailure -> ExtensionInstallFailure.NETWORK_FAILURE
            is WebExtensionInstallException.UnsupportedAddonType -> ExtensionInstallFailure.UNSUPPORTED_TYPE
            is WebExtensionInstallException.UserCancelled -> ExtensionInstallFailure.USER_CANCELLED
            else -> ExtensionInstallFailure.UNKNOWN
        }
    }
}
