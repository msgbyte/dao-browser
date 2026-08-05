package com.msgbyte.dao.ui

import com.msgbyte.dao.browser.BrowserFontScale
import org.junit.Assert.assertEquals
import org.junit.Test

class FontPreviewTypographyTest {
    @Test
    fun previewTypographyUsesTheSameFactorsAsWebContent() {
        assertEquals(
            FontPreviewTypography(titleSizeSp = 15.3f, bodySizeSp = 12.75f),
            fontPreviewTypography(BrowserFontScale.SMALL),
        )
        assertEquals(
            FontPreviewTypography(titleSizeSp = 18f, bodySizeSp = 15f),
            fontPreviewTypography(BrowserFontScale.MEDIUM),
        )
        assertEquals(
            FontPreviewTypography(titleSizeSp = 21.6f, bodySizeSp = 18f),
            fontPreviewTypography(BrowserFontScale.LARGE),
        )
    }
}
