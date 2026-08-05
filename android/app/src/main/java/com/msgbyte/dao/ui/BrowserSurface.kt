package com.msgbyte.dao.ui

import android.view.ViewGroup
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout
import com.msgbyte.dao.R
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineView

@Composable
fun BrowserSurface(
    engine: Engine,
    session: EngineSession,
    modifier: Modifier = Modifier,
    thumbnailCapture: BrowserThumbnailCapture? = null,
    refreshing: Boolean = false,
    onRefresh: () -> Unit = {},
) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val lifecycleBridge = remember { mutableStateOf<EngineViewLifecycle?>(null) }
    val hostedEngineView = remember { mutableStateOf<EngineView?>(null) }
    val verticalScrollPosition = remember { mutableStateOf(0f) }

    LaunchedEffect(hostedEngineView.value, session) {
        verticalScrollPosition.value = 0f
        hostedEngineView.value?.verticalScrollPosition?.collect { position ->
            verticalScrollPosition.value = position
        }
    }

    AndroidView(
        modifier = modifier,
        factory = { context ->
            val engineView = engine.createView(context)
            engineView.render(session)
            thumbnailCapture?.bind(engineView)
            hostedEngineView.value = engineView
            lifecycleBridge.value = EngineViewLifecycle(engineView)
            val hostedView = engineView.asView().apply {
                id = R.id.browser_engine_view
            }
            SwipeRefreshLayout(context).apply {
                setOnChildScrollUpCallback { _, _ ->
                    verticalScrollPosition.value > 0f || hostedView.canScrollVertically(-1)
                }
                setOnRefreshListener(onRefresh)
                addView(
                    hostedView,
                    ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT,
                    ),
                )
            }
        },
        update = { refreshLayout ->
            refreshLayout.setOnRefreshListener(onRefresh)
            refreshLayout.isRefreshing = refreshing
            hostedEngineView.value?.let { engineView ->
                engineView.render(session)
                thumbnailCapture?.bind(engineView)
            }
        },
    )

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            lifecycleBridge.value?.onEvent(event)
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            hostedEngineView.value?.let { thumbnailCapture?.unbind(it) }
            lifecycleBridge.value?.dispose()
        }
    }
}
