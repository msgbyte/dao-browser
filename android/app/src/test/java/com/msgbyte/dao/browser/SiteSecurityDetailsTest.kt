package com.msgbyte.dao.browser

import io.mockk.every
import io.mockk.mockk
import java.math.BigInteger
import java.security.cert.X509Certificate
import java.time.Instant
import java.time.ZoneId
import java.util.Date
import java.util.Locale
import javax.security.auth.x500.X500Principal
import mozilla.components.browser.state.state.SecurityInfo
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SiteSecurityDetailsTest {
    @Test
    fun `secure Gecko state exposes certificate identity and SHA-256 fingerprint`() {
        val certificate = mockk<X509Certificate>()
        every { certificate.subjectX500Principal } returns
            X500Principal("CN=example.com,O=Example Corp")
        every { certificate.issuerX500Principal } returns
            X500Principal("CN=Example CA,O=Example Trust")
        every { certificate.notBefore } returns
            Date.from(Instant.parse("2026-01-01T00:00:00Z"))
        every { certificate.notAfter } returns
            Date.from(Instant.parse("2027-01-01T00:00:00Z"))
        every { certificate.serialNumber } returns BigInteger("255")
        every { certificate.encoded } returns byteArrayOf(1, 2, 3)

        val details = SiteSecurityDetails.from(
            url = "https://example.com/path",
            securityInfo = SecurityInfo.Secure("example.com", "Example CA", certificate),
            locale = Locale.US,
            zoneId = ZoneId.of("UTC"),
        )

        assertEquals(SiteSecurityState.SECURE, details.state)
        assertEquals("example.com", details.host)
        assertEquals("example.com", details.subject)
        assertEquals("Example CA", details.issuer)
        assertEquals("Jan 1, 2026, 12:00:00 AM", details.validFrom)
        assertEquals("Jan 1, 2027, 12:00:00 AM", details.validUntil)
        assertEquals("FF", details.serialNumber)
        assertEquals(
            "03:90:58:C6:F2:C0:CB:49:2C:53:3B:0A:4D:14:EF:77:" +
                "CC:0F:78:AB:CC:CE:D5:28:7D:84:A1:A2:01:1C:FB:81",
            details.sha256Fingerprint,
        )
    }

    @Test
    fun `insecure and unknown states never expose stale certificate details`() {
        val insecure = SiteSecurityDetails.from(
            url = "http://example.com",
            securityInfo = SecurityInfo.Insecure("example.com", "", null),
            locale = Locale.US,
            zoneId = ZoneId.of("UTC"),
        )
        val unknown = SiteSecurityDetails.from(
            url = "about:blank",
            securityInfo = SecurityInfo.Unknown,
            locale = Locale.US,
            zoneId = ZoneId.of("UTC"),
        )

        assertEquals(SiteSecurityState.INSECURE, insecure.state)
        assertEquals("example.com", insecure.host)
        assertNull(insecure.subject)
        assertNull(insecure.issuer)
        assertNull(insecure.sha256Fingerprint)
        assertEquals(SiteSecurityState.UNAVAILABLE, unknown.state)
        assertEquals("about:blank", unknown.host)
        assertNull(unknown.subject)
        assertNull(unknown.sha256Fingerprint)
    }
}
