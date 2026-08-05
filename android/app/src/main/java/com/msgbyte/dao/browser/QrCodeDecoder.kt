package com.msgbyte.dao.browser

import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.MultiFormatReader
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.common.HybridBinarizer

object QrCodeDecoder {
    fun decode(
        luminance: ByteArray,
        width: Int,
        height: Int,
        rotationDegrees: Int,
    ): String? {
        if (luminance.size < width * height || width <= 0 || height <= 0) return null
        val rotated = rotate(luminance, width, height, rotationDegrees)
        val source = PlanarYUVLuminanceSource(
            rotated.bytes,
            rotated.width,
            rotated.height,
            0,
            0,
            rotated.width,
            rotated.height,
            false,
        )
        return runCatching {
            MultiFormatReader().decode(
                BinaryBitmap(HybridBinarizer(source)),
                mapOf(
                    DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE),
                    DecodeHintType.TRY_HARDER to true,
                ),
            ).text
        }.getOrNull()
    }

    private fun rotate(bytes: ByteArray, width: Int, height: Int, degrees: Int): Frame =
        when ((degrees % 360 + 360) % 360) {
            90 -> Frame(ByteArray(bytes.size) { index ->
                val x = index % height
                val y = index / height
                bytes[(height - 1 - x) * width + y]
            }, height, width)
            180 -> Frame(ByteArray(bytes.size) { index -> bytes[bytes.lastIndex - index] }, width, height)
            270 -> Frame(ByteArray(bytes.size) { index ->
                val x = index % height
                val y = index / height
                bytes[x * width + (width - 1 - y)]
            }, height, width)
            else -> Frame(bytes, width, height)
        }

    private data class Frame(val bytes: ByteArray, val width: Int, val height: Int)
}
