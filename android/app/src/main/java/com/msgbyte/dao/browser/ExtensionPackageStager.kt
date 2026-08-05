package com.msgbyte.dao.browser

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID

sealed class ExtensionPackageException(cause: Throwable? = null) : Exception(cause) {
    class InvalidFileName : ExtensionPackageException()
    class EmptyFile : ExtensionPackageException()
    class TooLarge : ExtensionPackageException()
    class Unavailable(cause: Throwable? = null) : ExtensionPackageException(cause)
}

data class StagedExtensionPackage(
    val uri: String,
    val file: File,
) {
    fun delete() {
        file.delete()
    }
}

class ExtensionPackageStager(
    private val context: Context,
) {
    fun stage(documentUri: Uri): StagedExtensionPackage {
        requireXpiName(displayName(documentUri))
        val directory = File(context.cacheDir, CACHE_DIRECTORY).also(File::mkdirs)
        val target = File(directory, "${UUID.randomUUID()}.xpi")
        try {
            val input = context.contentResolver.openInputStream(documentUri)
                ?: throw ExtensionPackageException.Unavailable()
            input.use { source ->
                target.outputStream().use { output ->
                    copyBounded(source, output, MAX_PACKAGE_BYTES)
                }
            }
            return StagedExtensionPackage(target.toURI().toString(), target)
        } catch (error: ExtensionPackageException) {
            target.delete()
            throw error
        } catch (error: Exception) {
            target.delete()
            throw ExtensionPackageException.Unavailable(error)
        }
    }

    private fun displayName(uri: Uri): String? {
        context.contentResolver.query(
            uri,
            arrayOf(OpenableColumns.DISPLAY_NAME),
            null,
            null,
            null,
        )?.use { cursor ->
            val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) return cursor.getString(index)
        }
        return uri.lastPathSegment
    }

    companion object {
        private const val CACHE_DIRECTORY = "extension-installs"
        const val MAX_PACKAGE_BYTES = 100L * 1024L * 1024L

        internal fun requireXpiName(name: String?): String {
            if (name.isNullOrBlank() || !name.endsWith(".xpi", ignoreCase = true)) {
                throw ExtensionPackageException.InvalidFileName()
            }
            return name
        }

        internal fun copyBounded(
            input: InputStream,
            output: OutputStream,
            maxBytes: Long,
        ): Long {
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            var total = 0L
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                if (total + read > maxBytes) throw ExtensionPackageException.TooLarge()
                output.write(buffer, 0, read)
                total += read
            }
            if (total == 0L) throw ExtensionPackageException.EmptyFile()
            return total
        }
    }
}
