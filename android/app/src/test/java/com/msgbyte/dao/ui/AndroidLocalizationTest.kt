package com.msgbyte.dao.ui

import android.content.Context
import android.content.res.Configuration
import androidx.test.core.app.ApplicationProvider
import com.msgbyte.dao.R
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AndroidLocalizationTest {
    private val application = ApplicationProvider.getApplicationContext<Context>()

    @Test
    fun selectsEnglishAsTheDefaultAndSimplifiedChineseFromTheSystemLocale() {
        val english = application.forLocale(Locale.US)
        val chinese = application.forLocale(Locale.SIMPLIFIED_CHINESE)

        assertEquals("Search or enter address", english.getString(R.string.address_hint))
        assertEquals("搜索或输入网址", chinese.getString(R.string.address_hint))
        assertEquals(
            "Firefox remote debugging",
            english.getString(R.string.remote_debugging_clip_label),
        )
        assertEquals(
            "Firefox 远程调试",
            chinese.getString(R.string.remote_debugging_clip_label),
        )
    }

    @Test
    fun fallsBackToEnglishForAnUnsupportedSystemLocale() {
        val unsupported = application.forLocale(Locale.forLanguageTag("eo"))

        assertEquals("Search or enter address", unsupported.getString(R.string.address_hint))
    }
}

private fun Context.forLocale(locale: Locale): Context {
    val configuration = Configuration(resources.configuration)
    configuration.setLocale(locale)
    return createConfigurationContext(configuration)
}
