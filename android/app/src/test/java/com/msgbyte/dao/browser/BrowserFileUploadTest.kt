package com.msgbyte.dao.browser

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.ClipData
import android.content.ContentProvider
import android.content.ContentValues
import android.content.ContextWrapper
import android.content.res.AssetFileDescriptor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import androidx.test.core.app.ApplicationProvider
import io.mockk.mockk
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.runBlocking
import java.io.File
import java.io.FileNotFoundException
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.prompt.PromptRequest
import org.junit.After
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowContentResolver

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [26, 35])
class BrowserFileUploadTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
    private val uploads = BrowserFileUpload(context, scope, Dispatchers.Unconfined)
    private val store = BrowserStore(middleware = listOf(uploads) + EngineMiddleware.create(mockk(relaxed = true), scope))
    private val tab = TabSessionState(
        content = ContentState(url = "https://example.com/form"),
        engineState = EngineState(engineSession = mockk<EngineSession>(relaxed = true)),
    )
    private var dismissed = 0
    private var selected = 0

    init {
        store.dispatch(TabListAction.AddTabAction(tab, select = true))
        uploads.isBrowsing = true
        uploads.isForeground = true
    }

    @After
    fun tearDown() {
        uploads.close()
        scope.cancel()
    }

    @Test
    fun cancellationConsumesTheOriginalPromptOnceAndAllowsRetry() {
        val prompt = request()
        val pending = uploads.pending.value!!
        assertNotNull(uploads.launchIntent(pending))
        assertNull(uploads.launchIntent(pending))

        uploads.onResult(pending.key, Activity.RESULT_CANCELED, null)
        uploads.onResult(pending.key, Activity.RESULT_CANCELED, null)

        assertEquals(1, dismissed)
        assertEquals(0, selected)
        assertFalse(store.state.tabs.single().content.promptRequests.contains(prompt))
        assertNull(uploads.pending.value)
        request()
        assertNotNull(uploads.launchIntent(uploads.pending.value!!))
    }

    @Test
    fun competingRequestsAreDismissedWithoutReplacingTheActivePicker() {
        request()
        val first = uploads.pending.value!!
        uploads.launchIntent(first)
        request()
        assertSame(first, uploads.pending.value)
        assertEquals(1, dismissed)
        assertEquals(1, store.state.tabs.single().content.promptRequests.size)
    }

    @Test
    fun switchingAwayAndBackInvalidatesEvenWithoutAStateFlowCollector() {
        request()
        val first = uploads.pending.value!!
        uploads.launchIntent(first)
        val other = tab.copy(id = "other")
        store.dispatch(TabListAction.AddTabAction(other, select = true))
        store.dispatch(TabListAction.SelectTabAction(tab.id))
        uploads.onResult(first.key, Activity.RESULT_OK, Intent())
        assertEquals(1, dismissed)
        assertEquals(0, selected)
        assertNull(uploads.pending.value)
    }

    @Test
    fun activityPauseAndReattachmentKeepTheSamePickerAssociation() {
        request()
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        uploads.isForeground = false
        uploads.isBrowsing = false
        uploads.isBrowsing = true
        uploads.isForeground = true
        assertSame(pending, uploads.pending.value)
        assertNull(uploads.launchIntent(pending))
        uploads.onResult(pending.key, Activity.RESULT_CANCELED, null)
        assertEquals(1, dismissed)
    }

    @Test
    fun backgroundAndHiddenPageRequestsAreConsumedWithoutOpeningAPicker() {
        val background = tab.copy(id = "background")
        store.dispatch(TabListAction.AddTabAction(background, select = false))
        request(background.id)
        uploads.isBrowsing = false
        request()
        uploads.isBrowsing = true
        uploads.isForeground = false
        request()
        assertNull(uploads.pending.value)
        assertEquals(3, dismissed)
        assertTrue(store.state.tabs.all { it.content.promptRequests.isEmpty() })
    }

    @Test
    fun reloadAndNavigationInvalidateRequestsEvenForTheSameUrl() {
        request()
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        store.dispatch(EngineAction.ReloadAction(tab.id))
        assertEquals(1, dismissed)
        uploads.onResult(pending.key, Activity.RESULT_CANCELED, null)
        request()
        store.dispatch(ContentAction.UpdateLoadingStateAction(tab.id, true))
        assertEquals(2, dismissed)
    }

    @Test
    fun closingTheSourceTabDiscardsLateResults() {
        request()
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        store.dispatch(TabListAction.RemoveTabAction(tab.id))
        uploads.onResult(pending.key, Activity.RESULT_OK, Intent())
        assertEquals(1, dismissed)
        assertEquals(0, selected)
        assertNull(uploads.pending.value)
    }

    @Test
    fun oldResultsCannotCancelANewerPicker() {
        request()
        val old = uploads.pending.value!!
        uploads.launchIntent(old)
        uploads.onResult(old.key, Activity.RESULT_CANCELED, null)
        request()
        val current = uploads.pending.value!!
        uploads.launchIntent(current)
        uploads.onResult(old.key, Activity.RESULT_CANCELED, null)
        assertSame(current, uploads.pending.value)
        assertEquals(1, dismissed)
    }

    @Test
    fun pickerIntentPreservesMimeTypesAndSelectionModeWithoutPersistentGrants() {
        request(types = arrayOf("image/*", "application/pdf"), multiple = true)
        val intent = uploads.launchIntent(uploads.pending.value!!)!!
        assertEquals(Intent.ACTION_OPEN_DOCUMENT, intent.action)
        assertTrue(intent.hasCategory(Intent.CATEGORY_OPENABLE))
        assertEquals("*/*", intent.type)
        assertArrayEquals(arrayOf("image/*", "application/pdf"), intent.getStringArrayExtra(Intent.EXTRA_MIME_TYPES))
        assertTrue(intent.getBooleanExtra(Intent.EXTRA_ALLOW_MULTIPLE, false))
        assertEquals(0, intent.flags and Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
    }

    @Test
    fun extensionAcceptValuesUseAndroidMimeTypes() {
        org.robolectric.Shadows.shadowOf(android.webkit.MimeTypeMap.getSingleton())
            .addExtensionMimeTypeMapping("pdf", "application/pdf")
        request(types = arrayOf("pdf", ".PDF", "image/*"))
        val intent = uploads.launchIntent(uploads.pending.value!!)!!
        assertArrayEquals(arrayOf("application/pdf", "image/*"), intent.getStringArrayExtra(Intent.EXTRA_MIME_TYPES))
    }

    @Test
    fun sessionLossDismissesAndNeverReusesTheReplacementSession() {
        request()
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        store.dispatch(EngineAction.UnlinkEngineSessionAction(tab.id))
        store.dispatch(EngineAction.LinkEngineSessionAction(tab.id, mockk(relaxed = true)))
        uploads.onResult(pending.key, Activity.RESULT_OK, Intent())
        assertEquals(1, dismissed)
        assertEquals(0, selected)
    }

    @Test
    fun launchFailureConsumesTheRequestAndEmitsOnlyOneError() {
        val errors = mutableListOf<Int>()
        scope.launch { uploads.errors.collect { errors += it } }
        request()
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        uploads.onLaunchFailed(pending.key)
        uploads.onLaunchFailed(pending.key)
        assertEquals(1, dismissed)
        assertEquals(listOf(com.msgbyte.dao.R.string.file_picker_unavailable), errors)
        request()
        assertNotNull(uploads.launchIntent(uploads.pending.value!!))
    }

    @Test
    fun multipleSelectionKeepsDistinctSameNamedDocumentsAndTheirBytes() {
        val first = document("first", "one")
        val second = document("second", "two")
        val files = mutableListOf<File>()
        request(multiple = true, onMultiple = { _, uris ->
            files += uris.map { File(it.path!!) }
        })
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        val data = Intent().apply {
            clipData = ClipData.newRawUri("documents", first).apply {
                addItem(ClipData.Item(second))
            }
        }
        uploads.onResult(pending.key, Activity.RESULT_OK, data)
        uploads.onResult(pending.key, Activity.RESULT_OK, data)
        assertEquals(listOf("one", "two"), files.map { it.readText() })
        assertEquals(listOf("same.txt", "same.txt"), files.map { it.name })
        assertEquals(2, files.distinct().size)
        assertEquals(mapOf("first" to 1, "second" to 1), opens)
        assertEquals(0, dismissed)
        assertTrue(store.state.tabs.single().content.promptRequests.isEmpty())
    }

    @Test
    fun oneUnreadableDocumentRejectsTheWholeSelectionAndPermitsRetry() {
        val errors = mutableListOf<Int>()
        scope.launch { uploads.errors.collect { errors += it } }
        val first = document("first", "one")
        val missing = Uri.parse("content://upload.test/missing")
        request(multiple = true)
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        uploads.onResult(pending.key, Activity.RESULT_OK, Intent().apply {
            clipData = ClipData.newRawUri("documents", first).apply { addItem(ClipData.Item(missing)) }
        })
        assertEquals(0, selected)
        assertEquals(1, dismissed)
        assertEquals(listOf(com.msgbyte.dao.R.string.file_picker_read_failed), errors)
        request()
        assertNotNull(uploads.launchIntent(uploads.pending.value!!))
    }

    @Test
    fun singleSelectionUsesOnlyTheFirstReadableDocument() {
        val first = document("first", "one")
        val second = document("second", "two")
        val chosen = mutableListOf<Uri>()
        request(onSingle = { _, uri -> chosen += uri })
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        uploads.onResult(pending.key, Activity.RESULT_OK, Intent().apply {
            clipData = ClipData.newRawUri("documents", Uri.parse("file:///data/data/com.msgbyte.dao/private")).apply {
                addItem(ClipData.Item(first))
                addItem(ClipData.Item(second))
            }
        })
        assertEquals(listOf("one"), chosen.map { File(it.path!!).readText() })
        assertEquals(0, dismissed)
    }

    @Test
    fun callbackConsumptionCannotCauseASecondTerminalOutcome() {
        val uri = document("first", "one")
        lateinit var prompt: PromptRequest.File
        prompt = request(onSingle = { _, _ ->
            selected++
            store.dispatch(ContentAction.ConsumePromptRequestAction(tab.id, prompt))
        })
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        uploads.onResult(pending.key, Activity.RESULT_OK, Intent().setData(uri))
        assertEquals(1, selected)
        assertEquals(0, dismissed)
    }

    @Test
    fun truncatedProviderDataFailsBeforeAnyCallback() {
        val uri = document("short", "short")
        lengths["short"] = 100
        request()
        val pending = uploads.pending.value!!
        uploads.launchIntent(pending)
        uploads.onResult(pending.key, Activity.RESULT_OK, Intent().setData(uri))
        assertEquals(0, selected)
        assertEquals(1, dismissed)
        assertNull(uploads.pending.value)
    }

    @Test
    fun invalidationDuringStagingDoesNotBlockAnotherTabsPicker() {
        val delayed = BrowserFileUpload(context, scope, StandardTestDispatcher())
        val localStore = BrowserStore(middleware = listOf(delayed))
        localStore.dispatch(TabListAction.AddTabAction(tab, select = true))
        delayed.isBrowsing = true
        delayed.isForeground = true
        val prompt = PromptRequest.File(
            emptyArray(),
            onSingleFileSelected = { _, _ -> selected++ },
            onMultipleFilesSelected = { _, _ -> selected++ },
            onDismiss = { dismissed++ },
        )
        localStore.dispatch(ContentAction.UpdatePromptRequestAction(tab.id, prompt))
        val pending = delayed.pending.value!!
        delayed.launchIntent(pending)
        delayed.onResult(pending.key, Activity.RESULT_OK, Intent().setData(document("slow", "bytes")))
        localStore.dispatch(TabListAction.AddTabAction(tab.copy(id = "new"), select = true))
        localStore.dispatch(ContentAction.UpdatePromptRequestAction("new", prompt.copy()))
        assertEquals(1, dismissed)
        assertEquals("new", delayed.pending.value!!.tabId)
        assertNotNull(delayed.launchIntent(delayed.pending.value!!))
        delayed.close()
    }

    @Test
    fun unusableUploadCacheFailsSelectionInsteadOfBrowserStartup() {
        val cache = File(context.cacheDir, "blocked-cache").apply { mkdirs() }
        File(cache, "web-file-uploads").writeText("not a directory")
        val storage = FileUploadStorage(object : ContextWrapper(context) {
            override fun getCacheDir() = cache
        })
        assertThrows(java.io.IOException::class.java) {
            runBlocking { storage.stage(listOf(document("first", "bytes")), false) }
        }
        cache.deleteRecursively()
    }

    private val documents = mutableMapOf<String, File>()
    private val opens = mutableMapOf<String, Int>()
    private val lengths = mutableMapOf<String, Long>()
    private fun document(id: String, bytes: String): Uri {
        documents[id] = File(context.cacheDir, "fixture-$id").apply { writeText(bytes) }
        val provider = object : ContentProvider() {
            override fun onCreate() = true
            override fun getType(uri: Uri) = "text/plain"
            override fun query(uri: Uri, projection: Array<out String>?, selection: String?, args: Array<out String>?, sort: String?) =
                MatrixCursor(arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)).apply {
                    addRow(arrayOf<Any?>("same.txt", documents[uri.lastPathSegment]?.length()))
                }
            override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
                val id = uri.lastPathSegment!!
                opens[id] = (opens[id] ?: 0) + 1
                return ParcelFileDescriptor.open(documents[id] ?: throw FileNotFoundException(), ParcelFileDescriptor.MODE_READ_ONLY)
            }
            override fun openAssetFile(uri: Uri, mode: String) =
                AssetFileDescriptor(openFile(uri, mode), 0, lengths[uri.lastPathSegment] ?: AssetFileDescriptor.UNKNOWN_LENGTH)
            override fun insert(uri: Uri, values: ContentValues?): Uri? = null
            override fun update(uri: Uri, values: ContentValues?, selection: String?, args: Array<out String>?) = 0
            override fun delete(uri: Uri, selection: String?, args: Array<out String>?) = 0
        }
        provider.attachInfo(context, android.content.pm.ProviderInfo().apply { authority = "upload.test" })
        ShadowContentResolver.registerProviderInternal("upload.test", provider)
        return Uri.parse("content://upload.test/$id")
    }

    private fun request(
        tabId: String = tab.id,
        types: Array<String> = emptyArray(),
        multiple: Boolean = false,
        onSingle: (Context, Uri) -> Unit = { _, _ -> selected++ },
        onMultiple: (Context, Array<Uri>) -> Unit = { _, _ -> selected++ },
    ): PromptRequest.File = PromptRequest.File(
        mimeTypes = types,
        isMultipleFilesSelection = multiple,
        onSingleFileSelected = onSingle,
        onMultipleFilesSelected = onMultiple,
        onDismiss = { dismissed++ },
    ).also { store.dispatch(ContentAction.UpdatePromptRequestAction(tabId, it)) }
}
