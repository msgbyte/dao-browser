package com.msgbyte.dao.browser

import java.net.URL
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AmoCatalogRepositoryTest {
    @Test
    fun searchesOnlyCompatibleAndroidExtensions() = runBlocking {
        var requested: URL? = null
        val repository = AmoCatalogRepository { url ->
            requested = url
            SEARCH_RESPONSE
        }

        val page = repository.search("dark reader", 2, "zh-CN", 153)

        val requestedUrl = requireNotNull(requested)
        assertEquals("https", requestedUrl.protocol)
        assertTrue(requestedUrl.query.contains("app=android"))
        assertTrue(requestedUrl.query.contains("appversion=153.0"))
        assertTrue(requestedUrl.query.contains("q=dark+reader"))
        assertTrue(requestedUrl.query.contains("page=2"))
        assertTrue(requestedUrl.query.contains("lang=zh-CN"))
        assertTrue(requestedUrl.query.contains("type=extension"))
        assertTrue(!requestedUrl.query.contains("sort=recommended"))
        assertEquals("addon@example", page.items.single().guid)
    }

    @Test
    fun usesRecommendedSortOnlyForBlankQueries() = runBlocking {
        var requested: URL? = null
        val repository = AmoCatalogRepository { url ->
            requested = url
            EMPTY_RESPONSE
        }

        repository.search("   ", 1, "en-US", 153)

        val requestedUrl = requireNotNull(requested)
        assertTrue(requestedUrl.query.contains("sort=recommended"))
        assertTrue(!requestedUrl.query.contains("q="))
    }

    @Test
    fun parsesLocalizedPublicHttpsAddonsAndOmitsInvalidEntries() = runBlocking {
        val repository = AmoCatalogRepository { PARSING_RESPONSE }

        val page = repository.search("", 1, "zh-CN", 153)

        val addon = page.items.single()
        assertEquals("addon@example", addon.guid)
        assertEquals("深色阅读器", addon.name)
        assertEquals("English summary", addon.summary)
        assertEquals("Ada", addon.author)
        assertEquals(4.5, addon.rating)
        assertEquals("1.2.3", addon.version)
        assertEquals("https://addons.mozilla.org/en-US/android/addon/dark-reader/", addon.detailUrl)
        assertEquals("https://addons.mozilla.org/firefox/downloads/file/1/dark-reader.xpi", addon.installUrl)
        assertEquals(3, page.nextPage)
    }

    @Test
    fun ignoresNonPositiveNextPages() = runBlocking {
        val repository = AmoCatalogRepository { """{"results": [], "next": "https://addons.mozilla.org/api/v5/addons/search/?page=0"}""" }

        assertNull(repository.search("", 1, "en-US", 153).nextPage)
    }

    @Test
    fun omitsHttpsDownloadsThatAreNotXpiPackages() = runBlocking {
        val repository = AmoCatalogRepository { NON_XPI_RESPONSE }

        assertTrue(repository.search("", 1, "en-US", 153).items.isEmpty())
    }

    private companion object {
        val EMPTY_RESPONSE = """{"results": [], "next": null}"""

        val NON_XPI_RESPONSE = """
            {
              "results": [
                {
                  "id": 47,
                  "guid": "not-an-xpi@example",
                  "current_version": {
                    "version": "1.2.3",
                    "file": { "status": "public", "url": "https://example.invalid/download/not-an-xpi.html" }
                  }
                }
              ],
              "next": null
            }
        """.trimIndent()

        val SEARCH_RESPONSE = """
            {
              "results": [
                {
                  "id": 42,
                  "guid": "addon@example",
                  "slug": "dark-reader",
                  "name": { "en-US": "Dark Reader" },
                  "summary": { "en-US": "Dark mode" },
                  "authors": [{ "name": "Ada" }],
                  "ratings": { "average": 4.5 },
                  "current_version": {
                    "version": "1.2.3",
                    "file": { "status": "public", "url": "https://addons.mozilla.org/firefox/downloads/file/1/dark-reader.xpi" }
                  },
                  "url": "https://addons.mozilla.org/en-US/android/addon/dark-reader/"
                }
              ],
              "next": null
            }
        """.trimIndent()

        val PARSING_RESPONSE = """
            {
              "results": [
                {
                  "id": 42,
                  "guid": "addon@example",
                  "slug": "dark-reader",
                  "name": { "zh-CN": "深色阅读器", "en-US": "Dark Reader" },
                  "summary": { "fr": "", "en-US": "English summary" },
                  "authors": [{ "name": "Ada" }],
                  "ratings": { "average": 4.5 },
                  "current_version": {
                    "version": "1.2.3",
                    "file": { "status": "public", "url": "https://addons.mozilla.org/firefox/downloads/file/1/dark-reader.xpi" }
                  },
                  "url": "https://addons.mozilla.org/en-US/android/addon/dark-reader/"
                },
                {
                  "id": 43,
                  "guid": "",
                  "current_version": { "version": "1", "file": { "status": "public", "url": "https://addons.mozilla.org/a.xpi" } }
                },
                {
                  "id": 44,
                  "guid": "missing-version@example"
                },
                {
                  "id": 45,
                  "guid": "not-public@example",
                  "current_version": { "version": "1", "file": { "status": "disabled", "url": "https://addons.mozilla.org/a.xpi" } }
                },
                {
                  "id": 46,
                  "guid": "http@example",
                  "current_version": { "version": "1", "file": { "status": "public", "url": "http://addons.mozilla.org/a.xpi" } }
                }
              ],
              "next": "https://addons.mozilla.org/api/v5/addons/search/?page=3"
            }
        """.trimIndent()
    }
}
