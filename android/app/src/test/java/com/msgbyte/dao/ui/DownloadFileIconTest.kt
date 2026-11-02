package com.msgbyte.dao.ui

import com.composables.icons.lucide.File
import com.composables.icons.lucide.FileArchive
import com.composables.icons.lucide.FileAudio
import com.composables.icons.lucide.FileImage
import com.composables.icons.lucide.FileText
import com.composables.icons.lucide.FileVideo
import com.composables.icons.lucide.Lucide
import com.composables.icons.lucide.Package
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class DownloadFileIconTest {
    @Test
    fun commonFileNamesSelectTheirTypeEvenWithAGenericServerType() {
        val cases = mapOf(
            "photo.JPG" to Lucide.FileImage,
            "song.mp3" to Lucide.FileAudio,
            "clip.mp4" to Lucide.FileVideo,
            "backup.zip" to Lucide.FileArchive,
            "backup.7z" to Lucide.FileArchive,
            "report.PDF" to Lucide.FileText,
            "notes.txt" to Lucide.FileText,
            "document.docx" to Lucide.FileText,
            "Dao.APK" to Lucide.Package,
        )
        for ((name, icon) in cases) {
            assertEquals(name, icon, downloadFileIcon(name, "application/octet-stream"))
        }
    }

    @Test
    fun tarAndCabinetArchivesUseArchiveIcons() {
        val cases = mapOf(
            "backup.tgz" to "application/x-gtar-compressed",
            "backup.taz" to "application/x-gtar-compressed",
            "backup.gtar" to "application/x-gtar",
            "backup.cab" to "application/vnd.ms-cab-compressed",
        )
        for ((name, mimeType) in cases) {
            assertEquals(name, Lucide.FileArchive, downloadFileIcon(name, "application/octet-stream"))
            assertEquals(mimeType, Lucide.FileArchive, downloadFileIcon("download", mimeType))
        }
    }

    @Test
    fun officeDocumentsKeepDocumentIcons() {
        val cases = mapOf(
            "document.doc" to "application/msword",
            "document.xls" to "application/vnd.ms-excel",
            "document.ppt" to "application/vnd.ms-powerpoint",
            "document.docm" to "application/vnd.ms-word.document.macroEnabled.12",
            "document.xlsm" to "application/vnd.ms-excel.sheet.macroEnabled.12",
            "document.xlsb" to "application/vnd.ms-excel.sheet.binary.macroEnabled.12",
            "document.pptm" to "application/vnd.ms-powerpoint.presentation.macroEnabled.12",
            "document.wps" to "application/vnd.ms-works",
            "document.xps" to "application/vnd.ms-xpsdocument",
            "project.mpp" to "application/vnd.ms-project",
            "template.mpt" to "application/vnd.ms-project",
        )
        for ((name, mimeType) in cases) {
            assertEquals(name, Lucide.FileText, downloadFileIcon(name, "application/octet-stream"))
            assertEquals(mimeType, Lucide.FileText, downloadFileIcon("download", mimeType))
        }
    }

    @Test
    fun missingOrUnknownExtensionsUseTheNormalizedServerType() {
        assertEquals(Lucide.FileImage, downloadFileIcon("download", " IMAGE/PNG; charset=UTF-8 "))
        assertEquals(Lucide.FileVideo, downloadFileIcon("download.unknown", "video/webm"))
        assertEquals(Lucide.FileText, downloadFileIcon("download", "application/pdf"))
    }

    @Test
    fun recognizedExtensionsTakePrecedenceOverMisleadingServerTypes() {
        assertEquals(Lucide.Package, downloadFileIcon("Dao.apk", "application/zip"))
        assertEquals(Lucide.FileImage, downloadFileIcon("photo.png", "text/plain"))
    }

    @Test
    fun unknownOrMissingTypesUseTheGenericFileIcon() {
        assertEquals(Lucide.File, downloadFileIcon("download", null))
        assertEquals(Lucide.File, downloadFileIcon("unknown.xyz123", "application/octet-stream"))
        assertEquals(Lucide.File, downloadFileIcon("download", ""))
        assertEquals(Lucide.File, downloadFileIcon("font.eot", "application/octet-stream"))
        assertEquals(Lucide.File, downloadFileIcon("download", "application/vnd.ms-fontobject"))
    }
}
