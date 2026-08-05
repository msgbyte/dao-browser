package com.msgbyte.dao.ui

import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.msgbyte.dao.browser.QrCodeDecoder
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

@Composable
fun QrCameraPreview(
    onDecoded: (String) -> Unit,
    onError: (Throwable) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val previewView = remember { PreviewView(context).apply { scaleType = PreviewView.ScaleType.FILL_CENTER } }
    val analysisExecutor = remember { Executors.newSingleThreadExecutor() }
    val delivered = remember { AtomicBoolean(false) }

    AndroidView(factory = { previewView }, modifier = modifier)

    DisposableEffect(previewView, lifecycleOwner) {
        val providerFuture = ProcessCameraProvider.getInstance(context)
        var provider: ProcessCameraProvider? = null
        providerFuture.addListener(
            {
                runCatching {
                    provider = providerFuture.get()
                    val preview = Preview.Builder().build().also {
                        it.surfaceProvider = previewView.surfaceProvider
                    }
                    val analysis = ImageAnalysis.Builder()
                        .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                        .build()
                    analysis.setAnalyzer(analysisExecutor) { image ->
                        analyze(image)?.let { content ->
                            if (delivered.compareAndSet(false, true)) {
                                ContextCompat.getMainExecutor(context).execute { onDecoded(content) }
                            }
                        }
                    }
                    provider?.unbindAll()
                    provider?.bindToLifecycle(
                        lifecycleOwner,
                        CameraSelector.DEFAULT_BACK_CAMERA,
                        preview,
                        analysis,
                    )
                }.onFailure(onError)
            },
            ContextCompat.getMainExecutor(context),
        )
        onDispose {
            provider?.unbindAll()
            analysisExecutor.shutdown()
        }
    }
}

private fun analyze(image: ImageProxy): String? = try {
    val plane = image.planes.firstOrNull() ?: return null
    val buffer = plane.buffer
    buffer.rewind()
    val raw = ByteArray(buffer.remaining()).also(buffer::get)
    val luminance = ByteArray(image.width * image.height)
    for (row in 0 until image.height) {
        for (column in 0 until image.width) {
            val sourceIndex = row * plane.rowStride + column * plane.pixelStride
            if (sourceIndex < raw.size) {
                luminance[row * image.width + column] = raw[sourceIndex]
            }
        }
    }
    QrCodeDecoder.decode(
        luminance,
        image.width,
        image.height,
        image.imageInfo.rotationDegrees,
    )
} finally {
    image.close()
}
