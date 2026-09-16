package com.msgbyte.dao.browser

import java.io.IOException
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

data class AppUpdateRelease(
    val version: String,
    val fileName: String,
    val url: String,
    val size: Long,
    val sha256: String,
    val notes: String,
) {
    fun toJson(): String = JSONObject().apply {
        put("version", version)
        put("fileName", fileName)
        put("url", url)
        put("size", size)
        put("sha256", sha256)
        put("notes", notes)
    }.toString()

    companion object {
        fun fromJson(raw: String): AppUpdateRelease = JSONObject(raw).let {
            AppUpdateRelease(
                it.getString("version"), it.getString("fileName"), it.getString("url"),
                it.getLong("size"), it.getString("sha256"), it.getString("notes"),
            )
        }
    }
}

internal class UpdateRateLimited(val retryAt: Long) : IOException("GitHub rate limit")
internal class NoCompatibleUpdate : IOException("No compatible Android release asset")

class AppUpdateRepository(
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val fetch: suspend (String) -> String = ::fetchUpdateText,
) {
    suspend fun findUpdate(currentVersion: String, supportedAbis: List<String>): AppUpdateRelease? =
        withContext(ioDispatcher) {
            val current = parseVersion(currentVersion) ?: throw IOException("Invalid installed version")
            var latest: JSONObject? = null
            var latestVersion = current
            var page = 1
            while (true) {
                currentCoroutineContext().ensureActive()
                // Android releases deliberately do not participate in /releases/latest.
                val releases = JSONArray(fetch("https://api.github.com/repos/msgbyte/dao-browser/releases?per_page=100&page=$page"))
                for (index in 0 until releases.length()) {
                    val release = releases.getJSONObject(index)
                    if (release.getBoolean("draft") || release.getBoolean("prerelease")) continue
                    val tag = release.getString("tag_name")
                    if (!tag.startsWith("android-v")) continue
                    val version = parseVersion(tag.removePrefix("android-v")) ?: continue
                    if (compareVersions(version, latestVersion) > 0) {
                        latest = release
                        latestVersion = version
                    }
                }
                if (releases.length() < 100) break
                // Fail explicitly if the repository outgrows this bound; never report a partial scan as current.
                if (++page > 20) throw IOException("Release listing exceeded page limit")
            }
            val release = latest ?: return@withContext null
            val tag = release.getString("tag_name")
            val version = tag.removePrefix("android-v")
            val assets = release.getJSONArray("assets")
            val byName = (0 until assets.length()).map { assets.getJSONObject(it) }
                .associateBy { it.getString("name") }
            val apk = supportedAbis.firstNotNullOfOrNull { abi ->
                byName["dao-browser-$version-android-$abi.apk"]
            } ?: throw NoCompatibleUpdate()
            val fileName = apk.getString("name")
            val prefix = "https://github.com/msgbyte/dao-browser/releases/download/$tag/"
            val url = apk.getString("browser_download_url")
            val sumsUrl = byName["SHA256SUMS"]?.getString("browser_download_url")
            if (url != prefix + fileName || sumsUrl != prefix + "SHA256SUMS") {
                throw IOException("Unexpected update asset URL")
            }
            val hashes = fetch(sumsUrl).lineSequence().mapNotNull { line ->
                Regex("^([0-9a-fA-F]{64}) [ *](.+)$").matchEntire(line)?.destructured
                    ?.let { (hash, name) -> if (name == fileName) hash.lowercase() else null }
            }.toList()
            if (hashes.size != 1) throw IOException("Missing or ambiguous APK checksum")
            val size = apk.getLong("size")
            if (size <= 0) throw IOException("Invalid APK size")
            AppUpdateRelease(version, fileName, url, size, hashes.single(),
                if (release.isNull("body")) "" else release.optString("body").take(20_000))
        }
}

internal fun isNewerAppVersion(candidate: String, installed: String): Boolean {
    val next = parseVersion(candidate) ?: return false
    val current = parseVersion(installed) ?: return false
    return compareVersions(next, current) > 0
}

private fun parseVersion(value: String): List<Long>? {
    if (!Regex("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)").matches(value)) return null
    return value.split('.').map { it.toLongOrNull() ?: return null }
}

private fun compareVersions(first: List<Long>, second: List<Long>): Int =
    first.zip(second).firstOrNull { (a, b) -> a != b }?.let { (a, b) -> a.compareTo(b) } ?: 0

private suspend fun fetchUpdateText(address: String): String = withContext(Dispatchers.IO) {
    val connection = URL(address).openConnection() as HttpURLConnection
    try {
        connection.connectTimeout = 10_000
        connection.readTimeout = 10_000
        connection.setRequestProperty("Accept", "application/vnd.github+json")
        connection.setRequestProperty("User-Agent", "Dao-Browser-Android")
        val status = connection.responseCode
        if (status == 403 || status == 429) {
            val now = System.currentTimeMillis()
            val retryAt = connection.getHeaderField("Retry-After")?.toLongOrNull()
                ?.coerceIn(1, 86_400)?.let { now + it * 1_000 }
                ?: connection.getHeaderField("X-RateLimit-Reset")?.toLongOrNull()?.times(1_000)
                ?: (now + 60_000)
            throw UpdateRateLimited(retryAt.coerceIn(now + 60_000, now + 86_400_000))
        }
        if (status !in 200..299) throw IOException("Update request failed: $status")
        connection.inputStream.use { stream ->
            val result = ByteArrayOutputStream()
            val buffer = ByteArray(8_192)
            while (true) {
                currentCoroutineContext().ensureActive()
                val count = stream.read(buffer)
                if (count < 0) break
                if (result.size() + count > 2_000_000) throw IOException("Update response too large")
                result.write(buffer, 0, count)
            }
            result.toString("UTF-8")
        }
    } finally {
        connection.disconnect()
    }
}
