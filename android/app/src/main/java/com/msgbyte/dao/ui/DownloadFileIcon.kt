package com.msgbyte.dao.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.net.Uri
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import com.composables.icons.lucide.FileImage
import com.composables.icons.lucide.Lucide
import com.msgbyte.dao.browser.BrowserDownload
import com.msgbyte.dao.browser.DownloadStatus
import com.msgbyte.dao.ui.theme.LocalNovaColors
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@Composable
internal fun DownloadFileIcon(download: BrowserDownload) {
    val colors = LocalNovaColors.current
    val context = LocalContext.current
    val size = with(LocalDensity.current) { 44.dp.roundToPx() }.coerceIn(1, 256)
    val icon = downloadFileIcon(download.request.fileName, download.request.contentType)
    val uri = download.localUri.takeIf {
        download.status == DownloadStatus.SUCCESSFUL && icon == Lucide.FileImage
    }
    var thumbnail by remember(download.id, uri, download.lastModified, size) { mutableStateOf<Bitmap?>(null) }
    LaunchedEffect(context, download.id, uri, download.lastModified, size) {
        thumbnail = uri?.let { loadDownloadThumbnail(context, it, size) }
    }
    val shape = RoundedCornerShape(11.dp)
    Box(
        Modifier.size(44.dp).clip(shape).background(colors.surfaceSecondary).border(1.dp, colors.border, shape),
        contentAlignment = Alignment.Center,
    ) {
        val image = thumbnail
        if (image == null) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(24.dp), tint = colors.muted)
        } else {
            Image(
                bitmap = image.asImageBitmap(),
                contentDescription = null,
                modifier = Modifier.matchParentSize(),
                contentScale = ContentScale.Crop,
            )
        }
    }
}

internal suspend fun loadDownloadThumbnail(context: Context, localUri: String, size: Int): Bitmap? =
    withContext(Dispatchers.IO) {
        val uri = Uri.parse(localUri)
        if (uri.scheme != "content" || size !in 1..256) return@withContext null
        val resolver = context.contentResolver
        try {
            val options = BitmapFactory.Options().apply {
                inJustDecodeBounds = true
                inSampleSize = 1
            }
            resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
            if (options.outWidth <= 0 || options.outHeight <= 0) return@withContext null
            // Bound both dimensions before allocating pixels, including very wide or tall images.
            while (maxOf(options.outWidth, options.outHeight).toLong() > size.toLong() * options.inSampleSize) {
                if (options.inSampleSize > Int.MAX_VALUE / 2) return@withContext null
                options.inSampleSize *= 2
            }
            options.inJustDecodeBounds = false
            val bitmap = resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
                ?: return@withContext null
            val orientation = try {
                resolver.openInputStream(uri)?.use {
                    ExifInterface(it).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)
                }
            } catch (_: IOException) {
                null
            }
            val matrix = Matrix()
            when (orientation) {
                ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> matrix.setScale(-1f, 1f)
                ExifInterface.ORIENTATION_ROTATE_180 -> matrix.setRotate(180f)
                ExifInterface.ORIENTATION_FLIP_VERTICAL -> matrix.setScale(1f, -1f)
                ExifInterface.ORIENTATION_TRANSPOSE -> { matrix.setRotate(90f); matrix.postScale(-1f, 1f) }
                ExifInterface.ORIENTATION_ROTATE_90 -> matrix.setRotate(90f)
                ExifInterface.ORIENTATION_TRANSVERSE -> { matrix.setRotate(270f); matrix.postScale(-1f, 1f) }
                ExifInterface.ORIENTATION_ROTATE_270 -> matrix.setRotate(270f)
                else -> return@withContext bitmap
            }
            Bitmap.createBitmap(bitmap, 0, 0, bitmap.width, bitmap.height, matrix, true).also {
                if (it !== bitmap) bitmap.recycle()
            }
        } catch (_: IOException) {
            null
        } catch (_: SecurityException) {
            null
        } catch (_: IllegalArgumentException) {
            null
        }
    }
