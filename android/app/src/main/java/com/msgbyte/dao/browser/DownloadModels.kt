package com.msgbyte.dao.browser

data class DownloadRequestData(
    val url: String,
    val fileName: String,
    val contentLength: Long? = null,
    val contentType: String? = null,
    val cookie: String? = null,
    val userAgent: String? = null,
)

enum class DownloadStatus {
    PENDING,
    RUNNING,
    PAUSED,
    SUCCESSFUL,
    FAILED,
}

data class BrowserDownload(
    val id: Long,
    val request: DownloadRequestData,
    val status: DownloadStatus,
    val bytesDownloaded: Long,
    val totalBytes: Long?,
    val localUri: String?,
    val reason: Int,
    val lastModified: Long,
) {
    val progress: Float?
        get() = totalBytes?.takeIf { it > 0 }?.let { total ->
            (bytesDownloaded.toDouble() / total.toDouble()).toFloat().coerceIn(0f, 1f)
        }
}

internal enum class DownloadGatewayStatus {
    PENDING,
    RUNNING,
    PAUSED,
    SUCCESSFUL,
    FAILED,
}

internal data class DownloadGatewayRecord(
    val id: Long,
    val status: DownloadGatewayStatus,
    val bytesDownloaded: Long,
    val totalBytes: Long,
    val localUri: String?,
    val reason: Int,
    val lastModified: Long,
)

internal interface DownloadGateway {
    fun enqueue(request: DownloadRequestData): Long
    fun query(ids: Set<Long>): List<DownloadGatewayRecord>
    fun remove(id: Long)
}

internal interface DownloadMetadataStore {
    fun readAll(): Map<Long, DownloadRequestData>
    fun writeAll(values: Map<Long, DownloadRequestData>)
}
