package com.msgbyte.dao.ui

import com.msgbyte.dao.browser.BrowserFontScale

data class FontPreviewTypography(
    val titleSizeSp: Float,
    val bodySizeSp: Float,
)

fun fontPreviewTypography(scale: BrowserFontScale): FontPreviewTypography =
    FontPreviewTypography(
        titleSizeSp = 18f * scale.factor,
        bodySizeSp = 15f * scale.factor,
    )
