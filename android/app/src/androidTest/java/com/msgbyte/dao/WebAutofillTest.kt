package com.msgbyte.dao

import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.ViewStructure
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.lifecycle.ViewModelProvider
import androidx.test.platform.app.InstrumentationRegistry
import com.msgbyte.dao.browser.BrowserSessionViewModel
import io.mockk.every
import io.mockk.mockk
import java.net.ServerSocket
import kotlin.concurrent.thread
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class WebAutofillTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<MainActivity>()

    private val server = ServerSocket(0)

    init {
        thread(isDaemon = true, name = "autofill-fixture") {
            while (!server.isClosed) {
                val socket = runCatching { server.accept() }.getOrNull() ?: break
                socket.use {
                    val input = it.getInputStream().bufferedReader()
                    while (!input.readLine().isNullOrEmpty()) Unit
                    val page = """
                        <!doctype html><meta name="viewport" content="width=device-width, initial-scale=1">
                        <title>autofill-ready</title>
                        <style>body{margin:0}input{display:block;box-sizing:border-box;width:100%;height:80px}</style>
                        <form>
                          <input name="username" autocomplete="username">
                          <input name="password" type="password" autocomplete="current-password">
                        </form>
                    """.trimIndent().toByteArray()
                    it.getOutputStream().apply {
                        write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ${page.size}\r\nConnection: close\r\n\r\n".toByteArray())
                        write(page)
                        flush()
                    }
                }
            }
        }
    }

    @After
    fun closeServer() {
        server.close()
    }

    @Test
    fun websiteCredentialsKeepTheirDomainWithFindInPageAndAfterNavigation() {
        openPage("127.0.0.1")
        assertCredentialDomain("127.0.0.1")

        composeRule.onNodeWithContentDescription(string(R.string.menu)).performClick()
        composeRule.onNodeWithText(string(R.string.find_in_page)).performClick()
        assertCredentialDomain("127.0.0.1")

        composeRule.onNodeWithContentDescription(string(R.string.close_find)).performClick()
        openPage("localhost")
        assertCredentialDomain("localhost")
    }

    private fun openPage(host: String) {
        val url = "http://$host:${server.localPort}/"
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val model = composeRule.runOnIdle {
            val activity = composeRule.activity
            val launchIntent = activity.intent
            instrumentation.callActivityOnNewIntent(activity, Intent(Intent.ACTION_VIEW, Uri.parse(url)))
            activity.intent = launchIntent
            ViewModelProvider(activity)[BrowserSessionViewModel::class.java]
        }
        composeRule.waitUntil(timeoutMillis = 30_000) {
            model.controller.selectedTab()?.content?.let {
                it.url == url && it.title == "autofill-ready" && !it.loading
            } == true
        }
        val (x, y) = composeRule.runOnIdle {
            val engine = composeRule.activity.findViewById<View>(R.id.browser_engine_view)
            val location = IntArray(2)
            engine.getLocationOnScreen(location)
            location[0] + engine.width / 2f to location[1] + 30 * engine.resources.displayMetrics.density
        }
        val now = SystemClock.uptimeMillis()
        instrumentation.sendPointerSync(MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, x, y, 0))
        instrumentation.sendPointerSync(MotionEvent.obtain(now, now + 50, MotionEvent.ACTION_UP, x, y, 0))
        // Gecko discovers form nodes asynchronously after the page has loaded.
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.runOnIdle {
                val engine = composeRule.activity.findViewById<View>(R.id.browser_engine_view)
                engine != null && snapshot(engine).nodes().any { it.domain == url.removeSuffix("/") }
            }
        }
    }

    private fun assertCredentialDomain(host: String) {
        val fields = composeRule.runOnIdle {
            snapshot(composeRule.activity.window.decorView).nodes()
                .filter { it.autofillType == View.AUTOFILL_TYPE_TEXT && it.domain != null }
        }
        assertTrue("The window autofill structure must include website credentials", fields.size >= 2)
        assertEquals(setOf("http://$host:${server.localPort}"), fields.map { it.domain }.toSet())
        assertTrue(fields.any { View.AUTOFILL_HINT_USERNAME in it.hints })
        assertTrue(fields.any { View.AUTOFILL_HINT_PASSWORD in it.hints })
    }

    private fun snapshot(view: View) = AutofillNode().also {
        view.dispatchProvideAutofillStructure(it.structure, 0)
    }

    private fun string(id: Int) = composeRule.activity.getString(id)

    // Keep child counts faithful to ViewStructure: ViewGroup stops traversing native views
    // when its virtual autofill structure has already supplied children.
    private class AutofillNode {
        val structure = mockk<ViewStructure>(relaxed = true)
        private val children = mutableListOf<AutofillNode?>()
        var domain: String? = null
        var autofillType = View.AUTOFILL_TYPE_NONE
        var hints = emptyList<String>()

        init {
            every { structure.childCount } answers { children.size }
            every { structure.setChildCount(any()) } answers {
                children.clear()
                repeat(firstArg<Int>()) { children.add(null) }
            }
            every { structure.addChildCount(any()) } answers {
                children.size.also { repeat(firstArg<Int>()) { children.add(null) } }
            }
            every { structure.newChild(any()) } answers {
                AutofillNode().also { children[firstArg<Int>()] = it }.structure
            }
            every { structure.setWebDomain(any()) } answers { domain = firstArg() }
            every { structure.setAutofillType(any()) } answers { autofillType = firstArg() }
            every { structure.setAutofillHints(any()) } answers {
                hints = firstArg<Array<String>?>()?.toList().orEmpty()
            }
        }

        fun nodes(): List<AutofillNode> = listOf(this) + children.filterNotNull().flatMap { it.nodes() }
    }
}
