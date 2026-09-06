package com.helmet.ledcontrol.ui.theme

import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val HelmetDarkColors = darkColorScheme(
    primary = Color(0xFF9ACBFF),
    onPrimary = Color(0xFF00325A),
    primaryContainer = Color(0xFF00497F),
    onPrimaryContainer = Color(0xFFD2E4FF),
    secondary = Color(0xFFBBC7DB),
    background = Color(0xFF10131A),
    onBackground = Color(0xFFE1E2E8),
    surface = Color(0xFF191C23),
    onSurface = Color(0xFFE1E2E8),
    surfaceVariant = Color(0xFF262A33),
    onSurfaceVariant = Color(0xFFC4C6D0),
    error = Color(0xFFFFB4AB),
)

@Composable
fun HelmetTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = HelmetDarkColors, content = content)
}
