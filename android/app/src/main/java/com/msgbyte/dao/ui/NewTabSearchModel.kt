package com.msgbyte.dao.ui

import androidx.annotation.ColorInt
import com.msgbyte.dao.browser.BookmarkEntry
import com.msgbyte.dao.browser.HistoryVisit
import java.net.URI

enum class SuggestionType {
    Site,
    Search,
}

data class SearchSuggestion(
    val title: String,
    val subtitle: String,
    val type: SuggestionType,
    val letter: String = "",
    @param:ColorInt val avatarColor: Int = 0,
    val target: String = subtitle,
)

fun searchSuggestions(
    query: String,
    pool: List<SearchSuggestion>,
): List<SearchSuggestion> {
    val normalizedQuery = query.trim()
    if (normalizedQuery.isEmpty()) {
        return pool.filter { it.type == SuggestionType.Site }.take(3)
    }
    return pool.filter {
        (it.title + " " + it.subtitle).contains(normalizedQuery, ignoreCase = true)
    }.take(5)
}

fun buildLibrarySuggestions(
    query: String,
    bookmarks: List<BookmarkEntry>,
    history: List<HistoryVisit>,
): List<SearchSuggestion> {
    val normalizedQuery = query.trim()
    val entries = if (normalizedQuery.isEmpty()) {
        sequence {
            history.forEach { yield(it.url to it.title) }
            bookmarks.forEach { yield(it.url to it.title) }
        }
    } else {
        sequence {
            bookmarks.forEach { yield(it.url to it.title) }
            history.forEach { yield(it.url to it.title) }
        }.filter { (url, title) ->
            "$title $url".contains(normalizedQuery, ignoreCase = true)
        }
    }
    val limit = if (normalizedQuery.isEmpty()) 3 else 5
    return entries
        .distinctBy { it.first }
        .take(limit)
        .map { (url, title) ->
            SearchSuggestion(
                title = title.ifBlank { url },
                subtitle = url,
                type = SuggestionType.Site,
                letter = siteLetter(url, title),
                avatarColor = 0xFF3F424A.toInt(),
                target = url,
            )
        }
        .toList()
}

private fun siteLetter(url: String, title: String): String {
    return title.trim().firstOrNull()?.uppercase()
        ?: runCatching { URI(url).host?.firstOrNull()?.uppercase() }.getOrNull()
        ?: "?"
}
