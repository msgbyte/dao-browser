package com.msgbyte.dao.ui

import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.util.Locale

enum class GreetingPeriod {
    MORNING,
    AFTERNOON,
    EVENING,
}

fun greetingPeriod(hour: Int): GreetingPeriod = when (hour) {
    in 5..11 -> GreetingPeriod.MORNING
    in 12..17 -> GreetingPeriod.AFTERNOON
    else -> GreetingPeriod.EVENING
}

fun formatNewTabDate(
    date: LocalDate,
    pattern: String,
    locale: Locale = Locale.getDefault(),
): String = DateTimeFormatter.ofPattern(pattern, locale).format(date)
