package com.msgbyte.dao.ui

import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.composables.icons.lucide.Copy
import com.composables.icons.lucide.Download
import com.composables.icons.lucide.Eye
import com.composables.icons.lucide.Lucide
import com.composables.icons.lucide.SquarePlus
import com.composables.icons.lucide.X
import com.msgbyte.dao.R
import com.msgbyte.dao.browser.BrowserTabsController
import com.msgbyte.dao.browser.PageImageActions
import com.msgbyte.dao.browser.imageSrc
import com.msgbyte.dao.browser.isHttpUrl
import com.msgbyte.dao.ui.theme.LocalNovaColors
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.TabSessionState

/** Long-press menu for page images: download, copy, preview, or open in a new tab. */
@Composable
internal fun PageImageContextMenu(
    tab: TabSessionState?,
    controller: BrowserTabsController,
    pageImages: PageImageActions,
) {
    val context = LocalContext.current
    var menuSrc by remember { mutableStateOf<String?>(null) }
    var previewSrc by remember { mutableStateOf<String?>(null) }
    val hitResult = tab?.content?.hitResult
    val private = tab?.content?.private == true

    LaunchedEffect(tab?.id, hitResult) {
        if (tab == null || hitResult == null) return@LaunchedEffect
        controller.consumeHitResult(tab.id)
        hitResult.imageSrc?.let { menuSrc = it }
    }

    fun toast(message: Int) = Toast.makeText(context.applicationContext, message, Toast.LENGTH_SHORT).show()

    fun launchAction(failureMessage: Int, action: suspend () -> Unit) {
        // Outlives this screen, so opening Downloads right away does not cancel a started download.
        @OptIn(DelicateCoroutinesApi::class)
        GlobalScope.launch(Dispatchers.Main) {
            try {
                action()
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                toast(failureMessage)
            }
        }
    }

    menuSrc?.let { src ->
        ImageContextMenuSheet(
            imageUrl = src,
            actions = listOf(
                ImageMenuAction(Lucide.Download, R.string.image_download) {
                    launchAction(R.string.image_download_failed) {
                        pageImages.download(src, private)
                        toast(R.string.image_download_started)
                    }
                },
                ImageMenuAction(Lucide.Copy, R.string.image_copy) {
                    launchAction(R.string.image_copy_failed) {
                        pageImages.copy(src, private)
                        // Android 13+ confirms clipboard writes itself.
                        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) toast(R.string.image_copied)
                    }
                },
                ImageMenuAction(Lucide.Eye, R.string.image_preview) { previewSrc = src },
                // blob: images only load inside their page, and a data: URL would bloat the saved session.
                ImageMenuAction(Lucide.SquarePlus, R.string.image_open_in_new_tab) {
                    controller.createTab(url = src, select = false)
                    toast(R.string.image_opened_in_new_tab)
                }.takeIf { isHttpUrl(src) },
            ).filterNotNull(),
            onDismiss = { menuSrc = null },
        )
    }

    previewSrc?.let { src ->
        ImagePreviewDialog(
            load = { pageImages.loadPreview(src, private).asImageBitmap() },
            onFailure = {
                previewSrc = null
                toast(R.string.image_preview_failed)
            },
            onDismiss = { previewSrc = null },
        )
    }
}

private class ImageMenuAction(val icon: ImageVector, val labelRes: Int, val onClick: () -> Unit)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ImageContextMenuSheet(
    imageUrl: String,
    actions: List<ImageMenuAction>,
    onDismiss: () -> Unit,
) {
    val colors = LocalNovaColors.current
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        containerColor = colors.surface,
        contentColor = colors.foreground,
        shape = RoundedCornerShape(topStart = 24.dp, topEnd = 24.dp),
    ) {
        Column(Modifier.fillMaxWidth().padding(bottom = 24.dp)) {
            Text(
                // data: URLs can hold a whole image; the header only needs their start.
                text = imageUrl.take(300),
                modifier = Modifier.padding(horizontal = 24.dp).padding(bottom = 8.dp),
                color = colors.muted,
                fontSize = 13.sp,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            actions.forEach { action ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            onDismiss()
                            action.onClick()
                        }
                        .padding(horizontal = 24.dp, vertical = 14.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Icon(action.icon, null, tint = colors.muted, modifier = Modifier.size(21.dp))
                    Spacer(Modifier.width(16.dp))
                    Text(
                        text = stringResource(action.labelRes),
                        color = colors.foreground,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Medium,
                    )
                }
            }
        }
    }
}

@Composable
private fun ImagePreviewDialog(
    load: suspend () -> ImageBitmap,
    onFailure: () -> Unit,
    onDismiss: () -> Unit,
) {
    var image by remember { mutableStateOf<ImageBitmap?>(null) }
    var scale by remember { mutableFloatStateOf(1f) }
    var offset by remember { mutableStateOf(Offset.Zero) }

    LaunchedEffect(Unit) {
        try {
            image = load()
        } catch (error: CancellationException) {
            throw error
        } catch (error: Exception) {
            onFailure()
        }
    }

    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.fillMaxSize().background(Color.Black), contentAlignment = Alignment.Center) {
            val loaded = image
            if (loaded == null) {
                CircularProgressIndicator(color = Color.White)
            } else {
                Image(
                    bitmap = loaded,
                    contentDescription = stringResource(R.string.image_preview),
                    modifier = Modifier
                        .fillMaxSize()
                        .pointerInput(Unit) {
                            detectTransformGestures { _, pan, zoom, _ ->
                                scale = (scale * zoom).coerceIn(1f, 5f)
                                offset = if (scale == 1f) Offset.Zero else offset + pan
                            }
                        }
                        .graphicsLayer {
                            scaleX = scale
                            scaleY = scale
                            translationX = offset.x
                            translationY = offset.y
                        },
                    contentScale = ContentScale.Fit,
                )
            }
            IconButton(
                onClick = onDismiss,
                modifier = Modifier
                    .align(Alignment.TopEnd)
                    .windowInsetsPadding(WindowInsets.safeDrawing)
                    .padding(8.dp),
            ) {
                Icon(Lucide.X, stringResource(R.string.close_image_preview), tint = Color.White)
            }
        }
    }
}
