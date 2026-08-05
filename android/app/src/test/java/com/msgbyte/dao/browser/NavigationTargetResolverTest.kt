package com.msgbyte.dao.browser

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NavigationTargetResolverTest {
    private val resolver = NavigationTargetResolver(
        searchUrl = "https://www.google.com/search?q=%s",
    )

    @Test
    fun blankInputDoesNotNavigate() {
        assertNull(resolver.resolve("   "))
    }

    @Test
    fun preservesHttpUrl() {
        assertEquals("http://example.com/path", resolver.resolve("http://example.com/path"))
    }

    @Test
    fun preservesUppercaseHttpUrl() {
        assertEquals("HTTP://example.com/path", resolver.resolve("HTTP://example.com/path"))
    }

    @Test
    fun preservesMixedCaseHttpsUrl() {
        assertEquals("hTtPs://example.com/path", resolver.resolve("hTtPs://example.com/path"))
    }

    @Test
    fun addsHttpsToDomain() {
        assertEquals("https://example.com", resolver.resolve("example.com"))
    }

    @Test
    fun preservesBlankPageWithFragment() {
        assertEquals("about:blank#test-page", resolver.resolve("about:blank#test-page"))
    }

    @Test
    fun encodesSearchTerms() {
        assertEquals(
            "https://www.google.com/search?q=dao+browser",
            resolver.resolve("dao browser"),
        )
    }
}
