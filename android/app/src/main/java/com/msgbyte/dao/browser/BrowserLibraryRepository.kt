package com.msgbyte.dao.browser

import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.database.sqlite.SQLiteDatabase
import java.net.URI
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext

class BrowserLibraryRepository(
    context: Context,
    databaseName: String = DEFAULT_DATABASE_NAME,
    ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val clock: () -> Long = System::currentTimeMillis,
) : AutoCloseable {
    private val database = BrowserDatabase(context, databaseName)
    private val serialIo = ioDispatcher.limitedParallelism(1)
    private var loaded = false
    private val mutableHistory = MutableStateFlow(emptyList<HistoryVisit>())
    private val mutableBookmarks = MutableStateFlow(emptyList<BookmarkEntry>())
    private val mutableFolders = MutableStateFlow(emptyList<BookmarkFolder>())

    val history: StateFlow<List<HistoryVisit>> = mutableHistory.asStateFlow()
    val bookmarks: StateFlow<List<BookmarkEntry>> = mutableBookmarks.asStateFlow()
    val folders: StateFlow<List<BookmarkFolder>> = mutableFolders.asStateFlow()

    suspend fun load() = withLibrary { Unit }

    private suspend fun <T> withLibrary(block: () -> T): T = withContext(serialIo) {
        if (!loaded) {
            mutableHistory.value = readHistory()
            mutableBookmarks.value = readBookmarks()
            mutableFolders.value = readFolders()
            loaded = true
        }
        block()
    }

    suspend fun recordVisit(url: String, title: String, visitedAt: Long = clock()) {
        if (!isStorableUrl(url)) return
        withLibrary {
            val visitTitle = displayTitle(url, title)
            val values = ContentValues().apply {
                put("url", url)
                put("title", visitTitle)
                put("visited_at", visitedAt)
            }
            val id = database.writableDatabase.insertOrThrow("history_visits", null, values)
            val visit = HistoryVisit(id, url, visitTitle, visitedAt)
            val previous = mutableHistory.value
            // The newest row id comes first when two visits have the same timestamp.
            val index = previous.indexOfFirst { it.visitedAt <= visitedAt }
            mutableHistory.value = buildList(previous.size + 1) {
                addAll(previous)
                add(if (index < 0) size else index, visit)
            }
        }
    }

    suspend fun toggleBookmark(url: String, title: String): BookmarkToggleResult {
        require(isStorableUrl(url)) { "Only HTTP and HTTPS pages can be bookmarked" }
        return withLibrary {
            val existingId = findBookmarkId(url, BookmarkKind.FAVORITE)
            if (existingId != null) {
                database.writableDatabase.delete("bookmarks", "id = ?", arrayOf(existingId.toString()))
                mutableBookmarks.value = readBookmarks()
                BookmarkToggleResult.REMOVED
            } else {
                val now = clock()
                val values = ContentValues().apply {
                    put("url", url)
                    put("title", displayTitle(url, title))
                    putNull("folder_id")
                    put("kind", BookmarkKind.FAVORITE.name)
                    put("created_at", now)
                    put("updated_at", now)
                }
                database.writableDatabase.insertOrThrow("bookmarks", null, values)
                mutableBookmarks.value = readBookmarks()
                BookmarkToggleResult.ADDED
            }
        }
    }

    suspend fun addBookmark(
        url: String,
        title: String,
        folderId: Long? = null,
        kind: BookmarkKind = BookmarkKind.FAVORITE,
    ): BookmarkEntry {
        require(isStorableUrl(url)) { "Only HTTP and HTTPS pages can be bookmarked" }
        return withLibrary {
            val now = clock()
            val values = ContentValues().apply {
                put("url", url)
                put("title", displayTitle(url, title))
                if (folderId == null) putNull("folder_id") else put("folder_id", folderId)
                put("kind", kind.name)
                put("created_at", now)
                put("updated_at", now)
            }
            val id = database.writableDatabase.insertOrThrow("bookmarks", null, values)
            mutableBookmarks.value = readBookmarks()
            mutableBookmarks.value.first { it.id == id }
        }
    }

    suspend fun createFolder(title: String): BookmarkFolder {
        val normalizedTitle = title.trim()
        require(normalizedTitle.isNotEmpty()) { "Folder title cannot be empty" }
        return withLibrary {
            val createdAt = clock()
            val values = ContentValues().apply {
                put("title", normalizedTitle)
                put("created_at", createdAt)
            }
            val id = database.writableDatabase.insertOrThrow("bookmark_folders", null, values)
            mutableFolders.value = readFolders()
            mutableFolders.value.first { it.id == id }
        }
    }

    suspend fun updateBookmark(
        id: Long,
        title: String,
        folderId: Long?,
        kind: BookmarkKind,
    ) {
        val normalizedTitle = title.trim()
        require(normalizedTitle.isNotEmpty()) { "Bookmark title cannot be empty" }
        withLibrary {
            val values = ContentValues().apply {
                put("title", normalizedTitle)
                if (folderId == null) putNull("folder_id") else put("folder_id", folderId)
                put("kind", kind.name)
                put("updated_at", clock())
            }
            val db = database.writableDatabase
            val args = arrayOf(id.toString())
            val updated = db.updateWithOnConflict("bookmarks", values, "id = ?", args, SQLiteDatabase.CONFLICT_IGNORE)
            // The target list already holds this URL (UNIQUE(url, kind)): keep that entry, with its
            // own title and folder, and drop the moved duplicate. A vanished id deletes nothing.
            if (updated == 0) db.delete("bookmarks", "id = ?", args)
            mutableBookmarks.value = readBookmarks()
        }
    }

    suspend fun deleteBookmark(id: Long) {
        withLibrary {
            database.writableDatabase.delete("bookmarks", "id = ?", arrayOf(id.toString()))
            mutableBookmarks.value = readBookmarks()
        }
    }

    suspend fun deleteFolder(id: Long) {
        withLibrary {
            database.writableDatabase.delete("bookmark_folders", "id = ?", arrayOf(id.toString()))
            mutableFolders.value = readFolders()
            mutableBookmarks.value = readBookmarks()
        }
    }

    suspend fun deleteHistoryVisit(id: Long) {
        withLibrary {
            database.writableDatabase.delete("history_visits", "id = ?", arrayOf(id.toString()))
            mutableHistory.value = mutableHistory.value.filterNot { it.id == id }
        }
    }

    suspend fun clearHistory() {
        withLibrary {
            database.writableDatabase.delete("history_visits", null, null)
            mutableHistory.value = emptyList()
        }
    }

    fun isBookmarked(url: String): Boolean = bookmarks.value.any {
        it.url == url && it.kind == BookmarkKind.FAVORITE
    }

    override fun close() {
        database.close()
    }

    private fun readHistory(): List<HistoryVisit> {
        return database.readableDatabase.query(
            "history_visits",
            arrayOf("id", "url", "title", "visited_at"),
            null,
            null,
            null,
            null,
            "visited_at DESC, id DESC",
        ).use { cursor ->
            buildList {
                while (cursor.moveToNext()) add(cursor.toHistoryVisit())
            }
        }
    }

    private fun readBookmarks(): List<BookmarkEntry> {
        return database.readableDatabase.query(
            "bookmarks",
            arrayOf("id", "url", "title", "folder_id", "kind", "created_at", "updated_at"),
            null,
            null,
            null,
            null,
            "updated_at DESC, id DESC",
        ).use { cursor ->
            buildList {
                while (cursor.moveToNext()) add(cursor.toBookmarkEntry())
            }
        }
    }

    private fun readFolders(): List<BookmarkFolder> {
        return database.readableDatabase.query(
            "bookmark_folders",
            arrayOf("id", "title", "created_at"),
            null,
            null,
            null,
            null,
            "created_at ASC, id ASC",
        ).use { cursor ->
            buildList {
                while (cursor.moveToNext()) {
                    add(
                        BookmarkFolder(
                            id = cursor.getLong(cursor.getColumnIndexOrThrow("id")),
                            title = cursor.getString(cursor.getColumnIndexOrThrow("title")),
                            createdAt = cursor.getLong(cursor.getColumnIndexOrThrow("created_at")),
                        ),
                    )
                }
            }
        }
    }

    private fun findBookmarkId(url: String, kind: BookmarkKind): Long? {
        return database.readableDatabase.query(
            "bookmarks",
            arrayOf("id"),
            "url = ? AND kind = ?",
            arrayOf(url, kind.name),
            null,
            null,
            null,
            "1",
        ).use { cursor -> if (cursor.moveToFirst()) cursor.getLong(0) else null }
    }

    private fun Cursor.toHistoryVisit() = HistoryVisit(
        id = getLong(getColumnIndexOrThrow("id")),
        url = getString(getColumnIndexOrThrow("url")),
        title = getString(getColumnIndexOrThrow("title")),
        visitedAt = getLong(getColumnIndexOrThrow("visited_at")),
    )

    private fun Cursor.toBookmarkEntry() = BookmarkEntry(
        id = getLong(getColumnIndexOrThrow("id")),
        url = getString(getColumnIndexOrThrow("url")),
        title = getString(getColumnIndexOrThrow("title")),
        folderId = getColumnIndexOrThrow("folder_id").let { index ->
            if (isNull(index)) null else getLong(index)
        },
        kind = BookmarkKind.valueOf(getString(getColumnIndexOrThrow("kind"))),
        createdAt = getLong(getColumnIndexOrThrow("created_at")),
        updatedAt = getLong(getColumnIndexOrThrow("updated_at")),
    )

    private fun isStorableUrl(url: String): Boolean {
        return runCatching { URI(url).scheme?.lowercase() in STORABLE_SCHEMES }.getOrDefault(false)
    }

    private fun displayTitle(url: String, title: String): String {
        return title.trim().ifEmpty {
            runCatching { URI(url).host }.getOrNull().orEmpty().ifEmpty { url }
        }
    }

    private companion object {
        const val DEFAULT_DATABASE_NAME = "dao-browser-library.db"
        val STORABLE_SCHEMES = setOf("http", "https")
    }
}
