package com.msgbyte.dao.ui

import androidx.lifecycle.Lifecycle
import io.mockk.mockk
import io.mockk.verify
import io.mockk.verifyOrder
import mozilla.components.concept.engine.EngineView
import org.junit.Test

class EngineViewLifecycleTest {
    @Test
    fun forwardsLifecycleAndReleasesView() {
        val view = mockk<EngineView>(relaxed = true)
        val lifecycle = EngineViewLifecycle(view)

        lifecycle.onEvent(Lifecycle.Event.ON_CREATE)
        lifecycle.onEvent(Lifecycle.Event.ON_START)
        lifecycle.onEvent(Lifecycle.Event.ON_RESUME)
        lifecycle.onEvent(Lifecycle.Event.ON_PAUSE)
        lifecycle.onEvent(Lifecycle.Event.ON_STOP)
        lifecycle.dispose()

        verifyOrder {
            view.onCreate()
            view.onStart()
            view.onResume()
            view.onPause()
            view.onStop()
            view.release()
            view.onDestroy()
        }
    }

    @Test
    fun destroysViewOnlyOnceWhenLifecycleAndComposeBothDisposeIt() {
        val view = mockk<EngineView>(relaxed = true)
        val lifecycle = EngineViewLifecycle(view)

        lifecycle.onEvent(Lifecycle.Event.ON_DESTROY)
        lifecycle.dispose()

        verifyOrder {
            view.release()
            view.onDestroy()
        }
        verify(exactly = 1) { view.release() }
        verify(exactly = 1) { view.onDestroy() }
    }
}
