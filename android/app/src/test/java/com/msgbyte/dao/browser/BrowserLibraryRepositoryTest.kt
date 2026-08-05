package com.msgbyte.dao.browser

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import java.util.UUID
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class BrowserLibraryRepositoryTest {
    private lateinit var context: Context
    private lateinit var databaseName: String
    private lateinit var repository: BrowserLibraryRepository

    @Before
    fun setUp() {
        context = ApplicationProvider.getApplicationContext()
        databaseName = "browser-library-${UUID.randomUUID()}.db"
        repository = BrowserLibraryRepository(context, databaseName)
    }

    @After
    fun tearDown() {
        repository.close()
        context.deleteDatabase(databaseName)
    }

    @Test
    fun completedVisitsArePersistedNewestFirst() = runBlocking {
        repository.recordVisit("https://example.com/one", "One", 10)
        repository.recordVisit("https://example.com/two", "Two", 20)

        assertEquals(listOf("Two", "One"), repository.history.value.map { it.title })
    }

    @Test
    fun togglingTheSameUrlAddsThenRemovesItsBookmark() = runBlocking {
        assertEquals(
            BookmarkToggleResult.ADDED,
            repository.toggleBookmark("https://example.com", "Example"),
        )
        assertEquals(
            BookmarkToggleResult.REMOVED,
            repository.toggleBookmark("https://example.com", "Example"),
        )

        assertTrue(repository.bookmarks.value.isEmpty())
    }

    @Test
    fun libraryDataSurvivesRepositoryRestart() = runBlocking {
        repository.recordVisit("https://example.com", "Example", 10)
        repository.addBookmark("https://example.com", "Example")
        repository.close()

        repository = BrowserLibraryRepository(context, databaseName)

        assertEquals(listOf("https://example.com"), repository.history.value.map { it.url })
        assertEquals(listOf("https://example.com"), repository.bookmarks.value.map { it.url })
    }

    @Test
    fun bookmarkCanMoveToFolderAndReadingList() = runBlocking {
        val folder = repository.createFolder("Research")
        val bookmark = repository.addBookmark("https://example.com", "Example")

        repository.updateBookmark(
            id = bookmark.id,
            title = "Updated",
            folderId = folder.id,
            kind = BookmarkKind.READING_LIST,
        )

        assertEquals(listOf("Research"), repository.folders.value.map { it.title })
        assertEquals("Updated", repository.bookmarks.value.single().title)
        assertEquals(folder.id, repository.bookmarks.value.single().folderId)
        assertEquals(BookmarkKind.READING_LIST, repository.bookmarks.value.single().kind)
    }

    @Test
    fun historyCanDeleteOneVisitAndThenClearEverything() = runBlocking {
        repository.recordVisit("https://example.com/one", "One", 10)
        repository.recordVisit("https://example.com/two", "Two", 20)

        repository.deleteHistoryVisit(repository.history.value.first().id)
        assertEquals(listOf("One"), repository.history.value.map { it.title })

        repository.clearHistory()
        assertTrue(repository.history.value.isEmpty())
    }

    @Test
    fun unsupportedSchemesAreNotStoredAsHistory() = runBlocking {
        repository.recordVisit("about:blank", "Blank", 10)
        repository.recordVisit("file:///tmp/example.html", "File", 20)

        assertTrue(repository.history.value.isEmpty())
    }

    @Test
    fun bookmarkAndFolderDeletionRefreshesTheLibrary() = runBlocking {
        val folder = repository.createFolder("Research")
        val first = repository.addBookmark("https://one.example", "One", folder.id)
        repository.addBookmark("https://two.example", "Two", folder.id)

        repository.deleteBookmark(first.id)
        repository.deleteFolder(folder.id)

        assertEquals(listOf("Two"), repository.bookmarks.value.map { it.title })
        assertEquals(null, repository.bookmarks.value.single().folderId)
        assertTrue(repository.folders.value.isEmpty())
    }
}
