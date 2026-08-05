package com.msgbyte.dao.browser

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.concept.engine.CancellableOperation
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.WebExtension
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

class BrowserRuntimeTest {
    @Test
    fun appliesRemoteDebuggingBeforeEngineAndSessionCreation() {
        val engine = mockk<Engine>()
        val operation = mockk<CancellableOperation>()
        val session = mockk<EngineSession>(relaxed = true)
        val calls = mutableListOf<String>()
        every {
            engine.installBuiltInWebExtension(any(), any(), any(), any())
        } answers {
            calls += "install"
            operation
        }
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            arg<(List<WebExtension>) -> Unit>(0)(emptyList())
        }
        every { engine.createSession(private = false, contextId = null) } answers {
            calls += "session"
            session
        }
        val runtime = BrowserRuntime(
            createEngine = {
                calls += "engine"
                engine
            },
            setRemoteDebugging = { enabled -> calls += "remote:$enabled" },
            onBuiltInAdBlockerError = {},
        )

        runtime.setRemoteDebuggingEnabled(true)
        BrowserSessionController(runtime.engine, "about:blank")

        assertEquals(listOf("remote:true", "engine", "install", "install", "session"), calls)
    }

    @Test
    fun installsAdBlockerOnceBeforeSessionCreation() {
        val engine = mockk<Engine>()
        val operation = mockk<CancellableOperation>()
        val session = mockk<EngineSession>(relaxed = true)
        val calls = mutableListOf<String>()
        every {
            engine.installBuiltInWebExtension(any(), any(), any(), any())
        } answers {
            calls += "install"
            operation
        }
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            arg<(List<WebExtension>) -> Unit>(0)(emptyList())
        }
        every { engine.createSession(private = false, contextId = null) } answers {
            calls += "session"
            session
        }
        val runtime = BrowserRuntime(
            createEngine = { engine },
            onBuiltInAdBlockerError = {},
        )

        BrowserSessionController(runtime.engine, "about:blank")
        assertSame(engine, runtime.engine)

        assertEquals(listOf("install", "install", "session"), calls)
        verify(exactly = 2) {
            engine.installBuiltInWebExtension(any(), any(), any(), any())
        }
    }

    @Test
    fun installationFailureDoesNotPreventEngineAccess() {
        val engine = mockk<Engine>()
        val operation = mockk<CancellableOperation>()
        val expected = IllegalStateException("installation failed")
        var actual: Throwable? = null
        every {
            engine.installBuiltInWebExtension(any(), any(), any(), any())
        } answers {
            arg<(Throwable) -> Unit>(3)(expected)
            operation
        }
        every { engine.listInstalledWebExtensions(any(), any()) } answers {
            arg<(List<WebExtension>) -> Unit>(0)(emptyList())
        }
        val runtime = BrowserRuntime(
            createEngine = { engine },
            onBuiltInAdBlockerError = { actual = it },
            onBuiltInTranslatorError = {},
        )

        assertSame(engine, runtime.engine)
        assertSame(expected, actual)
    }
}
