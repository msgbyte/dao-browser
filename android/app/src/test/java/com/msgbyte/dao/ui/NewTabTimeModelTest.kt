package com.msgbyte.dao.ui

import java.time.LocalDate
import java.util.Locale
import org.junit.Assert.assertEquals
import org.junit.Test

class NewTabTimeModelTest {
    @Test
    fun selectsGreetingFromTheActualHour() {
        assertEquals(GreetingPeriod.MORNING, greetingPeriod(8))
        assertEquals(GreetingPeriod.AFTERNOON, greetingPeriod(15))
        assertEquals(GreetingPeriod.EVENING, greetingPeriod(23))
        assertEquals(GreetingPeriod.EVENING, greetingPeriod(2))
    }

    @Test
    fun formatsTheActualDateWithTheLocalizedResourcePattern() {
        assertEquals(
            "2026-08-03",
            formatNewTabDate(LocalDate.of(2026, 8, 3), "yyyy-MM-dd", Locale.US),
        )
    }
}
