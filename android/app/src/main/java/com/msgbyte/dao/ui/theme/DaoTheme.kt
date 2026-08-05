package com.msgbyte.dao.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

@Immutable
data class NovaColors(
    val background: Color,
    val surface: Color,
    val surfaceSecondary: Color,
    val foreground: Color,
    val muted: Color,
    val faint: Color,
    val border: Color,
    val strongBorder: Color,
    val accent: Color,
    val onAccent: Color,
    val success: Color,
    val warning: Color,
    val danger: Color,
)

val LightNovaColors = NovaColors(
    background = Color(0xFFFAFAFB),
    surface = Color(0xFFFFFFFF),
    surfaceSecondary = Color(0xFFF2F3F5),
    foreground = Color(0xFF1C1E23),
    muted = Color(0xFF71757E),
    faint = Color(0xFFA6A9B0),
    border = Color(0xFFE6E7EA),
    strongBorder = Color(0xFFD9DBDF),
    accent = Color(0xFF26282E),
    onAccent = Color(0xFFFCFCFC),
    success = Color(0xFF2E9E63),
    warning = Color(0xFFD69A28),
    danger = Color(0xFFD24B3E),
)

val DarkNovaColors = NovaColors(
    background = Color(0xFF17181C),
    surface = Color(0xFF1F2126),
    surfaceSecondary = Color(0xFF282A30),
    foreground = Color(0xFFF1F1F3),
    muted = Color(0xFF9EA1A9),
    faint = Color(0xFF6E717A),
    border = Color(0xFF33353B),
    strongBorder = Color(0xFF3F424A),
    accent = Color(0xFFF4F4F6),
    onAccent = Color(0xFF1B1D22),
    success = Color(0xFF2E9E63),
    warning = Color(0xFFD69A28),
    danger = Color(0xFFD24B3E),
)

val LocalNovaColors = staticCompositionLocalOf { LightNovaColors }

@Composable
fun DaoTheme(
    darkTheme: Boolean = false,
    content: @Composable () -> Unit,
) {
    val novaColors = if (darkTheme) DarkNovaColors else LightNovaColors
    val materialColors = if (darkTheme) {
        darkColorScheme(
            primary = novaColors.accent,
            onPrimary = novaColors.onAccent,
            background = novaColors.background,
            onBackground = novaColors.foreground,
            surface = novaColors.surface,
            onSurface = novaColors.foreground,
            surfaceVariant = novaColors.surfaceSecondary,
            outline = novaColors.border,
            error = novaColors.danger,
        )
    } else {
        lightColorScheme(
            primary = novaColors.accent,
            onPrimary = novaColors.onAccent,
            background = novaColors.background,
            onBackground = novaColors.foreground,
            surface = novaColors.surface,
            onSurface = novaColors.foreground,
            surfaceVariant = novaColors.surfaceSecondary,
            outline = novaColors.border,
            error = novaColors.danger,
        )
    }

    androidx.compose.runtime.CompositionLocalProvider(LocalNovaColors provides novaColors) {
        MaterialTheme(
            colorScheme = materialColors,
            typography = MaterialTheme.typography,
            content = content,
        )
    }
}
