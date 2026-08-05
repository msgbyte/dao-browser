package com.msgbyte.dao.ui

import android.content.Context
import android.graphics.Bitmap
import android.view.View
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeDown
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout
import io.mockk.mockk
import java.lang.reflect.Proxy
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.coroutines.runBlocking
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineView
import mozilla.components.concept.engine.selection.SelectionActionDelegate
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class BrowserSurfaceTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun initializationAndRecompositionDoNotDestroyCreatedView() {
        lateinit var engineView: RecordingEngineView
        val engine = fakeEngine { context ->
            RecordingEngineView(context).also { engineView = it }
        }
        val session = mockk<EngineSession>(relaxed = true)
        val recomposition = mutableIntStateOf(0)

        composeRule.setContent {
            BrowserSurface(engine = engine, session = session)
            recomposition.intValue
        }
        composeRule.waitForIdle()

        composeRule.runOnIdle {
            recomposition.intValue++
        }
        composeRule.waitForIdle()

        assertEquals(0, engineView.releaseCount.get())
        assertEquals(0, engineView.destroyCount.get())
    }

    @Test
    fun boundCaptureReturnsTheRealEngineViewThumbnail() {
        val expected = Bitmap.createBitmap(24, 32, Bitmap.Config.ARGB_8888)
        val engine = fakeEngine { context -> RecordingEngineView(context, expected) }
        val session = mockk<EngineSession>(relaxed = true)
        val capture = BrowserThumbnailCapture()

        composeRule.setContent {
            BrowserSurface(
                engine = engine,
                session = session,
                thumbnailCapture = capture,
            )
        }
        composeRule.waitForIdle()

        assertSame(expected, runBlocking { capture.capture() })
    }

    @Test
    fun pullingAtTheTopInvokesRefreshOnce() {
        val refreshCount = AtomicInteger()
        val engine = fakeEngine { context -> RecordingEngineView(context) }
        val session = mockk<EngineSession>(relaxed = true)

        composeRule.setContent {
            BrowserSurface(
                engine = engine,
                session = session,
                modifier = Modifier.fillMaxSize().testTag(REFRESH_SURFACE_TAG),
                onRefresh = { refreshCount.incrementAndGet() },
            )
        }
        composeRule.waitForIdle()

        composeRule.onNodeWithTag(REFRESH_SURFACE_TAG).performTouchInput {
            swipeDown(durationMillis = 500)
        }

        composeRule.waitUntil(timeoutMillis = 5_000) { refreshCount.get() == 1 }
        assertEquals(1, refreshCount.get())
    }

    @Test
    fun pullingWhileGeckoReportsNonZeroScrollPositionDoesNotRefresh() {
        val refreshCount = AtomicInteger()
        val engine = fakeEngine { context -> RecordingEngineView(context, scrollPosition = 120f) }
        val session = mockk<EngineSession>(relaxed = true)

        composeRule.setContent {
            BrowserSurface(
                engine = engine,
                session = session,
                modifier = Modifier.fillMaxSize().testTag(REFRESH_SURFACE_TAG),
                onRefresh = { refreshCount.incrementAndGet() },
            )
        }
        composeRule.waitForIdle()

        composeRule.onNodeWithTag(REFRESH_SURFACE_TAG).performTouchInput {
            swipeDown(durationMillis = 500)
        }
        composeRule.waitForIdle()

        assertEquals(0, refreshCount.get())
    }

    @Test
    fun refreshingStateUpdatesWithoutRecreatingTheEngineView() {
        lateinit var engineView: RecordingEngineView
        val refreshing = mutableStateOf(false)
        val engine = fakeEngine { context ->
            RecordingEngineView(context).also { engineView = it }
        }
        val session = mockk<EngineSession>(relaxed = true)

        composeRule.setContent {
            BrowserSurface(
                engine = engine,
                session = session,
                refreshing = refreshing.value,
            )
        }
        composeRule.waitForIdle()
        val container = engineView.parent as SwipeRefreshLayout
        assertFalse(container.isRefreshing)

        composeRule.runOnIdle { refreshing.value = true }
        composeRule.waitForIdle()
        assertTrue(container.isRefreshing)

        composeRule.runOnIdle { refreshing.value = false }
        composeRule.waitForIdle()
        assertFalse(container.isRefreshing)
        assertEquals(0, engineView.releaseCount.get())
        assertEquals(0, engineView.destroyCount.get())
    }

    private fun fakeEngine(createView: (Context) -> EngineView): Engine = Proxy.newProxyInstance(
        Engine::class.java.classLoader,
        arrayOf(Engine::class.java),
    ) { _, method, arguments ->
        if (method.name == "createView") {
            createView(arguments?.first() as Context)
        } else {
            null
        }
    } as Engine

    private class RecordingEngineView(
        context: Context,
        private val thumbnail: Bitmap? = null,
        scrollPosition: Float = 0f,
    ) : View(context), EngineView {
        val releaseCount = AtomicInteger()
        val destroyCount = AtomicInteger()

        override val verticalScrollPosition: Flow<Float> = MutableStateFlow(scrollPosition)
        override val verticalScrollDelta: Flow<Float> = emptyFlow()
        override var selectionActionDelegate: SelectionActionDelegate? = null

        override fun render(session: EngineSession) = Unit

        override fun release() {
            releaseCount.incrementAndGet()
        }

        override fun onDestroy() {
            destroyCount.incrementAndGet()
        }

        override fun captureThumbnail(onFinish: (Bitmap?) -> Unit) {
            thumbnail?.let(onFinish)
        }

        override fun setVerticalClipping(height: Int) = Unit

        override fun setDynamicToolbarMaxHeight(height: Int) = Unit

        override fun setActivityContext(context: Context?) = Unit

        override fun addWindowInsetsListener(
            key: String,
            listener: androidx.core.view.OnApplyWindowInsetsListener?,
        ) = Unit

        override fun removeWindowInsetsListener(id: String) = Unit
    }

    private companion object {
        const val REFRESH_SURFACE_TAG = "refresh-browser-surface"
    }
}
