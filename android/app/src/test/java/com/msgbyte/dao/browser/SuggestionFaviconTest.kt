package com.msgbyte.dao.browser

import android.content.Context
import android.graphics.Bitmap
import androidx.test.core.app.ApplicationProvider
import io.mockk.every
import io.mockk.mockk
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import kotlinx.coroutines.runBlocking
import mozilla.components.browser.icons.BrowserIcons
import mozilla.components.browser.icons.IconRequest
import mozilla.components.browser.icons.loader.MemoryInfoProvider
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class SuggestionFaviconTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val requests = mutableListOf<Request>()
    private val client = mockk<Client>()
    private val icons = BrowserIcons(context, client, memoryInfoProvider = mockk<MemoryInfoProvider> {
        every { getAvailMem() } returns 1024L * 1024 * 1024
    })

    @Test
    fun aPreviouslyDiscoveredIconIsReusedWithoutRequestingTheRootFavicon() = runBlocking {
        every { client.fetch(any()) } answers { pngResponse(firstArg()) }
        val url = "https://discovered.example/article"
        icons.loadIcon(
            IconRequest(url, resources = listOf(IconRequest.Resource(
                "https://discovered.example/assets/brand.png", IconRequest.Resource.Type.FAVICON,
            ))),
        ).await()
        requests.clear()

        val bitmap = icons.loadSuggestionFavicon(url, isPrivate = false)
        assertNotNull(bitmap)
        assertEquals(android.graphics.Color.RED, bitmap!!.getPixel(bitmap.width / 2, bitmap.height / 2))
        assertTrue(requests.isEmpty())
    }

    @Test
    fun anUncachedPageUsesItsOriginFaviconAndPreservesThePrivateRequest() = runBlocking {
        every { client.fetch(any()) } answers { pngResponse(firstArg()) }

        assertNotNull(icons.loadSuggestionFavicon("https://fresh.example:8443/path?q=secret#top", true))

        assertEquals(listOf("https://fresh.example:8443/favicon.ico"), requests.map { it.url })
        assertTrue(requests.single().private)
        assertEquals(Request.CookiePolicy.OMIT, requests.single().cookiePolicy)
    }

    @Test
    fun anUnavailableOrInvalidIconLeavesTheLetterFallback() = runBlocking {
        every { client.fetch(any()) } throws IOException("offline")
        assertNull(icons.loadSuggestionFavicon("https://offline.example/page", false))

        every { client.fetch(any()) } answers {
            val request = firstArg<Request>()
            Response(request.url, 200, MutableHeaders(), Response.Body("not an image".byteInputStream()))
        }
        assertNull(icons.loadSuggestionFavicon("https://invalid.example/page", false))
    }

    @Test
    fun internalAndMalformedAddressesDoNotFetchIcons() = runBlocking {
        listOf("about:blank", "file:///tmp/page.html", "https:///missing-host", "not a URL").forEach {
            assertNull(icons.loadSuggestionFavicon(it, false))
        }
    }

    private fun pngResponse(request: Request): Response {
        requests.add(request)
        val bytes = ByteArrayOutputStream().also {
            Bitmap.createBitmap(48, 48, Bitmap.Config.ARGB_8888)
                .apply { eraseColor(android.graphics.Color.RED) }
                .compress(Bitmap.CompressFormat.PNG, 100, it)
        }.toByteArray()
        return Response(request.url, 200, MutableHeaders(), Response.Body(ByteArrayInputStream(bytes)))
    }
}
