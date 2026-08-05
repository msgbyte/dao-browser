package com.msgbyte.dao.browser

import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

data class AmoAddon(
    val id: Int,
    val guid: String,
    val slug: String,
    val name: String,
    val summary: String,
    val author: String,
    val iconUrl: String?,
    val rating: Double?,
    val version: String,
    val detailUrl: String,
    val installUrl: String,
)

data class AmoCatalogPage(
    val items: List<AmoAddon>,
    val nextPage: Int?,
)

interface AmoCatalogSource {
    suspend fun search(query: String, page: Int, locale: String, geckoMajor: Int): AmoCatalogPage
}

class AmoCatalogRepository(
    private val fetch: suspend (URL) -> String = { url -> fetchJson(url) },
) : AmoCatalogSource {
    override suspend fun search(
        query: String,
        page: Int,
        locale: String,
        geckoMajor: Int,
    ): AmoCatalogPage {
        val url = buildSearchUrl(query, page, locale, geckoMajor)
        return parsePage(fetch(url), locale)
    }

    private fun buildSearchUrl(
        query: String,
        page: Int,
        locale: String,
        geckoMajor: Int,
    ): URL {
        val trimmedQuery = query.trim()
        val parameters = mutableListOf(
            "page=${page.coerceAtLeast(1)}",
            "lang=${encode(locale)}",
            "app=android",
            "appversion=${geckoMajor}.0",
            "type=extension",
        )
        if (trimmedQuery.isBlank()) {
            parameters += "sort=recommended"
        } else {
            parameters += "q=${encode(trimmedQuery)}"
        }
        return URL("https://addons.mozilla.org/api/v5/addons/search/?${parameters.joinToString("&")}")
    }

    private fun parsePage(body: String, locale: String): AmoCatalogPage {
        val response = JSONObject(body)
        val items = buildList {
            val results = response.optJSONArray("results") ?: JSONArray()
            for (index in 0 until results.length()) {
                val addon = results.optJSONObject(index) ?: continue
                parseAddon(addon, locale)?.let(::add)
            }
        }
        return AmoCatalogPage(items, parseNextPage(response.optString("next")))
    }

    private fun parseAddon(addon: JSONObject, locale: String): AmoAddon? {
        val guid = addon.optString("guid").trim()
        val currentVersion = addon.optJSONObject("current_version") ?: return null
        val version = currentVersion.optString("version").trim()
        val file = currentVersion.optJSONObject("file") ?: return null
        val installUrl = file.optString("url").trim()
        if (
            guid.isBlank() ||
            version.isBlank() ||
            file.optString("status") != "public" ||
            !isHttpsXpiUrl(installUrl)
        ) {
            return null
        }
        val authors = addon.optJSONArray("authors") ?: JSONArray()
        val firstAuthor = authors.optJSONObject(0)?.optString("name").orEmpty()
        val rating = addon.optJSONObject("ratings")
            ?.takeIf { it.has("average") && !it.isNull("average") }
            ?.optDouble("average")
        return AmoAddon(
            id = addon.optInt("id"),
            guid = guid,
            slug = addon.optString("slug"),
            name = localizedString(addon.opt("name"), locale),
            summary = localizedString(addon.opt("summary"), locale),
            author = firstAuthor,
            iconUrl = addon.optString("icon_url").takeIf(String::isNotBlank),
            rating = rating,
            version = version,
            detailUrl = addon.optString("url"),
            installUrl = installUrl,
        )
    }

    private fun localizedString(value: Any?, locale: String): String {
        if (value is String) return value.takeIf(String::isNotBlank).orEmpty()
        val translated = value as? JSONObject ?: return ""
        translated.optString(locale).takeIf(String::isNotBlank)?.let { return it }
        translated.optString("en-US").takeIf(String::isNotBlank)?.let { return it }
        return translated.keys().asSequence()
            .map { translated.optString(it) }
            .firstOrNull(String::isNotBlank)
            .orEmpty()
    }

    private fun parseNextPage(next: String): Int? {
        if (next.isBlank()) return null
        val page = runCatching {
            URL(next).query.split("&")
                .firstOrNull { it.substringBefore('=') == "page" }
                ?.substringAfter('=')
                ?.toIntOrNull()
        }.getOrNull()
        return page?.takeIf { it > 0 }
    }

    private fun isHttpsXpiUrl(value: String): Boolean = runCatching {
        URL(value)
    }.getOrNull()?.let { url ->
        url.protocol == "https" &&
            url.host.isNotBlank() &&
            url.path.endsWith(".xpi")
    } == true

    private fun encode(value: String): String = URLEncoder.encode(value, StandardCharsets.UTF_8.name())

    private companion object {
        suspend fun fetchJson(url: URL): String = withContext(Dispatchers.IO) {
            val connection = url.openConnection() as HttpURLConnection
            try {
                connection.connectTimeout = 10_000
                connection.readTimeout = 10_000
                connection.setRequestProperty("Accept", "application/json")
                connection.inputStream.bufferedReader().use { it.readText() }
            } finally {
                connection.disconnect()
            }
        }
    }
}
