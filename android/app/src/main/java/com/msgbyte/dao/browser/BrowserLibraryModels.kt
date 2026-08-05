package com.msgbyte.dao.browser

enum class BookmarkKind {
    FAVORITE,
    READING_LIST,
}

enum class BookmarkToggleResult {
    ADDED,
    REMOVED,
}

data class HistoryVisit(
    val id: Long,
    val url: String,
    val title: String,
    val visitedAt: Long,
)

data class BookmarkFolder(
    val id: Long,
    val title: String,
    val createdAt: Long,
)

data class BookmarkEntry(
    val id: Long,
    val url: String,
    val title: String,
    val folderId: Long?,
    val kind: BookmarkKind,
    val createdAt: Long,
    val updatedAt: Long,
)
