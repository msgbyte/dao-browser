package com.msgbyte.dao.browser

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class ExtensionPackageStagerTest {
    @Test
    fun acceptsXpiFileNamesCaseInsensitively() {
        assertEquals("addon.xpi", ExtensionPackageStager.requireXpiName("addon.xpi"))
        assertEquals("ADDON.XPI", ExtensionPackageStager.requireXpiName("ADDON.XPI"))
    }

    @Test
    fun rejectsMissingOrNonXpiFileNames() {
        assertThrows(ExtensionPackageException.InvalidFileName::class.java) {
            ExtensionPackageStager.requireXpiName(null)
        }
        assertThrows(ExtensionPackageException.InvalidFileName::class.java) {
            ExtensionPackageStager.requireXpiName("addon.zip")
        }
    }

    @Test
    fun copiesAFileAtTheConfiguredLimit() {
        val output = ByteArrayOutputStream()

        val copied = ExtensionPackageStager.copyBounded(
            input = ByteArrayInputStream(byteArrayOf(1, 2, 3, 4)),
            output = output,
            maxBytes = 4,
        )

        assertEquals(4, copied)
        assertArrayEquals(byteArrayOf(1, 2, 3, 4), output.toByteArray())
    }

    @Test
    fun rejectsEmptyFiles() {
        assertThrows(ExtensionPackageException.EmptyFile::class.java) {
            ExtensionPackageStager.copyBounded(
                input = ByteArrayInputStream(byteArrayOf()),
                output = ByteArrayOutputStream(),
                maxBytes = 4,
            )
        }
    }

    @Test
    fun rejectsTheFirstByteBeyondTheConfiguredLimit() {
        assertThrows(ExtensionPackageException.TooLarge::class.java) {
            ExtensionPackageStager.copyBounded(
                input = ByteArrayInputStream(byteArrayOf(1, 2, 3, 4, 5)),
                output = ByteArrayOutputStream(),
                maxBytes = 4,
            )
        }
    }
}
