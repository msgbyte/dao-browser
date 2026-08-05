package com.msgbyte.dao.browser

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

sealed interface AmoStoreState {
    data object Loading : AmoStoreState

    data class Content(
        val query: String,
        val items: List<AmoAddon>,
        val nextPage: Int?,
        val isLoadingNext: Boolean = false,
    ) : AmoStoreState

    data class Empty(
        val query: String,
    ) : AmoStoreState

    data class Failed(
        val query: String,
        val error: Throwable,
    ) : AmoStoreState
}

class AmoStoreViewModel(
    private val source: AmoCatalogSource,
    locale: String,
    private val geckoMajor: Int,
    private val workerScope: CoroutineScope? = null,
) : ViewModel() {
    private val mutableState = MutableStateFlow<AmoStoreState>(AmoStoreState.Loading)
    private var query = ""
    private var locale = locale
    private var requestGeneration = 0L

    val state: StateFlow<AmoStoreState> = mutableState.asStateFlow()

    init {
        search("")
    }

    fun search(query: String) {
        this.query = query.trim()
        loadReplacement()
    }

    fun retry() {
        loadReplacement()
    }

    fun updateLocale(locale: String) {
        if (this.locale == locale) return
        this.locale = locale
        loadReplacement()
    }

    fun loadNext() {
        val content = mutableState.value as? AmoStoreState.Content ?: return
        val nextPage = content.nextPage ?: return
        if (content.isLoadingNext) return
        val generation = requestGeneration
        val requestQuery = content.query
        val requestLocale = locale
        mutableState.value = content.copy(isLoadingNext = true)
        launch {
            try {
                val page = source.search(requestQuery, nextPage, requestLocale, geckoMajor)
                if (generation != requestGeneration) return@launch
                val current = mutableState.value as? AmoStoreState.Content ?: return@launch
                val combined = (current.items + page.items).distinctBy(AmoAddon::guid)
                mutableState.value = current.copy(
                    items = combined,
                    nextPage = page.nextPage,
                    isLoadingNext = false,
                )
            } catch (error: Throwable) {
                if (error is CancellationException) throw error
                if (generation == requestGeneration) {
                    val current = mutableState.value as? AmoStoreState.Content ?: return@launch
                    mutableState.value = current.copy(isLoadingNext = false)
                }
            }
        }
    }

    private fun loadReplacement() {
        val generation = ++requestGeneration
        val requestQuery = query
        val requestLocale = locale
        mutableState.value = AmoStoreState.Loading
        launch {
            try {
                val page = source.search(requestQuery, 1, requestLocale, geckoMajor)
                if (generation != requestGeneration) return@launch
                mutableState.value = if (page.items.isEmpty()) {
                    AmoStoreState.Empty(requestQuery)
                } else {
                    AmoStoreState.Content(
                        query = requestQuery,
                        items = page.items.distinctBy(AmoAddon::guid),
                        nextPage = page.nextPage,
                    )
                }
            } catch (error: Throwable) {
                if (error is CancellationException) throw error
                if (generation == requestGeneration) {
                    mutableState.value = AmoStoreState.Failed(requestQuery, error)
                }
            }
        }
    }

    private fun launch(block: suspend () -> Unit) {
        (workerScope ?: viewModelScope).launch {
            block()
        }
    }
}
