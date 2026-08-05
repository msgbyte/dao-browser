package com.msgbyte.dao.browser

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class AmoStoreViewModelTest {
    @Test
    fun loadsRecommendedAddonsOnCreation() = runTest {
        val source = FakeAmoSource().apply {
            pages["" to 1] = page(addon("recommended@example"))
        }

        val viewModel = AmoStoreViewModel(source, "en-US", 153, backgroundScope)
        runCurrent()

        assertEquals(listOf("recommended@example"), viewModel.content().items.map(AmoAddon::guid))
        assertEquals(listOf("" to 1), source.calls)
    }

    @Test
    fun replacesSearchContentAndShowsEmptyResults() = runTest {
        val source = FakeAmoSource().apply {
            pages["" to 1] = page(addon("recommended@example"))
            pages["nothing" to 1] = AmoCatalogPage(emptyList(), null)
        }
        val viewModel = AmoStoreViewModel(source, "en-US", 153, backgroundScope)
        runCurrent()

        viewModel.search("  nothing  ")
        runCurrent()

        val empty = viewModel.state.value as AmoStoreState.Empty
        assertEquals("nothing", empty.query)
    }

    @Test
    fun retriesTheFailedReplacementSearch() = runTest {
        val source = FakeAmoSource().apply {
            pages["" to 1] = AmoCatalogPage(emptyList(), null)
            outcomes["broken" to 1] = ArrayDeque(
                listOf(
                    Result.failure(IllegalStateException("offline")),
                    Result.success(page(addon("fixed@example"))),
                ),
            )
        }
        val viewModel = AmoStoreViewModel(source, "en-US", 153, backgroundScope)
        runCurrent()

        viewModel.search("broken")
        runCurrent()
        assertTrue(viewModel.state.value is AmoStoreState.Failed)

        viewModel.retry()
        runCurrent()

        assertEquals(listOf("fixed@example"), viewModel.content().items.map(AmoAddon::guid))
        assertEquals(listOf("broken" to 1, "broken" to 1), source.calls.takeLast(2))
    }

    @Test
    fun appendsNextPageWithoutDuplicateGuids() = runTest {
        val source = ControllableAmoSource()
        val viewModel = AmoStoreViewModel(source, "en-US", 153, backgroundScope)
        source.complete("", 1, page(addon("one@example"), addon("two@example"), nextPage = 2))
        runCurrent()

        viewModel.loadNext()
        assertTrue(viewModel.content().isLoadingNext)
        source.complete("", 2, page(addon("two@example"), addon("three@example")))
        runCurrent()

        assertEquals(
            listOf("one@example", "two@example", "three@example"),
            viewModel.content().items.map(AmoAddon::guid),
        )
        assertFalse(viewModel.content().isLoadingNext)
    }

    @Test
    fun ignoresAnOlderSearchResponse() = runTest {
        val source = ControllableAmoSource()
        val viewModel = AmoStoreViewModel(source, "en-US", 153, backgroundScope)
        source.complete("", 1, AmoCatalogPage(emptyList(), null))
        runCurrent()

        viewModel.search("old")
        viewModel.search("new")
        source.complete("new", 1, page(addon("new@example")))
        source.complete("old", 1, page(addon("old@example")))
        runCurrent()

        assertEquals(listOf("new@example"), viewModel.content().items.map(AmoAddon::guid))
    }

    @Test
    fun doesNotConvertCancellationIntoFailure() = runTest {
        val source = object : AmoCatalogSource {
            override suspend fun search(
                query: String,
                page: Int,
                locale: String,
                geckoMajor: Int,
            ): AmoCatalogPage = throw CancellationException("cancelled")
        }

        val viewModel = AmoStoreViewModel(source, "en-US", 153, backgroundScope)
        runCurrent()

        assertTrue(viewModel.state.value is AmoStoreState.Loading)
    }

    @Test
    fun nextPageKeepsTheQueryFromTheDisplayedContent() = runTest {
        val source = RecordingControllableAmoSource()
        val viewModel = AmoStoreViewModel(source, "en-US", 153, backgroundScope)
        source.complete("", 1, page(addon("one@example"), nextPage = 2))
        runCurrent()

        viewModel.loadNext()
        viewModel.search("new")
        runCurrent()

        assertTrue("" to 2 in source.calls)
        assertFalse("new" to 2 in source.calls)
    }

    @Test
    fun reloadsTheCurrentQueryWhenLocaleChanges() = runTest {
        val source = FakeAmoSource().apply {
            pages["" to 1] = page(addon("localized@example"))
        }
        val viewModel = AmoStoreViewModel(source, "en-US", 153, backgroundScope)
        runCurrent()

        viewModel.updateLocale("zh-CN")
        runCurrent()

        assertEquals(listOf("en-US", "zh-CN"), source.locales)
    }

    private fun AmoStoreViewModel.content(): AmoStoreState.Content =
        state.value as AmoStoreState.Content

    private fun page(vararg items: AmoAddon, nextPage: Int? = null) = AmoCatalogPage(items.toList(), nextPage)

    private fun addon(guid: String) = AmoAddon(
        id = guid.hashCode(),
        guid = guid,
        slug = guid.substringBefore('@'),
        name = guid,
        summary = "Summary",
        author = "Author",
        iconUrl = null,
        rating = null,
        version = "1.0",
        detailUrl = "https://addons.mozilla.org/addon/${guid.substringBefore('@')}/",
        installUrl = "https://addons.mozilla.org/download/$guid.xpi",
    )

    private class FakeAmoSource : AmoCatalogSource {
        val pages = mutableMapOf<Pair<String, Int>, AmoCatalogPage>()
        val outcomes = mutableMapOf<Pair<String, Int>, ArrayDeque<Result<AmoCatalogPage>>>()
        val calls = mutableListOf<Pair<String, Int>>()
        val locales = mutableListOf<String>()

        override suspend fun search(
            query: String,
            page: Int,
            locale: String,
            geckoMajor: Int,
        ): AmoCatalogPage {
            calls += query to page
            locales += locale
            outcomes[query to page]?.removeFirstOrNull()?.getOrThrow()?.let { return it }
            return pages[query to page] ?: AmoCatalogPage(emptyList(), null)
        }
    }

    private class RecordingControllableAmoSource : AmoCatalogSource {
        private val pending = mutableMapOf<Pair<String, Int>, CompletableDeferred<AmoCatalogPage>>()
        val calls = mutableListOf<Pair<String, Int>>()

        override suspend fun search(
            query: String,
            page: Int,
            locale: String,
            geckoMajor: Int,
        ): AmoCatalogPage {
            calls += query to page
            return pending.getOrPut(query to page) { CompletableDeferred() }.await()
        }

        fun complete(query: String, page: Int, value: AmoCatalogPage) {
            pending.getOrPut(query to page) { CompletableDeferred() }.complete(value)
        }
    }

    private class ControllableAmoSource : AmoCatalogSource {
        private val pending = mutableMapOf<Pair<String, Int>, CompletableDeferred<AmoCatalogPage>>()

        override suspend fun search(
            query: String,
            page: Int,
            locale: String,
            geckoMajor: Int,
        ): AmoCatalogPage = pending.getOrPut(query to page) { CompletableDeferred() }.await()

        fun complete(query: String, page: Int, value: AmoCatalogPage) {
            pending.getOrPut(query to page) { CompletableDeferred() }.complete(value)
        }
    }
}
