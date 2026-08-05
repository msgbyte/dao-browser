package com.msgbyte.dao.browser

import mozilla.components.concept.engine.CancellableOperation
import mozilla.components.concept.engine.Engine

object BuiltInAdBlocker {
    const val EXTENSION_ID = "uBlock0@raymondhill.net"
    const val RESOURCE_URI = "resource://android/assets/extensions/ublock_origin/"

    fun install(
        engine: Engine,
        onError: (Throwable) -> Unit,
    ): CancellableOperation = engine.installBuiltInWebExtension(
        id = EXTENSION_ID,
        url = RESOURCE_URI,
        onSuccess = {},
        onError = onError,
    )
}
