package com.msgbyte.dao.browser

import java.net.URLEncoder
import java.nio.charset.StandardCharsets

class NavigationTargetResolver(
    private val searchUrl: String,
) {
    fun resolve(input: String): String? {
        val value = input.trim()
        if (value.isEmpty()) return null

        if (
            value.startsWith("http://", ignoreCase = true) ||
            value.startsWith("https://", ignoreCase = true)
        ) {
            return value
        }

        if (value.equals("about:blank", ignoreCase = true) ||
            value.startsWith("about:blank#", ignoreCase = true)
        ) {
            return value
        }

        val isDomain = !value.contains(' ') && value.contains('.')
        if (isDomain) return "https://$value"

        val query = URLEncoder.encode(value, StandardCharsets.UTF_8.name())
        return searchUrl.format(query)
    }
}
