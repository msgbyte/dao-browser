package com.msgbyte.dao.browser

import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.webextension.EnableSource

object BuiltInTranslator {
    const val EXTENSION_ID = "{fb25c100-22ce-4d5a-be7e-75f3d6f0fc13}"
    const val RESOURCE_URI = "resource://android/assets/extensions/kiss_translator/"

    private const val LEGACY_EXTENSION_ID = "{036a55b4-5e72-4d05-a06c-cba2dfcc134a}"

    fun install(
        engine: Engine,
        onError: (Throwable) -> Unit,
    ) = engine.listInstalledWebExtensions(
        onSuccess = { installed ->
            val startsDisabled = installed.none { it.id == EXTENSION_ID }
            engine.installBuiltInWebExtension(
                id = EXTENSION_ID,
                url = RESOURCE_URI,
                onSuccess = { extension ->
                    if (startsDisabled) {
                        engine.disableWebExtension(
                            extension = extension,
                            source = EnableSource.USER,
                            onSuccess = { removeLegacyExtension(engine, onError) },
                            onError = { error ->
                                onError(error)
                                removeLegacyExtension(engine, onError)
                            },
                        )
                    } else {
                        removeLegacyExtension(engine, onError)
                    }
                },
                onError = onError,
            )
        },
        onError = onError,
    )

    private fun removeLegacyExtension(
        engine: Engine,
        onError: (Throwable) -> Unit,
    ) {
        engine.listInstalledWebExtensions(
            onSuccess = { extensions ->
                extensions.firstOrNull { it.id == LEGACY_EXTENSION_ID }?.let { legacy ->
                    engine.uninstallWebExtension(
                        ext = legacy,
                        onSuccess = {},
                        onError = { _, error -> onError(error) },
                    )
                }
            },
            onError = onError,
        )
    }
}
