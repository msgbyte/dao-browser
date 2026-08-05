package com.msgbyte.dao.ui

import java.time.LocalDate
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

fun formatHistoryDate(
    date: LocalDate,
    pattern: String,
    locale: Locale = Locale.getDefault(),
): String = DateTimeFormatter.ofPattern(pattern, locale).format(date)

fun formatHistoryTime(
    value: ZonedDateTime,
    locale: Locale = Locale.getDefault(),
): String = DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT)
    .withLocale(locale)
    .format(value)
