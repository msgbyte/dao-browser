package com.msgbyte.dao.browser

import android.graphics.Bitmap
import java.net.URI
import kotlinx.coroutines.CancellationException
import mozilla.components.browser.icons.BrowserIcons
import mozilla.components.browser.icons.Icon
import mozilla.components.browser.icons.IconRequest

internal suspend fun BrowserIcons.loadSuggestionFavicon(url: String, isPrivate: Boolean): Bitmap? {
    return try {
        val uri = URI(url)
        if (uri.scheme?.lowercase() !in listOf("http", "https") || uri.host.isNullOrEmpty()) return null

        val request = IconRequest(url = url, isPrivate = isPrivate)
        var icon = loadIcon(request).await()
        if (icon.source == Icon.Source.GENERATOR) {
            // Existing history may predate page-icon discovery.
            val favicon = URI(uri.scheme, null, uri.host, uri.port, "/favicon.ico", null, null)
            icon = loadIcon(request.copy(resources = listOf(
                IconRequest.Resource(favicon.toASCIIString(), IconRequest.Resource.Type.FAVICON),
            ))).await()
        }
        icon.takeUnless { it.source == Icon.Source.GENERATOR }?.bitmap
    } catch (error: CancellationException) {
        throw error
    } catch (_: Exception) {
        null
    }
}
