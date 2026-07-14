package com.example.hellokotlin.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val LightColors = lightColorScheme(
    primary = CourtGreen,
    onPrimary = Color.White,
    primaryContainer = CourtGreenLight,
    onPrimaryContainer = CourtGreenDark,
    secondary = ShuttleOrange,
    onSecondary = Color.White,
    secondaryContainer = ShuttleOrangeLight,
    onSecondaryContainer = Color(0xFF5D3A00),
    background = BackgroundLight,
    onBackground = Slate900,
    surface = CardWhite,
    onSurface = Slate900,
    surfaceVariant = Slate200,
    onSurfaceVariant = Slate600,
    outline = Slate200
)

private val DarkColors = darkColorScheme(
    primary = Color(0xFF4CAF78),
    onPrimary = Color(0xFF003822),
    primaryContainer = CourtGreenDark,
    onPrimaryContainer = Color(0xFFB8E6CC),
    secondary = ShuttleOrange,
    background = Color(0xFF121820),
    surface = Color(0xFF1E2630),
    onBackground = Color(0xFFE8EDF2),
    onSurface = Color(0xFFE8EDF2)
)

@Composable
fun BadmintonTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colors = if (darkTheme && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        DarkColors
    } else if (darkTheme) {
        DarkColors
    } else {
        LightColors
    }

    MaterialTheme(
        colorScheme = colors,
        content = content
    )
}
