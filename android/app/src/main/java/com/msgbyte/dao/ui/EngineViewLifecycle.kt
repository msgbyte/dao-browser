package com.msgbyte.dao.ui

import androidx.lifecycle.Lifecycle
import mozilla.components.concept.engine.EngineView

class EngineViewLifecycle(
    private val engineView: EngineView,
) {
    private var disposed = false

    fun onEvent(event: Lifecycle.Event) {
        when (event) {
            Lifecycle.Event.ON_CREATE -> engineView.onCreate()
            Lifecycle.Event.ON_START -> engineView.onStart()
            Lifecycle.Event.ON_RESUME -> engineView.onResume()
            Lifecycle.Event.ON_PAUSE -> engineView.onPause()
            Lifecycle.Event.ON_STOP -> engineView.onStop()
            Lifecycle.Event.ON_DESTROY -> dispose()
            Lifecycle.Event.ON_ANY -> Unit
        }
    }

    fun dispose() {
        if (disposed) {
            return
        }

        disposed = true
        engineView.release()
        engineView.onDestroy()
    }
}
