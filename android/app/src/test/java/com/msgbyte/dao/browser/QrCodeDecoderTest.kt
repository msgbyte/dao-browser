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
            QrCodeDecoder.decode(luminance, matrix.width, matrix.height, rotationDegrees = 0),
        )
    }

    @Test
    fun returnsNullForAFrameWithoutQrContent() {
        assertNull(QrCodeDecoder.decode(ByteArray(100 * 100) { 0xFF.toByte() }, 100, 100, 0))
    }
}
