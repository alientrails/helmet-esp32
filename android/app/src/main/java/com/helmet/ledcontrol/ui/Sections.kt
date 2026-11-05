package com.helmet.ledcontrol.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import com.helmet.ledcontrol.BassMeter
import com.helmet.ledcontrol.HelmetProtocol
import com.helmet.ledcontrol.HelmetState
import com.helmet.ledcontrol.ui.theme.AlienTrails
import com.helmet.ledcontrol.ui.theme.AtType
import kotlinx.coroutines.delay

// The parts of the one-page layout that mirror the CYD's tabs, in the CYD's
// order: Colour, Bass, Strip, System. Each only appears when the connected
// firmware reports the fields it needs, so an older helmet shows the controls
// it can actually honour and nothing else.

/** An eyebrow over its panel, the page's section heading. */
@Composable
fun Section(title: String, content: @Composable () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(AtSpace.s3)) {
        Eyebrow(title)
        content()
    }
}

private fun percent(v: Int): String = "${(v * 100 + 127) / 255}%"

@Composable
fun ColourSection(state: HelmetState, onPick: (Int, Int, Int) -> Unit) {
    Section("Colour") {
        Panel {
            Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
                HueRing(state.r, state.g, state.b, onPick)
            }
            Spacer(Modifier.height(AtSpace.s4))
            Eyebrow("Presets")
            Spacer(Modifier.height(AtSpace.s2))
            PresetGrid(HelmetProtocol.PRESETS, state.r, state.g, state.b, onPick)
        }
    }
}

/**
 * The bass section: the live meter first, because it is what the sliders
 * under it are tuned against. Cover the mic or sit in silence, watch where
 * the bar settles, and set the noise gate just above it.
 */
@Composable
fun BassSection(
    state: HelmetState,
    meter: BassMeter?,
    onBass: (Int, Int) -> Unit,
    onSelectBass: () -> Unit,
) {
    val bass = state.bass
    val live = state.mode == HelmetProtocol.MODE_BASS && meter != null

    // A kick lights the bar for 150 ms. Keyed on the helmet's kick counter, so
    // each new kick restarts the timer and a steady meter never flashes.
    var kickLit by remember { mutableStateOf(false) }
    var lastKicks by remember { mutableStateOf<Int?>(null) }
    val kicks = meter?.kicks
    LaunchedEffect(kicks) {
        val prev = lastKicks
        lastKicks = kicks
        if (kicks != null && prev != null && kicks != prev) {
            kickLit = true
            delay(150)
            kickLit = false
        }
    }

    Section("Bass") {
        Panel {
            Row(
                Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Eyebrow("Input", Modifier.weight(1f))
                val reading = if (live && meter != null) {
                    "IN ${meter.level} · OUT ${(meter.out * 100 + 127) / 255}%"
                } else {
                    "IDLE"
                }
                Text(reading, style = AtType.data, color = AlienTrails.Ink)
            }
            Spacer(Modifier.height(AtSpace.s2))
            BassMeterBar(meter, bass.gate, live, kickLit)
            Spacer(Modifier.height(AtSpace.s2))
            BassMeterLegend(kickLit)
            if (state.mode != HelmetProtocol.MODE_BASS) {
                Spacer(Modifier.height(AtSpace.s3))
                SecondaryButton("Switch to Bass", onSelectBass, Modifier.fillMaxWidth())
            }

            Spacer(Modifier.height(AtSpace.s4))
            SettingSlider(
                "Sensitivity", bass.sensitivity, 0..255, ::percent,
                { onBass(HelmetProtocol.BASS_SENSITIVITY, it) },
            )
            SettingSlider(
                "Noise gate", bass.gate, 0..255, { "$it" },
                { onBass(HelmetProtocol.BASS_GATE, it) },
            )
            SettingSlider(
                "Kick", bass.kick, 0..255, ::percent,
                { onBass(HelmetProtocol.BASS_KICK, it) },
            )
            SettingSlider(
                "Decay", bass.decay, 0..255, { "${HelmetProtocol.decayMs(it)} ms" },
                { onBass(HelmetProtocol.BASS_DECAY, it) },
            )

            Spacer(Modifier.height(AtSpace.s3))
            Eyebrow("Band")
            Spacer(Modifier.height(AtSpace.s2))
            // The hum filter switch only exists on firmware that reports it.
            if (state.hasHum) {
                OptionRow(
                    options = listOf("Kick", "Full"),
                    selected = bass.band,
                    onSelect = { onBass(HelmetProtocol.BASS_BAND, it) },
                    toggle = { m ->
                        OptionCell(
                            "Hum", bass.hum != 0,
                            { onBass(HelmetProtocol.BASS_HUM, if (bass.hum != 0) 0 else 1) },
                            m, isSwitch = true,
                        )
                    },
                )
            } else {
                OptionRow(
                    options = listOf("Kick", "Full"),
                    selected = bass.band,
                    onSelect = { onBass(HelmetProtocol.BASS_BAND, it) },
                )
            }

            Spacer(Modifier.height(AtSpace.s3))
            Eyebrow("Style")
            Spacer(Modifier.height(AtSpace.s2))
            OptionRow(
                options = listOf("Meter", "Pulse", "Centre"),
                selected = bass.style,
                onSelect = { onBass(HelmetProtocol.BASS_STYLE, it) },
            )

            Spacer(Modifier.height(AtSpace.s3))
            Eyebrow("Colour")
            Spacer(Modifier.height(AtSpace.s2))
            val flash = (bass.flags and HelmetProtocol.FLAG_FLASH) != 0
            OptionRow(
                options = HelmetProtocol.COLOUR_NAMES,
                selected = HelmetProtocol.bassColour(bass.flags),
                onSelect = { choice ->
                    onBass(
                        HelmetProtocol.BASS_FLAGS,
                        HelmetProtocol.withBassColour(bass.flags, choice),
                    )
                },
                toggle = { m ->
                    OptionCell(
                        "Flash", flash,
                        { onBass(HelmetProtocol.BASS_FLAGS, bass.flags xor HelmetProtocol.FLAG_FLASH) },
                        m, isSwitch = true,
                    )
                },
            )
        }
    }
}

@Composable
fun StripSection(
    state: HelmetState,
    onOrder: (Int) -> Unit,
    onCount: (Int) -> Unit,
    onSkip: (Int) -> Unit,
    onMaxMa: (Int) -> Unit,
) {
    Section("Strip") {
        Panel {
            Eyebrow("Colour order")
            Spacer(Modifier.height(AtSpace.s2))
            OptionRow(
                options = HelmetProtocol.ORDER_NAMES,
                selected = state.order,
                onSelect = onOrder,
            )
            Spacer(Modifier.height(AtSpace.s4))
            SettingSlider(
                "Pixels", state.count, 1..HelmetProtocol.LED_COUNT_MAX, { "$it px" }, onCount,
            )
            if (state.hasSkip) {
                // Paired with what is left lit, which is the number you are
                // actually watching while you slide this.
                SettingSlider(
                    "Hidden", state.skip, 0..HelmetProtocol.LED_SKIP_MAX,
                    { "$it · ${(state.count - it).coerceAtLeast(0)} lit" }, onSkip,
                )
            }
            SettingSlider(
                "Current budget", state.maxMa,
                HelmetProtocol.MAX_MA_MIN..HelmetProtocol.MAX_MA_MAX, { "$it mA" }, onMaxMa,
                snap = 50,
            )
        }
    }
}

@Composable
fun SystemSection(deviceName: String?, appVersion: String, state: HelmetState) {
    Section("System") {
        Panel {
            Column(verticalArrangement = Arrangement.spacedBy(AtSpace.s2)) {
                InfoRow("Helmet", (deviceName ?: "—").lowercase())
                InfoRow("App", "v$appVersion")
                if (state.hasStrip) {
                    InfoRow("LED strip", "${state.count} px · ${state.lit} lit · ${HelmetProtocol.ORDER_NAMES.getOrElse(state.order) { "?" }}")
                }
            }
        }
    }
}
