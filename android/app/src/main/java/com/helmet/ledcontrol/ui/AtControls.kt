package com.helmet.ledcontrol.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.helmet.ledcontrol.BassMeter
import com.helmet.ledcontrol.ui.theme.AlienTrails
import com.helmet.ledcontrol.ui.theme.AtType
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.roundToInt
import kotlin.math.sin

// Controls added for parity with the CYD: everything that is not brightness.
// Brightness alone is the trail (house rule 3: once per view), so every other
// slider here is a quiet ion bar, matching the CYD's speed, strip and bass
// sliders.

/** A plain ion slider: line-strong track, ion fill and knob, no glow. */
@Composable
fun IonSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    onValueChangeFinished: () -> Unit,
    label: String,
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
                contentDescription = label
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
        val h = 4.dp.toPx()
        val corner = CornerRadius(h / 2, h / 2)
        drawRoundRect(AlienTrails.LineStrong, Offset(inset, cy - h / 2), Size(track, h), corner)
        if (knobX > inset) {
            drawRoundRect(AlienTrails.Ion, Offset(inset, cy - h / 2), Size(knobX - inset, h), corner)
        }
        drawCircle(AlienTrails.Ion, 8.dp.toPx(), Offset(knobX, cy))
    }
}

/**
 * An eyebrow, a live reading and an [IonSlider], over an integer [range].
 * While dragging it shows and sends the dragged value, so the helmet follows
 * your finger — which is the whole point when you are setting a gate against
 * the meter. [snap] rounds to a step, e.g. 50 mA for the current budget.
 */
@Composable
fun SettingSlider(
    title: String,
    value: Int,
    range: IntRange,
    reading: (Int) -> String,
    onChange: (Int) -> Unit,
    modifier: Modifier = Modifier,
    snap: Int = 1,
) {
    var drag by remember { mutableStateOf<Int?>(null) }
    val shown = drag ?: value
    val span = (range.last - range.first).coerceAtLeast(1)

    Column(modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Eyebrow(title, Modifier.weight(1f))
            Text(reading(shown), style = AtType.data, color = AlienTrails.Ink)
        }
        IonSlider(
            value = (shown - range.first).toFloat() / span,
            onValueChange = { f ->
                val raw = range.first + (f * span).roundToInt()
                val v = (((raw + snap / 2) / snap) * snap).coerceIn(range)
                if (v != drag) {
                    drag = v
                    onChange(v)
                }
            },
            onValueChangeFinished = { drag = null },
            label = title,
        )
    }
}

/**
 * One choice in a row of them, or an independent on/off switch: the running
 * choice is phosphor on phosphor-soft, the same as the running mode. Mono
 * label, like the CYD's option buttons.
 */
@Composable
fun OptionCell(
    text: String,
    active: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    isSwitch: Boolean = false,
) {
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
            .height(44.dp)
            .background(bg, RadiusMd)
            .border(1.dp, edge, RadiusMd)
            .clickable(
                interactionSource = interaction,
                indication = null,
                role = if (isSwitch) Role.Switch else Role.RadioButton,
                onClick = onClick,
            )
            .semantics {
                if (isSwitch) {
                    stateDescription = if (active) "On" else "Off"
                } else {
                    selected = active
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text.uppercase(),
            style = AtType.label,
            color = if (active) AlienTrails.Phosphor else AlienTrails.InkMuted,
            textAlign = TextAlign.Center,
        )
    }
}

/**
 * A row of mutually exclusive choices, optionally followed — set apart, so it
 * does not read as one more choice — by an independent toggle, the way the
 * CYD lays out BAND + HUM and COLOUR + FLASH.
 */
@Composable
fun OptionRow(
    options: List<String>,
    selected: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
    toggle: (@Composable (Modifier) -> Unit)? = null,
) {
    Row(modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(AtSpace.s2)) {
        options.forEachIndexed { i, name ->
            OptionCell(name, i == selected, { onSelect(i) }, Modifier.weight(1f))
        }
        if (toggle != null) {
            Spacer(Modifier.width(AtSpace.s2))
            toggle(Modifier.weight(0.9f))
        }
    }
}

/**
 * Hue ring with the current colour in the middle, like the CYD's colour
 * wheel: it picks hue at full saturation and value, and the presets below it
 * cover the warm and neutral whites a hue ring cannot reach. Drag or tap
 * anywhere around the ring.
 */
@Composable
fun HueRing(
    r: Int,
    g: Int,
    b: Int,
    onPick: (Int, Int, Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val pick by rememberUpdatedState(onPick)
    val hue = remember(r, g, b) { hueOf(r, g, b) }
    val ringColours = remember {
        listOf(
            Color.Red, Color.Yellow, Color.Green, Color.Cyan, Color.Blue, Color.Magenta, Color.Red,
        )
    }

    Box(modifier.size(220.dp), contentAlignment = Alignment.Center) {
        Canvas(
            Modifier
                .fillMaxSize()
                .semantics { contentDescription = "Colour ring" }
                .pointerInput(Unit) {
                    fun at(p: Offset) {
                        val dx = p.x - size.width / 2f
                        val dy = p.y - size.height / 2f
                        // The middle of the ring has no meaningful angle.
                        if (hypot(dx, dy) < size.width * 0.2f) return
                        val deg = ((atan2(dy, dx) * 180.0 / PI) + 360.0) % 360.0
                        val rgb = android.graphics.Color.HSVToColor(floatArrayOf(deg.toFloat(), 1f, 1f))
                        pick((rgb shr 16) and 0xFF, (rgb shr 8) and 0xFF, rgb and 0xFF)
                    }
                    detectTapGestures { at(it) }
                }
                .pointerInput(Unit) {
                    fun at(p: Offset) {
                        val dx = p.x - size.width / 2f
                        val dy = p.y - size.height / 2f
                        if (hypot(dx, dy) < size.width * 0.2f) return
                        val deg = ((atan2(dy, dx) * 180.0 / PI) + 360.0) % 360.0
                        val rgb = android.graphics.Color.HSVToColor(floatArrayOf(deg.toFloat(), 1f, 1f))
                        pick((rgb shr 16) and 0xFF, (rgb shr 8) and 0xFF, rgb and 0xFF)
                    }
                    detectDragGestures(onDragStart = { at(it) }) { change, _ -> at(change.position) }
                },
        ) {
            val stroke = 22.dp.toPx()
            val radius = size.minDimension / 2 - stroke / 2
            // Sweep gradients start at 3 o'clock and run clockwise on screen,
            // the same way atan2 measures with y pointing down, so hue h sits
            // at angle h.
            drawCircle(Brush.sweepGradient(ringColours), radius, style = Stroke(stroke))
            if (hue != null) {
                val a = hue * PI / 180.0
                val p = Offset(
                    center.x + (cos(a) * radius).toFloat(),
                    center.y + (sin(a) * radius).toFloat(),
                )
                drawCircle(AlienTrails.Void, stroke / 2, p, style = Stroke(3.dp.toPx()))
                drawCircle(AlienTrails.Ink, stroke / 2 - 3.dp.toPx(), p, style = Stroke(2.dp.toPx()))
            }
        }
        Box(
            Modifier
                .size(116.dp)
                .background(Color(r, g, b), CircleShape)
                .border(1.dp, AlienTrails.Line, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                "#%02X%02X%02X".format(r, g, b),
                style = AtType.data,
                color = contrastOn(r, g, b),
            )
        }
    }
}

/** Hue in degrees, or null for a grey/white that has no hue to mark. */
private fun hueOf(r: Int, g: Int, b: Int): Float? {
    val hsv = FloatArray(3)
    android.graphics.Color.RGBToHSV(r, g, b, hsv)
    return if (hsv[1] < 0.15f) null else hsv[0]
}

/** Void or ink text, whichever stays legible on an arbitrary swatch. */
fun contrastOn(r: Int, g: Int, b: Int): Color {
    val luma = 0.2126f * r + 0.7152f * g + 0.0722f * b
    return if (luma > 140f) AlienTrails.Void else AlienTrails.Ink
}

/** The preset swatches, six to a row. The current colour is ringed in ion. */
@Composable
fun PresetGrid(
    presets: List<Int>,
    r: Int,
    g: Int,
    b: Int,
    onPick: (Int, Int, Int) -> Unit,
) {
    val current = (r shl 16) or (g shl 8) or b
    Column(verticalArrangement = Arrangement.spacedBy(AtSpace.s2)) {
        presets.chunked(6).forEach { row ->
            Row(horizontalArrangement = Arrangement.spacedBy(AtSpace.s2)) {
                row.forEach { rgb ->
                    val pr = (rgb shr 16) and 0xFF
                    val pg = (rgb shr 8) and 0xFF
                    val pb = rgb and 0xFF
                    val active = rgb == current
                    Box(
                        Modifier
                            .weight(1f)
                            .height(40.dp)
                            .background(Color(pr, pg, pb), RadiusMd)
                            .border(
                                if (active) 2.dp else 1.dp,
                                if (active) AlienTrails.Ion else AlienTrails.Line,
                                RadiusMd,
                            )
                            .clickable(role = Role.Button) { onPick(pr, pg, pb) }
                            .semantics {
                                contentDescription = "Preset #%06X".format(rgb)
                                selected = active
                            },
                    )
                }
            }
        }
    }
}

/**
 * The bass input meter, drawn like the CYD's: the detector's envelope as a
 * bar, the noise gate as a solid amber line, the live kick threshold as a pair
 * of ink notches. The bar turns phosphor for the instant a kick fires.
 * Gate and trigger differ in shape as well as colour, so they read without
 * colour.
 */
@Composable
fun BassMeterBar(
    meter: BassMeter?,
    gate: Int,
    live: Boolean,
    kickLit: Boolean,
    modifier: Modifier = Modifier,
) {
    Box(modifier.fillMaxWidth().height(30.dp), contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxSize()) {
            val corner = CornerRadius(2.dp.toPx(), 2.dp.toPx())
            drawRoundRect(AlienTrails.Hull, cornerRadius = corner)
            drawRoundRect(
                AlienTrails.LineStrong,
                cornerRadius = corner,
                style = Stroke(1.dp.toPx()),
            )
            if (!live || meter == null) return@Canvas

            val pad = 2.dp.toPx()
            val span = size.width - 2 * pad
            fun at(v: Int) = pad + span * v.coerceIn(0, 255) / 255f

            val fillEnd = at(meter.level)
            if (fillEnd > pad) {
                drawRoundRect(
                    if (kickLit) AlienTrails.Phosphor else AlienTrails.Ion,
                    Offset(pad, pad),
                    Size(fillEnd - pad, size.height - 2 * pad),
                    CornerRadius(1.dp.toPx(), 1.dp.toPx()),
                )
            }

            // Trigger first, so the gate wins where they coincide — exactly
            // the case in silence.
            val tx = at(meter.trigger)
            val notchW = 3.dp.toPx()
            val notchH = 6.dp.toPx()
            drawRect(AlienTrails.Ink, Offset(tx - notchW / 2, 0f), Size(notchW, notchH))
            drawRect(AlienTrails.Ink, Offset(tx - notchW / 2, size.height - notchH), Size(notchW, notchH))

            val gx = at(gate)
            drawRect(AlienTrails.Amber, Offset(gx - 1.dp.toPx(), 0f), Size(2.dp.toPx(), size.height))
        }
        if (!live) {
            Text(
                "SELECT BASS TO SEE THE INPUT",
                style = AtType.label.copy(fontSize = 11.sp),
                color = AlienTrails.InkMuted,
            )
        }
    }
}

/** The meter's legend: each mark as its shape and a word, never colour alone. */
@Composable
fun BassMeterLegend(kickLit: Boolean) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(width = 2.dp, height = 12.dp).background(AlienTrails.Amber))
        Spacer(Modifier.width(AtSpace.s1))
        Eyebrow("Gate")
        Spacer(Modifier.width(AtSpace.s4))
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Box(Modifier.size(3.dp).background(AlienTrails.Ink))
            Box(Modifier.size(3.dp).background(AlienTrails.Ink))
        }
        Spacer(Modifier.width(AtSpace.s1))
        Eyebrow("Trigger")
        Spacer(Modifier.weight(1f))
        Box(
            Modifier
                .size(8.dp)
                .background(if (kickLit) AlienTrails.Phosphor else AlienTrails.LineStrong),
        )
        Spacer(Modifier.width(AtSpace.s1))
        Eyebrow("Kick", color = if (kickLit) AlienTrails.Phosphor else AlienTrails.InkMuted)
    }
}

/**
 * A determinate progress bar in ion — the trail colour, for something moving
 * along. Not the dotted trail: house rule 3 gives that to brightness alone.
 */
@Composable
fun ProgressBar(fraction: Float, modifier: Modifier = Modifier) {
    val f = fraction.coerceIn(0f, 1f)
    Canvas(
        modifier
            .fillMaxWidth()
            .height(6.dp)
            .semantics { progressBarRangeInfo = ProgressBarRangeInfo(f, 0f..1f) },
    ) {
        val corner = CornerRadius(size.height / 2, size.height / 2)
        drawRoundRect(AlienTrails.LineStrong, cornerRadius = corner)
        if (f > 0f) {
            drawRoundRect(
                AlienTrails.Ion,
                Offset(0f, 0f),
                Size(size.width * f, size.height),
                corner,
            )
        }
    }
}

/** One "Label ........ reading" line: words in Grotesk, readings in Mono. */
@Composable
fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = AtType.small, color = AlienTrails.InkMuted, modifier = Modifier.weight(1f))
        Text(value, style = AtType.data, color = AlienTrails.Ink)
    }
}
