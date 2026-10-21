package com.msgbyte.dao.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.res.painterResource
import com.msgbyte.dao.R

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
    val brand: Color,
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
    brand = Color(0xFF4678BE),
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
    brand = Color(0xFF8AB0E6),
    onAccent = Color(0xFF1B1D22),
    success = Color(0xFF2E9E63),
    warning = Color(0xFFD69A28),
    danger = Color(0xFFD24B3E),
)

// Private tabs live in their own space, painted with the desktop sidebar's dark blue-gray in both
// themes, so the mode reads from the whole screen instead of a label.
val PrivateNovaColors = NovaColors(
    background = Color(0xFF363B40),
    surface = Color(0xFF2C3035),
    surfaceSecondary = Color(0xFF484E55),
    foreground = Color(0xFFF4F5F7),
    muted = Color(0xFFBCC2C8),
    faint = Color(0xFF9AA0A8),
    border = Color(0xFF464C53),
    strongBorder = Color(0xFF575D64),
    accent = Color(0xFFF4F5F7),
    brand = Color(0xFF8AB0E6),
    onAccent = Color(0xFF2A2E33),
    success = Color(0xFF6CC59A),
    warning = Color(0xFFE0A83A),
    danger = Color(0xFFF07A6E),
)

val LocalNovaColors = staticCompositionLocalOf { LightNovaColors }

val LocalDaoDarkTheme = staticCompositionLocalOf { false }

// Picked from Dao's effective theme rather than a night resource qualifier,
// which would follow the system even when the user overrides the theme.
@Composable
fun daoBrandLogoPainter(): Painter = painterResource(
    if (LocalDaoDarkTheme.current) R.drawable.dao_brand_logo_dark else R.drawable.dao_brand_logo,
)

@Composable
fun DaoTheme(
    darkTheme: Boolean = false,
    content: @Composable () -> Unit,
) {
    val novaColors = if (darkTheme) DarkNovaColors else LightNovaColors
    CompositionLocalProvider(
        LocalNovaColors provides novaColors,
        LocalDaoDarkTheme provides darkTheme,
    ) {
        MaterialTheme(
            colorScheme = novaColorScheme(novaColors, darkTheme),
            typography = MaterialTheme.typography,
            content = content,
        )
    }
}

/**
 * Switches [content] into the private space palette while [enabled]. The composition structure is
 * the same either way, so toggling it never recreates hosted views such as the Gecko engine view.
 */
@Composable
fun PrivateSpaceTheme(
    enabled: Boolean,
    content: @Composable () -> Unit,
) {
    CompositionLocalProvider(
        LocalNovaColors provides if (enabled) PrivateNovaColors else LocalNovaColors.current,
        LocalDaoDarkTheme provides (enabled || LocalDaoDarkTheme.current),
        LocalContentColor provides if (enabled) PrivateNovaColors.foreground else LocalContentColor.current,
    ) {
        MaterialTheme(
            colorScheme = if (enabled) novaColorScheme(PrivateNovaColors, darkTheme = true) else MaterialTheme.colorScheme,
            typography = MaterialTheme.typography,
            content = content,
        )
    }
}

private fun novaColorScheme(colors: NovaColors, darkTheme: Boolean): ColorScheme = if (darkTheme) {
    darkColorScheme(
        primary = colors.accent,
        onPrimary = colors.onAccent,
        background = colors.background,
        onBackground = colors.foreground,
        surface = colors.surface,
        onSurface = colors.foreground,
        surfaceVariant = colors.surfaceSecondary,
        secondaryContainer = colors.surfaceSecondary,
        onSecondaryContainer = colors.foreground,
        outline = colors.border,
        error = colors.danger,
    )
} else {
    lightColorScheme(
        primary = colors.accent,
        onPrimary = colors.onAccent,
        background = colors.background,
        onBackground = colors.foreground,
        surface = colors.surface,
        onSurface = colors.foreground,
        surfaceVariant = colors.surfaceSecondary,
        secondaryContainer = colors.surfaceSecondary,
        onSecondaryContainer = colors.foreground,
        outline = colors.border,
        error = colors.danger,
    )
}
