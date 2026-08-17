package com.msgbyte.dao.ui

import android.graphics.Bitmap
import android.view.View
import androidx.core.view.drawToBitmap
import kotlin.coroutines.resume
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import mozilla.components.concept.engine.EngineView

class BrowserThumbnailCapture {
    @Volatile
    private var engineView: EngineView? = null

    fun bind(view: EngineView) {
        engineView = view
    }

    fun unbind(view: EngineView) {
        if (engineView === view) engineView = null
    }

    suspend fun capture(): Bitmap? {
        val view = engineView ?: return null
        return withTimeoutOrNull(CAPTURE_TIMEOUT_MILLIS) {
            suspendCancellableCoroutine { continuation ->
                view.captureThumbnail { bitmap ->
                    if (continuation.isActive) continuation.resume(bitmap)
                }
            }
        }
    }

    private companion object {
        const val CAPTURE_TIMEOUT_MILLIS = 750L
    }
}

internal fun captureViewThumbnail(view: View): Bitmap? {
    if (!view.isLaidOut || view.width <= 0 || view.height <= 0) return null
    return view.drawToBitmap()
}
