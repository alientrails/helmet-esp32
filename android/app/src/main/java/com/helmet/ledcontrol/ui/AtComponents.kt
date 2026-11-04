package com.helmet.ledcontrol.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.helmet.ledcontrol.ui.theme.AlienTrails
import com.helmet.ledcontrol.ui.theme.AtType

// Spacing (4dp base) and machined, small radii from the design system.
object AtSpace {
    val s1 = 4.dp
    val s2 = 8.dp
    val s3 = 12.dp
    val s4 = 16.dp
    val s6 = 24.dp
    val s8 = 32.dp
    val s12 = 48.dp
}

val RadiusSm = RoundedCornerShape(2.dp)
val RadiusMd = RoundedCornerShape(6.dp)

/** UPPERCASE Space Mono label in ink-muted — "BRIGHTNESS", "MODES". */
@Composable
fun Eyebrow(text: String, modifier: Modifier = Modifier, color: Color = AlienTrails.InkMuted) {
    Text(text.uppercase(), style = AtType.label, color = color, modifier = modifier)
}

/** A raised hull panel with a 1dp hairline edge. Borders over shadows. */
@Composable
fun Panel(
    modifier: Modifier = Modifier,
    padding: PaddingValues = PaddingValues(AtSpace.s4),
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(AlienTrails.Hull, RadiusMd)
            .border(1.dp, AlienTrails.Line, RadiusMd)
            .padding(padding),
        content = content,
    )
}

/** The five states a SignalTag can show. Map every new state onto these. */
enum class Signal { ONLINE, SCANNING, IDLE, WARN, ALERT }

/**
 * A one- or two-word status reading with a pip whose shape carries the
 * meaning — disc, ring, diamond, square — so it reads without colour.
 */
@Composable
fun SignalTag(signal: Signal, word: String, modifier: Modifier = Modifier) {
    val (fg, bg, border) = when (signal) {
        Signal.ONLINE -> Triple(AlienTrails.Phosphor, AlienTrails.PhosphorSoft, Color.Transparent)
        Signal.SCANNING -> Triple(AlienTrails.Ion, AlienTrails.IonSoft, Color.Transparent)
        Signal.IDLE -> Triple(AlienTrails.InkMuted, AlienTrails.Hull, AlienTrails.Line)
        Signal.WARN -> Triple(AlienTrails.Amber, AlienTrails.Hull, AlienTrails.Line)
        Signal.ALERT -> Triple(AlienTrails.Alert, AlienTrails.Hull, AlienTrails.Line)
    }
    Row(
        modifier = modifier
            .background(bg, RadiusSm)
            .border(1.dp, border, RadiusSm)
            .padding(horizontal = AtSpace.s2, vertical = AtSpace.s1),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(AtSpace.s2),
    ) {
        val pip = Modifier.size(8.dp)
        when (signal) {
            Signal.SCANNING -> Box(pip.border(2.dp, fg, CircleShape))
            Signal.WARN -> Box(Modifier.size(7.dp).rotate(45f).background(fg, RectangleShape))
            Signal.ALERT -> Box(pip.background(fg, RectangleShape))
            else -> Box(pip.background(fg, CircleShape))
        }
        Text(word.uppercase(), style = AtType.label, color = fg)
    }
}

/** The one phosphor primary per view. */
@Composable
fun PrimaryButton(text: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    Button(
        onClick = onClick,
        modifier = modifier.height(44.dp),
        shape = RadiusMd,
        colors = ButtonDefaults.buttonColors(
            containerColor = AlienTrails.Phosphor,
            contentColor = AlienTrails.OnPhosphor,
        ),
        contentPadding = PaddingValues(horizontal = AtSpace.s4),
    ) {
        Text(text, style = AtType.small.copy(fontWeight = FontWeight.SemiBold))
    }
}

/** Secondary: transparent, line-strong outline, ink label. */
@Composable
fun SecondaryButton(text: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    OutlinedButton(
        onClick = onClick,
        modifier = modifier.height(44.dp),
        shape = RadiusMd,
        border = BorderStroke(1.dp, AlienTrails.LineStrong),
        colors = ButtonDefaults.outlinedButtonColors(contentColor = AlienTrails.Ink),
        contentPadding = PaddingValues(horizontal = AtSpace.s4),
    ) {
        Text(text, style = AtType.small.copy(fontWeight = FontWeight.SemiBold))
    }
}

@Composable
fun PowerSwitch(checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Switch(
        checked = checked,
        onCheckedChange = onCheckedChange,
        modifier = Modifier.semantics { contentDescription = "Lights" },
        colors = SwitchDefaults.colors(
            checkedThumbColor = AlienTrails.OnPhosphor,
            checkedTrackColor = AlienTrails.Phosphor,
            checkedBorderColor = AlienTrails.Phosphor,
            uncheckedThumbColor = AlienTrails.InkMuted,
            uncheckedTrackColor = AlienTrails.HullHigh,
            uncheckedBorderColor = AlienTrails.LineStrong,
        ),
    )
}

/**
 * Brightness as the brand's trail: a row of dots that fades phosphor to ion up
 * to the value, and dim line-strong dots past it, ending in a glowing ion
 * signal dot that you drag. Tap anywhere to jump.
 */
@Composable
fun TrailSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    onValueChangeFinished: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val change by rememberUpdatedState(onValueChange)
    val finished by rememberUpdatedState(onValueChangeFinished)
    val edge = 10.dp

    Canvas(
        modifier = modifier
            .fillMaxWidth()
            .height(36.dp)
            .semantics {
                contentDescription = "Brightness"
                progressBarRangeInfo = ProgressBarRangeInfo(value, 0f..1f)
                setProgress { target ->
                    change(target.coerceIn(0f, 1f))
                    finished()
                    true
                }
            }
            .pointerInput(Unit) {
                val inset = edge.toPx()
                fun at(x: Float) = ((x - inset) / (size.width - 2 * inset)).coerceIn(0f, 1f)
                detectTapGestures { change(at(it.x)); finished() }
            }
            .pointerInput(Unit) {
                val inset = edge.toPx()
                fun at(x: Float) = ((x - inset) / (size.width - 2 * inset)).coerceIn(0f, 1f)
                detectHorizontalDragGestures(
                    onDragStart = { change(at(it.x)) },
                    onDragEnd = { finished() },
                    onDragCancel = { finished() },
                    onHorizontalDrag = { c, _ -> change(at(c.position.x)) },
                )
            },
    ) {
        val inset = edge.toPx()
        val track = size.width - 2 * inset
        val cy = size.height / 2
        val knobX = inset + track * value.coerceIn(0f, 1f)
        val pitch = 8.dp.toPx()

        var x = inset
        while (x <= inset + track + 0.5f) {
            if (x <= knobX) {
                val t = ((x - inset) / track).coerceIn(0f, 1f)
                drawCircle(lerp(AlienTrails.Phosphor, AlienTrails.Ion, t), 2.5.dp.toPx(), Offset(x, cy))
            } else {
                drawCircle(AlienTrails.LineStrong, 1.25.dp.toPx(), Offset(x, cy))
            }
            x += pitch
        }

        // The signal dot, with the ion glow: light leaking from something live.
        drawCircle(AlienTrails.Ion.copy(alpha = 0.18f), 16.dp.toPx(), Offset(knobX, cy))
        drawCircle(AlienTrails.Ion.copy(alpha = 0.30f), 11.dp.toPx(), Offset(knobX, cy))
        drawCircle(AlienTrails.Ion, 8.dp.toPx(), Offset(knobX, cy))
    }
}

/** The nine patterns as a 3x3 grid. The running one is tinted phosphor. */
@Composable
fun ModeGrid(names: List<String>, selected: Int, onSelect: (Int) -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(AtSpace.s2)) {
        names.chunked(3).forEachIndexed { row, chunk ->
            Row(horizontalArrangement = Arrangement.spacedBy(AtSpace.s2)) {
                chunk.forEachIndexed { col, name ->
                    val index = row * 3 + col
                    ModeCell(
                        name = name,
                        active = index == selected,
                        onClick = { onSelect(index) },
                        modifier = Modifier.weight(1f),
                    )
                }
                repeat(3 - chunk.size) { Spacer(Modifier.weight(1f)) }
            }
        }
    }
}

@Composable
private fun ModeCell(name: String, active: Boolean, onClick: () -> Unit, modifier: Modifier) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val bg = when {
        active -> AlienTrails.PhosphorSoft
        pressed -> AlienTrails.HullHigh
        else -> AlienTrails.Hull
    }
    val edge = when {
        active -> AlienTrails.Phosphor
        pressed -> AlienTrails.LineStrong
        else -> AlienTrails.Line
    }
    Box(
        modifier = modifier
            .height(56.dp)
            .background(bg, RadiusMd)
            .border(1.dp, edge, RadiusMd)
            .clickable(interactionSource = interaction, indication = null, role = Role.RadioButton, onClick = onClick)
            .semantics { selected = active },
        contentAlignment = Alignment.Center,
    ) {
        Text(
            name,
            style = AtType.small.copy(fontWeight = FontWeight.SemiBold),
            color = if (active) AlienTrails.Phosphor else AlienTrails.InkMuted,
            textAlign = TextAlign.Center,
        )
    }
}

/** A thin 1dp Line hairline. */
@Composable
fun Hairline(modifier: Modifier = Modifier) {
    Box(modifier.fillMaxWidth().height(1.dp).background(AlienTrails.Line))
}

@Composable
fun VerticalHairline(height: Dp, modifier: Modifier = Modifier) {
    Box(modifier.width(1.dp).height(height).background(AlienTrails.Line))
}
