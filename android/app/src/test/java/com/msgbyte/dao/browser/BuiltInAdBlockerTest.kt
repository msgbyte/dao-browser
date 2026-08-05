package com.msgbyte.dao.browser

import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import io.mockk.verify
import mozilla.components.concept.engine.CancellableOperation
import mozilla.components.concept.engine.Engine
import org.junit.Assert.assertSame
import org.junit.Test

class BuiltInAdBlockerTest {
    private val engine = mockk<Engine>()
    private val operation = mockk<CancellableOperation>()

    @Test
    fun installsTheBundledExtensionWithStableIdentity() {
        every {
            engine.installBuiltInWebExtension(any(), any(), any(), any())
        } returns operation

        val result = BuiltInAdBlocker.install(engine) {}

        assertSame(operation, result)
        verify(exactly = 1) {
            engine.installBuiltInWebExtension(
                id = "uBlock0@raymondhill.net",
                url = "resource://android/assets/extensions/ublock_origin/",
                onSuccess = any(),
                onError = any(),
            )
        }
    }

    @Test
    fun forwardsInstallationFailureWithoutThrowing() {
        val errorCallback = slot<(Throwable) -> Unit>()
        val expected = IllegalStateException("installation failed")
        var actual: Throwable? = null
        every {
            engine.installBuiltInWebExtension(
                any(),
                any(),
                any(),
                capture(errorCallback),
            )
        } returns operation

        BuiltInAdBlocker.install(engine) { actual = it }
        errorCallback.captured(expected)

        assertSame(expected, actual)
    }
}
