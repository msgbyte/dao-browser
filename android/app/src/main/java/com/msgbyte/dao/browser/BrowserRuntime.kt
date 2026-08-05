package com.msgbyte.dao.browser

import android.util.Log
import mozilla.components.concept.engine.Engine

class BrowserRuntime(
    private val createEngine: () -> Engine,
    private val setRemoteDebugging: (Boolean) -> Unit = {},
    private val onBuiltInAdBlockerError: (Throwable) -> Unit = { error ->
        Log.e(TAG, "Unable to install the built-in ad blocker", error)
    },
    private val onBuiltInTranslatorError: (Throwable) -> Unit = { error ->
        Log.e(TAG, "Unable to install the built-in translator", error)
    },
) {
    val engine: Engine by lazy(LazyThreadSafetyMode.SYNCHRONIZED) {
        createEngine().also { engine ->
            BuiltInAdBlocker.install(engine, onBuiltInAdBlockerError)
            BuiltInTranslator.install(engine, onBuiltInTranslatorError)
        }
    }

    fun setRemoteDebuggingEnabled(enabled: Boolean) {
        setRemoteDebugging(enabled)
    }

    private companion object {
        const val TAG = "BrowserRuntime"
    }
}
