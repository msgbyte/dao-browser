package com.msgbyte.dao.browser

import org.junit.Assert.assertEquals
import org.junit.Test

class AppConfigurationTest {
    @Test
    fun initialUrlIsBlankBrowserPage() {
        assertEquals("about:blank", AppConfiguration.INITIAL_URL)
    }
}
