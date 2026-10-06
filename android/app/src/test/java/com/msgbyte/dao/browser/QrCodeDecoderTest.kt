package com.msgbyte.dao.browser

import com.google.zxing.BarcodeFormat
import com.google.zxing.qrcode.QRCodeWriter
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class QrCodeDecoderTest {
    @Test
    fun decodesQrContentFromCameraLuminanceData() {
        val expected = "https://example.com/from-qr"
        val matrix = QRCodeWriter().encode(expected, BarcodeFormat.QR_CODE, 240, 240)
        val luminance = ByteArray(matrix.width * matrix.height) { index ->
            val x = index % matrix.width
            val y = index / matrix.width
            if (matrix[x, y]) 0 else 0xFF.toByte()
        }

        assertEquals(
            expected,
            QrCodeDecoder.decode(luminance, matrix.width, matrix.height),
        )
    }

    @Test
    fun decodesRotatedQrFromCenterOfStridePaddedFrame() {
        val expected = "https://example.com/padded-frame"
        val matrix = QRCodeWriter().encode(expected, BarcodeFormat.QR_CODE, 240, 240)
        val width = 400
        val height = 300
        val rowStride = 416
        val left = (width - matrix.width) / 2
        val top = (height - matrix.height) / 2
        // Row padding is black so reading it as pixels would corrupt the code.
        val luminance = ByteArray(rowStride * height) { index ->
            val column = index % rowStride
            val x = column - left
            val y = index / rowStride - top
            val dark = column >= width ||
                (x in 0 until matrix.width && y in 0 until matrix.height && matrix[y, matrix.width - 1 - x])
            if (dark) 0 else 0xFF.toByte()
        }

        assertEquals(expected, QrCodeDecoder.decode(luminance, width, height, rowStride))
    }

    @Test
    fun returnsNullForAFrameWithoutQrContent() {
        assertNull(QrCodeDecoder.decode(ByteArray(100 * 100) { 0xFF.toByte() }, 100, 100))
    }
}
