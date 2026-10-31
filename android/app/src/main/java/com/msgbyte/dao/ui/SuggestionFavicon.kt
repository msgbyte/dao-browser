package com.msgbyte.dao.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.unit.dp
import com.msgbyte.dao.browser.loadSuggestionFavicon
import mozilla.components.browser.icons.BrowserIcons

@Composable
internal fun SuggestionFavicon(
    icons: BrowserIcons,
    url: String,
    isPrivate: Boolean,
    letter: String,
    color: Color,
    modifier: Modifier = Modifier,
) {
    var bitmap by remember(icons, url, isPrivate) { mutableStateOf<Bitmap?>(null) }
    LaunchedEffect(icons, url, isPrivate) {
        bitmap = icons.loadSuggestionFavicon(url, isPrivate)
    }
    val image = bitmap
    if (image == null) {
        LetterAvatar(letter, color, modifier.size(34.dp))
    } else {
        Image(
            bitmap = image.asImageBitmap(),
            contentDescription = null,
            modifier = modifier.size(34.dp).clip(RoundedCornerShape(10.dp)),
        )
    }
}
