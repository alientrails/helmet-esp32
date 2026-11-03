package com.helmet.ledcontrol.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.helmet.ledcontrol.R

// Alien Trails Labs design system, Void theme. Values are the design system's
// tokens verbatim; the firmware's CYD UI uses the same set (include/ui_theme.h).
//
//  1. Dark first: everything starts on Void, panels are raised with Hull and
//     separated by 1dp Line, not shadows.
//  2. Signal is scarce: Phosphor marks what is live (the link, lights on, the
//     running mode) or the one primary action. Nothing decorative is Phosphor.
//  3. Leave a trail: the dotted phosphor-to-ion trail appears once per view —
//     here it is the brightness slider.
object AlienTrails {
    val Void = Color(0xFF050A12)
    val Hull = Color(0xFF0C1520)
    val HullHigh = Color(0xFF142131)
    val Line = Color(0xFF1F2D3F)
    val LineStrong = Color(0xFF4D6480)
    val Ink = Color(0xFFE4F0EA)
    val InkMuted = Color(0xFF8FA3B6)
    val Phosphor = Color(0xFF3DFF8F)
    val PhosphorSoft = Color(0xFF0B2A1B)
    val OnPhosphor = Color(0xFF050A12)
    val Ion = Color(0xFF4AA8FF)
    val IonSoft = Color(0xFF0B2140)
    val OnIon = Color(0xFF050A12)
    val Abyss = Color(0xFF12295F)
    val Amber = Color(0xFFFFB547)
    val Alert = Color(0xFFFF6B78)
}

val SpaceGrotesk = FontFamily(
    Font(R.font.space_grotesk_regular, FontWeight.Normal),
    Font(R.font.space_grotesk_medium, FontWeight.Medium),
    Font(R.font.space_grotesk_semibold, FontWeight.SemiBold),
    Font(R.font.space_grotesk_bold, FontWeight.Bold),
)

val SpaceMono = FontFamily(
    Font(R.font.space_mono_regular, FontWeight.Normal),
    Font(R.font.space_mono_bold, FontWeight.Bold),
)

// The design system's type scale. Grotesk carries everything read as words;
// Mono is the instrument panel — labels, readings, commands. Never paragraphs.
object AtType {
    val display = TextStyle(
        fontFamily = SpaceGrotesk, fontWeight = FontWeight.Bold,
        fontSize = 44.sp, lineHeight = 46.sp, letterSpacing = (-0.02).em,
    )
    val title = TextStyle(
        fontFamily = SpaceGrotesk, fontWeight = FontWeight.SemiBold,
        fontSize = 28.sp, lineHeight = 34.sp, letterSpacing = (-0.01).em,
    )
    val heading = TextStyle(
        fontFamily = SpaceGrotesk, fontWeight = FontWeight.SemiBold,
        fontSize = 20.sp, lineHeight = 28.sp,
    )
    val body = TextStyle(
        fontFamily = SpaceGrotesk, fontWeight = FontWeight.Normal,
        fontSize = 16.sp, lineHeight = 26.sp,
    )
    val small = TextStyle(
        fontFamily = SpaceGrotesk, fontWeight = FontWeight.Normal,
        fontSize = 14.sp, lineHeight = 20.sp,
    )

    /** UPPERCASE eyebrows, tags, tabs. Callers pass upper-case text. */
    val label = TextStyle(
        fontFamily = SpaceMono, fontWeight = FontWeight.Bold,
        fontSize = 12.sp, lineHeight = 16.sp, letterSpacing = 0.14.em,
    )
    val code = TextStyle(
        fontFamily = SpaceMono, fontWeight = FontWeight.Normal,
        fontSize = 14.sp, lineHeight = 22.sp,
    )
    val data = TextStyle(
        fontFamily = SpaceMono, fontWeight = FontWeight.Normal,
        fontSize = 13.sp, lineHeight = 20.sp,
    )
}

private val AtColors = darkColorScheme(
    primary = AlienTrails.Phosphor,
    onPrimary = AlienTrails.OnPhosphor,
    primaryContainer = AlienTrails.PhosphorSoft,
    onPrimaryContainer = AlienTrails.Phosphor,
    secondary = AlienTrails.Ion,
    onSecondary = AlienTrails.OnIon,
    secondaryContainer = AlienTrails.IonSoft,
    onSecondaryContainer = AlienTrails.Ion,
    tertiary = AlienTrails.Ion,
    background = AlienTrails.Void,
    onBackground = AlienTrails.Ink,
    surface = AlienTrails.Void,
    onSurface = AlienTrails.Ink,
    surfaceVariant = AlienTrails.Hull,
    onSurfaceVariant = AlienTrails.InkMuted,
    surfaceContainer = AlienTrails.Hull,
    surfaceContainerHigh = AlienTrails.HullHigh,
    outline = AlienTrails.LineStrong,
    outlineVariant = AlienTrails.Line,
    error = AlienTrails.Alert,
    onError = AlienTrails.Void,
)

private val AtTypography = Typography(
    displaySmall = AtType.display,
    headlineSmall = AtType.title,
    titleLarge = AtType.heading,
    titleMedium = AtType.heading.copy(fontSize = 16.sp, lineHeight = 22.sp),
    bodyLarge = AtType.body,
    bodyMedium = AtType.small,
    labelLarge = AtType.small.copy(fontWeight = FontWeight.SemiBold),
    labelMedium = AtType.label,
    labelSmall = AtType.label.copy(fontSize = 11.sp),
)

@Composable
fun HelmetTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = AtColors, typography = AtTypography, content = content)
}
