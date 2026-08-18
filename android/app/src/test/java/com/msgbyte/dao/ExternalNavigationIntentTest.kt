package com.msgbyte.dao

import android.content.Intent
import android.net.Uri
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class ExternalNavigationIntentTest {
    @Test
    fun `accepts view intents for web urls`() {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse("https://example.com/article?id=1"))

        assertEquals("https://example.com/article?id=1", intent.httpNavigationUrl())
    }

    @Test
    fun `rejects non web schemes and intents without a host`() {
        assertNull(
            Intent(Intent.ACTION_VIEW, Uri.parse("file:///tmp/page.html")).httpNavigationUrl(),
        )
        assertNull(
            Intent(Intent.ACTION_VIEW, Uri.parse("https:///missing-host")).httpNavigationUrl(),
        )
        assertNull(
            Intent(Intent.ACTION_SEND, Uri.parse("https://example.com")).httpNavigationUrl(),
        )
    }
}
