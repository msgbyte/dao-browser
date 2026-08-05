package com.msgbyte.dao.ui

import com.msgbyte.dao.browser.BookmarkEntry
import com.msgbyte.dao.browser.BookmarkKind
import com.msgbyte.dao.browser.HistoryVisit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NewTabSearchModelTest {
    private val pool = listOf(
        SearchSuggestion("GitHub", "github.com", SuggestionType.Site),
        SearchSuggestion("Arc", "arc.net", SuggestionType.Site),
        SearchSuggestion("Vercel", "vercel.com", SuggestionType.Site),
        SearchSuggestion("Figma Community", "figma.com/community", SuggestionType.Site),
        SearchSuggestion("figma shortcuts", "Search suggestion", SuggestionType.Search),
    )

    @Test
    fun blankQueryReturnsTheThreeRecentSites() {
        val results = searchSuggestions("", pool)

        assertEquals(listOf("GitHub", "Arc", "Vercel"), results.map { it.title })
    }

    @Test
    fun queryFiltersTitleAndSubtitleIgnoringCaseAndLimitsResults() {
        val results = searchSuggestions("FIGMA", pool)

        assertEquals(listOf("Figma Community", "figma shortcuts"), results.map { it.title })
        assertTrue(results.size <= 5)
    }

    @Test
    fun unmatchedQueryReturnsNoSiteSuggestions() {
        assertTrue(searchSuggestions("missing", pool).isEmpty())
    }

    @Test
    fun blankQueryUsesRecentDistinctHistoryBeforeBookmarks() {
        val history = listOf(
            HistoryVisit(3, "https://recent.example", "Recent", 30),
            HistoryVisit(2, "https://recent.example", "Older duplicate", 20),
            HistoryVisit(1, "https://docs.example", "Docs", 10),
        )
        val bookmarks = listOf(
            bookmark(1, "https://bookmarked.example", "Bookmarked"),
        )

        val results = buildLibrarySuggestions("", bookmarks, history)

        assertEquals(
            listOf("https://recent.example", "https://docs.example", "https://bookmarked.example"),
            results.map { it.target },
        )
    }

    @Test
    fun queryCombinesBookmarksAndHistoryWithoutDuplicateUrls() {
        val bookmarks = listOf(
            bookmark(1, "https://example.com", "Example bookmark"),
        )
        val history = listOf(
            HistoryVisit(2, "https://example.com", "Example visit", 20),
            HistoryVisit(1, "https://docs.example.org", "Documentation", 10),
        )

        val results = buildLibrarySuggestions("exa", bookmarks, history)

        assertEquals(
            listOf("https://example.com", "https://docs.example.org"),
            results.map { it.target },
        )
    }

    private fun bookmark(id: Long, url: String, title: String) = BookmarkEntry(
        id = id,
        url = url,
        title = title,
        folderId = null,
        kind = BookmarkKind.FAVORITE,
        createdAt = id,
        updatedAt = id,
    )
}
