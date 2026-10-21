package com.msgbyte.dao

import android.content.ContentValues
import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import android.provider.MediaStore
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import androidx.lifecycle.ViewModelProvider
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry
import com.msgbyte.dao.browser.BrowserSessionViewModel
import java.net.ServerSocket
import java.util.UUID
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread
import mozilla.components.concept.engine.EngineView
import org.junit.After
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
@SdkSuppress(minSdkVersion = 29)
class WebFileUploadTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val documents = mutableListOf<Uri>()
    private val server = ServerSocket(0)
    private val submissions = LinkedBlockingQueue<String>()
    private var scenario: ActivityScenario<MainActivity>? = null
    private lateinit var model: BrowserSessionViewModel
    private lateinit var activity: MainActivity
    private var page = ""

    init {
        thread(isDaemon = true, name = "upload-fixture") {
            while (!server.isClosed) {
                val socket = runCatching { server.accept() }.getOrNull() ?: break
                socket.use {
                    val input = it.getInputStream().buffered()
                    val header = StringBuilder()
                    while (!header.endsWith("\r\n\r\n")) {
                        val value = input.read()
                        if (value == -1) return@use
                        header.append(value.toChar())
                    }
                    val length = Regex("(?i)Content-Length: (\\d+)").find(header)?.groupValues?.get(1)?.toInt() ?: 0
                    val body = ByteArray(length)
                    var read = 0
                    while (read < length) {
                        val count = input.read(body, read, length - read)
                        if (count == -1) return@use
                        read += count
                    }
                    val upload = header.startsWith("POST /upload ")
                    if (upload) submissions.offer(body.toString(Charsets.UTF_8))
                    val response = (if (upload) "ok" else page).toByteArray()
                    it.getOutputStream().apply {
                        write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ${response.size}\r\nConnection: close\r\n\r\n".toByteArray())
                        write(response)
                        flush()
                    }
                }
            }
        }
    }

    @After
    fun close() {
        scenario?.close()
        server.close()
        documents.forEach { context.contentResolver.delete(it, null, null) }
    }

    @Test
    fun realPickerReturnsDocumentBytesAndCancellationAllowsRetry() {
        val name = addDocument("text/plain", "Dao upload fixture")
        openPage("")
        tapInput()
        waitForPicker()
        instrumentation.uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK)
        waitUntil { model.controller.selectedTab()?.content?.title == "cancelled" }
        tapInput()
        chooseDocument(name)
        assertTrue(submissions.poll(15, TimeUnit.SECONDS)!!.contains("filename=\"$name\""))
        waitUntil { model.controller.selectedTab()?.content?.title == "uploaded:1:Dao upload fixture" }
        assertTrue(model.controller.selectedTab()!!.content.promptRequests.isEmpty())
    }

    @Test
    fun styledPdfInputSurvivesActivityRecreationWithItsOriginalPrompt() {
        val name = addDocument("application/pdf", "%PDF-1.4 Dao fixture")
        openPage("accept='.pdf'", hidden = true)
        tapInput()
        waitForPicker()
        val original = model.fileUploads.pending.value!!
        assertTrue(original.prompt.mimeTypes.isNotEmpty())
        // ActivityScenario.recreate() waits for RESUMED, which an external picker prevents.
        instrumentation.runOnMainSync { activity.recreate() }
        waitUntil {
            var recreated = false
            instrumentation.runOnMainSync {
                val monitor = androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry.getInstance()
                recreated = listOf(
                    androidx.test.runner.lifecycle.Stage.CREATED,
                    androidx.test.runner.lifecycle.Stage.STARTED,
                    androidx.test.runner.lifecycle.Stage.RESUMED,
                    androidx.test.runner.lifecycle.Stage.PAUSED,
                    androidx.test.runner.lifecycle.Stage.STOPPED,
                ).flatMap(monitor::getActivitiesInStage).any {
                    it is MainActivity && it !== activity &&
                        ViewModelProvider(it)[BrowserSessionViewModel::class.java] === model
                }
            }
            recreated
        }
        assertSame(original, model.fileUploads.pending.value)
        chooseDocument(name)
        assertTrue(submissions.poll(15, TimeUnit.SECONDS)!!.contains("%PDF-1.4 Dao fixture"))
        waitUntil { model.controller.selectedTab()?.content?.title == "uploaded:1:%PDF-1.4 Dao fixture" }
        assertNull(model.fileUploads.pending.value)
    }

    @Test
    fun privateMultipleInputSubmitsEveryChosenDocument() {
        val first = addDocument("text/plain", "first document")
        val second = addDocument("text/plain", "second document")
        openPage("multiple accept='text/plain,application/pdf'", isPrivate = true)
        tapInput()
        waitForPicker()
        assertTrue(model.fileUploads.pending.value!!.prompt.isMultipleFilesSelection)
        clickText(first, longClick = true)
        clickText(second)
        clickText("Select")
        val body = submissions.poll(15, TimeUnit.SECONDS)!!
        assertTrue(body.contains("filename=\"$first\""))
        assertTrue(body.contains("filename=\"$second\""))
        assertTrue(body.contains("first document"))
        assertTrue(body.contains("second document"))
        waitUntil { model.controller.selectedTab()?.content?.title?.startsWith("uploaded:2:") == true }
        assertTrue(model.controller.selectedTab()!!.content.private)
        assertFalse(java.io.File(context.cacheDir, "uploads").exists())
    }

    private fun addDocument(type: String, text: String): String {
        val name = "dao-upload-${UUID.randomUUID()}.${if (type == "application/pdf") "pdf" else "txt"}"
        val uri = context.contentResolver.insert(
            MediaStore.Downloads.EXTERNAL_CONTENT_URI,
            ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, name)
                put(MediaStore.MediaColumns.MIME_TYPE, type)
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            },
        )!!
        documents += uri
        context.contentResolver.openOutputStream(uri)!!.use { it.write(text.toByteArray()) }
        context.contentResolver.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null)
        return name
    }

    private fun openPage(attributes: String, hidden: Boolean = false, isPrivate: Boolean = false) {
        page = """
            <!doctype html><meta name="viewport" content="width=device-width, initial-scale=1">
            <title>upload-ready</title>
            <style>body{margin:0}input,label{display:block;width:100%;height:160px}</style>
            <form id="form"><input id="file" name="files" type="file" $attributes ${if (hidden) "style='display:none'" else ""}>
            ${if (hidden) "<label for='file'>Choose a document</label>" else ""}</form>
            <script>
            const file=document.getElementById('file');
            file.addEventListener('cancel',()=>document.title='cancelled');
            file.addEventListener('change',async()=>{
                await fetch('/upload',{method:'POST',body:new FormData(document.getElementById('form'))});
                document.title='uploaded:'+file.files.length+':'+(await Promise.all(Array.from(file.files,f=>f.text()))).join('|');
            });
            </script>
        """.trimIndent()
        scenario = ActivityScenario.launch(Intent(context, MainActivity::class.java).apply {
            action = Intent.ACTION_VIEW
            data = Uri.parse("http://127.0.0.1:${server.localPort}/")
        })
        scenario!!.onActivity {
            activity = it
            model = ViewModelProvider(it)[BrowserSessionViewModel::class.java]
        }
        waitUntil { model.controller.selectedTab()?.content?.title == "upload-ready" }
        if (isPrivate) {
            scenario!!.onActivity {
                model.controller.createTab(private = true, url = "http://127.0.0.1:${server.localPort}/private")
            }
            waitUntil { model.controller.selectedTab()?.content?.private == true && model.controller.selectedTab()?.content?.title == "upload-ready" }
        }
    }

    private fun tapInput() {
        waitUntil {
            model.controller.selectedTab()?.content?.let { !it.loading && it.firstContentfulPaint } == true &&
                model.fileUploads.isBrowsing && model.fileUploads.isForeground
        }
        var x = 0f
        var y = 0f
        scenario!!.onActivity { activity ->
            val view = findEngineView(activity.window.decorView)!!
            val location = IntArray(2)
            view.getLocationOnScreen(location)
            x = location[0] + view.width / 2f
            y = location[1] + 40 * view.resources.displayMetrics.density
        }
        val now = SystemClock.uptimeMillis()
        instrumentation.sendPointerSync(MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, x, y, 0))
        instrumentation.sendPointerSync(MotionEvent.obtain(now, now + 50, MotionEvent.ACTION_UP, x, y, 0))
    }

    private fun findEngineView(view: View): View? {
        if (view is EngineView) return view
        if (view is ViewGroup) repeat(view.childCount) { findEngineView(view.getChildAt(it))?.let { return it } }
        return null
    }

    private fun waitForPicker() = waitUntil {
        instrumentation.uiAutomation.rootInActiveWindow?.packageName?.toString()?.contains("documentsui") == true
    }

    private fun chooseDocument(name: String) {
        waitForPicker()
        clickText(name)
    }

    private fun clickText(text: String, longClick: Boolean = false) {
        waitUntil {
            var node = instrumentation.uiAutomation.rootInActiveWindow
                ?.findAccessibilityNodeInfosByText(text)?.firstOrNull {
                    it.text?.toString().equals(text, ignoreCase = true) ||
                        it.contentDescription?.toString().equals(text, ignoreCase = true)
                } ?: return@waitUntil false
            val action = if (longClick) AccessibilityNodeInfo.ACTION_LONG_CLICK else AccessibilityNodeInfo.ACTION_CLICK
            while (true) {
                if (node.performAction(action)) return@waitUntil true
                node = node.parent ?: return@waitUntil false
            }
            @Suppress("UNREACHABLE_CODE")
            false
        }
    }

    private fun waitUntil(condition: () -> Boolean) {
        val end = SystemClock.uptimeMillis() + 60_000
        while (!condition()) {
            if (SystemClock.uptimeMillis() >= end) {
                fun describe(node: AccessibilityNodeInfo?): String {
                    if (node == null) return ""
                    return "${node.text ?: node.contentDescription ?: ""} " +
                        (0 until node.childCount).joinToString { describe(node.getChild(it)) }
                }
                throw AssertionError("Timed out waiting for the upload flow: ${describe(instrumentation.uiAutomation.rootInActiveWindow)}")
            }
            SystemClock.sleep(100)
        }
    }
}
