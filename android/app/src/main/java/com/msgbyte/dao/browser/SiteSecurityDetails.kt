package com.msgbyte.dao.browser

import java.net.URI
import java.security.MessageDigest
import java.security.cert.X509Certificate
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale
import javax.security.auth.x500.X500Principal
import mozilla.components.browser.state.state.SecurityInfo

enum class SiteSecurityState {
    SECURE,
    INSECURE,
    UNAVAILABLE,
}

data class SiteSecurityDetails(
    val host: String,
    val state: SiteSecurityState,
    val subject: String? = null,
    val issuer: String? = null,
    val validFrom: String? = null,
    val validUntil: String? = null,
    val serialNumber: String? = null,
    val sha256Fingerprint: String? = null,
) {
    companion object {
        fun from(
            url: String,
            securityInfo: SecurityInfo?,
            locale: Locale = Locale.getDefault(),
            zoneId: ZoneId = ZoneId.systemDefault(),
        ): SiteSecurityDetails {
            val host = securityInfo?.host.orEmpty().ifBlank { hostFromUrl(url) }
            return when (securityInfo) {
                is SecurityInfo.Secure -> fromSecure(host, securityInfo, locale, zoneId)
                is SecurityInfo.Insecure -> SiteSecurityDetails(
                    host = host,
                    state = SiteSecurityState.INSECURE,
                )
                else -> SiteSecurityDetails(
                    host = host,
                    state = SiteSecurityState.UNAVAILABLE,
                )
            }
        }

        private fun fromSecure(
            host: String,
            securityInfo: SecurityInfo.Secure,
            locale: Locale,
            zoneId: ZoneId,
        ): SiteSecurityDetails {
            val certificate = securityInfo.certificate
                ?: return SiteSecurityDetails(host = host, state = SiteSecurityState.SECURE)
            val dateFormatter = DateTimeFormatter.ofLocalizedDateTime(FormatStyle.MEDIUM)
                .withLocale(locale)
                .withZone(zoneId)
            return SiteSecurityDetails(
                host = host,
                state = SiteSecurityState.SECURE,
                subject = commonName(certificate.subjectX500Principal),
                issuer = securityInfo.issuer.ifBlank {
                    commonName(certificate.issuerX500Principal)
                },
                validFrom = dateFormatter.format(certificate.notBefore.toInstant()),
                validUntil = dateFormatter.format(certificate.notAfter.toInstant()),
                serialNumber = certificate.serialNumber.toString(16).uppercase(Locale.ROOT),
                sha256Fingerprint = fingerprint(certificate),
            )
        }

        private fun hostFromUrl(url: String): String = runCatching {
            URI(url).host.orEmpty()
        }.getOrDefault("").ifBlank { url.ifBlank { "about:blank" } }

        private fun commonName(principal: X500Principal): String {
            val fullName = principal.name
            return Regex("(?:^|,)CN=([^,]+)", RegexOption.IGNORE_CASE)
                .find(fullName)
                ?.groupValues
                ?.get(1)
                ?.replace("\\,", ",")
                ?.trim()
                ?.takeIf(String::isNotEmpty)
                ?: fullName
        }

        private fun fingerprint(certificate: X509Certificate): String? = runCatching {
            MessageDigest.getInstance("SHA-256")
                .digest(certificate.encoded)
                .joinToString(":") { byte -> "%02X".format(byte.toInt() and 0xFF) }
        }.getOrNull()
    }
}
