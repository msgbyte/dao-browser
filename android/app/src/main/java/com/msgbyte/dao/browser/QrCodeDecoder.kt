package com.msgbyte.dao.browser

import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.common.HybridBinarizer
import com.google.zxing.qrcode.QRCodeReader

object QrCodeDecoder {
    private val hints = mapOf(
        DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE),
        DecodeHintType.TRY_HARDER to true,
    )

    /**
     * Decodes a QR code from the centered square of a camera luminance plane.
     * QR detection is rotation-invariant, so the frame is read in sensor
     * orientation without copying or rotating it.
     */
    fun decode(
        luminance: ByteArray,
        width: Int,
        height: Int,
        rowStride: Int = width,
    ): String? {
        if (width <= 0 || height <= 0 || rowStride < width) return null
        if (luminance.size < rowStride * (height - 1) + width) return null
        val side = minOf(width, height)
        val source = PlanarYUVLuminanceSource(
            luminance,
            rowStride,
            height,
            (width - side) / 2,
            (height - side) / 2,
            side,
            side,
            false,
        )
        return runCatching {
            QRCodeReader().decode(BinaryBitmap(HybridBinarizer(source)), hints).text
        }.getOrNull()
    }
}
