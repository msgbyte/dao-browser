package com.msgbyte.dao.ui

import java.time.LocalDate
import java.time.ZoneId
import java.time.ZonedDateTime
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Test

class HistoryTimeModelTest {
    @Test
    fun formatsHistoryDatesWithTheSelectedResourcePatternAndLocale() {
        val date = LocalDate.of(2026, 8, 3)

        assertEquals("Aug 3", formatHistoryDate(date, "MMM d", Locale.US))
        assertEquals("8月3日", formatHistoryDate(date, "M月d日", Locale.SIMPLIFIED_CHINESE))
    }

    @Test
    fun formatsHistoryTimesWithTheSelectedLocale() {
        val time = ZonedDateTime.of(2026, 8, 3, 14, 5, 0, 0, ZoneId.of("UTC"))

        assertEquals("2:05 PM", formatHistoryTime(time, Locale.US))
        assertEquals("下午2:05", formatHistoryTime(time, Locale.SIMPLIFIED_CHINESE))
    }
}
