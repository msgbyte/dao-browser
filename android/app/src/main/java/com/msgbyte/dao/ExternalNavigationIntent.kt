package com.msgbyte.dao

import android.content.Intent

internal fun Intent.httpNavigationUrl(): String? {
    if (action != Intent.ACTION_VIEW) return null
    val navigationUri = data ?: return null
    val isHttp = navigationUri.scheme.equals("http", ignoreCase = true)
    val isHttps = navigationUri.scheme.equals("https", ignoreCase = true)
    if ((!isHttp && !isHttps) || navigationUri.host.isNullOrBlank()) return null
    return navigationUri.toString()
}
