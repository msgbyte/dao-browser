package com.msgbyte.dao.browser

import java.io.IOException
import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class AppUpdateRepositoryTest {
    private val checksum = "ab".repeat(32)

    @Test
    fun selectsNumericAndroidVersionAndPreferredAbiAcrossPages() = runBlocking {
        val first = JSONArray().apply {
            repeat(98) { put(release("v1.0.$it")) }
            put(release("android-v0.1.9"))
            put(release("android-v9.0.0").put("prerelease", true))
        }
        val urls = mutableListOf<String>()
        val repository = AppUpdateRepository { url ->
            urls += url
            when {
                url.endsWith("SHA256SUMS") -> "$checksum  dao-browser-0.1.10-android-arm64-v8a.apk\n"
                url.endsWith("page=1") -> first.toString()
                else -> JSONArray().put(release("android-v0.1.10")).toString()
            }
        }
        val update = repository.findUpdate("0.1.2", listOf("arm64-v8a", "armeabi-v7a"))!!
        assertEquals("0.1.10", update.version)
        assertEquals("dao-browser-0.1.10-android-arm64-v8a.apk", update.fileName)
        assertEquals(checksum, update.sha256)
        assertEquals(3, urls.size)
    }

    @Test
    fun ignoresDraftsAndNeverOffersEqualOrOlderVersions() = runBlocking {
        val repository = AppUpdateRepository {
            JSONArray().put(release("android-v0.1.9"))
                .put(release("android-v0.1.10"))
                .put(release("android-v0.2.0").put("draft", true))
                .put(release("android-v1.0.0-beta.1")).toString()
        }
        assertNull(repository.findUpdate("0.1.10", listOf("arm64-v8a")))
    }

    @Test
    fun incompatibleAndUnverifiableReleasesAreErrorsNotUpToDate() = runBlocking {
        val repository = AppUpdateRepository { url ->
            if (url.endsWith("SHA256SUMS")) "$checksum  different.apk"
            else JSONArray().put(release("android-v0.2.0")).toString()
        }
        assertTrue(runCatching { repository.findUpdate("0.1.2", listOf("x86")) }.exceptionOrNull() is IOException)
        assertTrue(runCatching { repository.findUpdate("0.1.2", listOf("arm64-v8a")) }.exceptionOrNull() is IOException)
        assertTrue(runCatching { repository.findUpdate("invalid", listOf("arm64-v8a")) }.isFailure)
    }

    private fun release(tag: String): JSONObject {
        val version = tag.removePrefix("android-v")
        val prefix = "https://github.com/msgbyte/dao-browser/releases/download/$tag/"
        return JSONObject().put("tag_name", tag).put("draft", false).put("prerelease", false)
            .put("body", "Release notes")
            .put("assets", JSONArray().apply {
                listOf("armeabi-v7a", "arm64-v8a").forEach { abi ->
                    val name = "dao-browser-$version-android-$abi.apk"
                    put(JSONObject().put("name", name).put("size", 123L).put("browser_download_url", prefix + name))
                }
                put(JSONObject().put("name", "SHA256SUMS").put("browser_download_url", prefix + "SHA256SUMS"))
            })
    }
}
