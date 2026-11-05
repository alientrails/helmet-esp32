package com.helmet.ledcontrol

import java.util.UUID
import kotlin.math.pow
import kotlin.math.roundToInt

/**
 * The helmet's BLE protocol, mirrored from `include/ble.h` in the firmware.
 *
 * Pure Kotlin with no Android types, so the byte layout is pinned down by
 * JVM unit tests (`app/src/test`) instead of being trusted. Everything here
 * that has a firmware counterpart names it; keep the two in step, and only
 * ever append — indices go over the wire and into the helmet's flash.
 */
object HelmetProtocol {
    val SERVICE_UUID: UUID = UUID.fromString("7b1e8f00-2a5d-4e1a-9c3a-2f6b1d8a10e0")
    val CONTROL_UUID: UUID = UUID.fromString("7b1e8f01-2a5d-4e1a-9c3a-2f6b1d8a10e0")
    val STATE_UUID: UUID = UUID.fromString("7b1e8f02-2a5d-4e1a-9c3a-2f6b1d8a10e0")
    val METER_UUID: UUID = UUID.fromString("7b1e8f03-2a5d-4e1a-9c3a-2f6b1d8a10e0")
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    const val OP_SET_MODE: Byte = 0x01
    const val OP_SET_BRIGHTNESS: Byte = 0x02
    const val OP_SET_ON: Byte = 0x03
    const val OP_TOGGLE: Byte = 0x04
    const val OP_NUDGE_BRIGHTNESS: Byte = 0x05
    const val OP_SET_COLOR: Byte = 0x06
    const val OP_SET_SPEED: Byte = 0x07
    const val OP_SET_ORDER: Byte = 0x08
    const val OP_SET_COUNT: Byte = 0x09
    const val OP_SET_MAX_MA: Byte = 0x0A
    const val OP_SET_SKIP: Byte = 0x0B
    const val OP_SET_BASS: Byte = 0x0C

    // BassParam ids for OP_SET_BASS (include/leds.h).
    const val BASS_SENSITIVITY = 0
    const val BASS_GATE = 1
    const val BASS_KICK = 2
    const val BASS_DECAY = 3
    const val BASS_BAND = 4
    const val BASS_STYLE = 5
    const val BASS_FLAGS = 6
    const val BASS_HUM = 7

    const val BAND_KICK = 0
    const val BAND_FULL = 1

    const val STYLE_METER = 0
    const val STYLE_PULSE = 1
    const val STYLE_CENTRE = 2

    const val FLAG_FLASH = 0x01
    const val FLAG_MY_COLOUR = 0x02

    // Limits, from include/config.h.
    const val BRIGHTNESS_MIN = 8
    const val SPEED_MIN = 16
    const val SPEED_NORMAL = 128
    const val LED_COUNT_MAX = 120
    const val LED_SKIP_MAX = 32
    const val MAX_MA_MIN = 100
    const val MAX_MA_MAX = 2000

    // Index order matters: it is what goes over the wire. Append only.
    val MODE_NAMES = listOf(
        "Solid", "Breathe", "Heartbeat", "Slow blink",
        "Fast blink", "Strobe", "SOS", "Party", "Bass",
        "Comet", "Scanner", "Twinkle", "Fire", "Rainbow", "Wipe", "Chase",
    )
    const val MODE_BASS = 8

    // LedOrder (include/leds.h).
    val ORDER_NAMES = listOf("GRB", "RGB", "BRG", "RBG", "GBR", "BGR")

    // The CYD's twelve colour presets (kPresets in src/ui_screens.cpp), so
    // both screens offer the same palette.
    val PRESETS: List<Int> = listOf(
        0xFF0000, 0xFF4E00, 0xFF8A00, 0xFFC400, 0xC6FF00, 0x18FF62,
        0x00E5D0, 0x00A8FF, 0x3355FF, 0xB14BFF, 0xFFC98A, 0xFFFFFF,
    )

    /**
     * The flash time constant a bass decay setting maps to — the same formula
     * as `BassDsp::decayMs()`, so the app shows the number the helmet uses.
     */
    fun decayMs(decay: Int): Int =
        (40.0 * (700.0 / 40.0).pow(decay.coerceIn(0, 255) / 255.0)).roundToInt()

    // ---- Commands. Every one is [opcode, value...], written without response.

    fun command(op: Byte, value: Int): ByteArray = byteArrayOf(op, value.toByte())

    fun setColor(r: Int, g: Int, b: Int): ByteArray =
        byteArrayOf(OP_SET_COLOR, r.toByte(), g.toByte(), b.toByte())

    /** Sent in tens of mA, so the whole budget fits the one value byte. */
    fun setMaxMa(milliamps: Int): ByteArray =
        byteArrayOf(OP_SET_MAX_MA, (milliamps.coerceIn(MAX_MA_MIN, MAX_MA_MAX) / 10).toByte())

    fun setBass(param: Int, value: Int): ByteArray =
        byteArrayOf(OP_SET_BASS, param.toByte(), value.coerceIn(0, 255).toByte())

    // ---- Notifications.

    /**
     * Applies a state notification on top of [prev]. The payload only ever
     * grows at the end, so older firmware sends a prefix: fields it did not
     * send keep their previous value and their `has…` flag stays false, which
     * is how the UI knows to hide controls the helmet cannot honour.
     *
     * Layout (20 bytes): mode, brightness, on, outputLevel, r, g, b, speed,
     * order, count, maxMa/10, skip, then bass sensitivity, gate, kick, decay,
     * band, style, flags, hum.
     */
    fun parseState(v: ByteArray, prev: HelmetState): HelmetState? {
        if (v.size < 4) return null
        fun u(i: Int) = v[i].toInt() and 0xFF
        var s = prev.copy(mode = u(0), brightness = u(1), on = u(2) != 0, outputLevel = u(3))
        if (v.size >= 7) s = s.copy(r = u(4), g = u(5), b = u(6), hasColour = true)
        if (v.size >= 8) s = s.copy(speed = u(7), hasSpeed = true)
        if (v.size >= 11) s = s.copy(order = u(8), count = u(9), maxMa = u(10) * 10, hasStrip = true)
        if (v.size >= 12) s = s.copy(skip = u(11), hasSkip = true)
        if (v.size >= 19) {
            s = s.copy(
                bass = BassSettings(
                    sensitivity = u(12),
                    gate = u(13),
                    kick = u(14),
                    decay = u(15),
                    band = u(16),
                    style = u(17),
                    flags = u(18),
                    hum = if (v.size >= 20) u(19) else prev.bass.hum,
                ),
                hasBass = true,
                hasHum = v.size >= 20,
            )
        }
        return s
    }

    /** The bass meter: [level, trigger, out, kicks]. */
    fun parseMeter(v: ByteArray): BassMeter? {
        if (v.size < 4) return null
        fun u(i: Int) = v[i].toInt() and 0xFF
        return BassMeter(level = u(0), trigger = u(1), out = u(2), kicks = u(3))
    }
}

/** The Bass mode settings, by the same ids as `BassParam` in the firmware. */
data class BassSettings(
    val sensitivity: Int = 128,
    val gate: Int = 40,
    val kick: Int = 128,
    val decay: Int = 96,
    val band: Int = HelmetProtocol.BAND_KICK,
    val style: Int = HelmetProtocol.STYLE_METER,
    val flags: Int = HelmetProtocol.FLAG_FLASH,
    val hum: Int = 1,
) {
    fun get(param: Int): Int = when (param) {
        HelmetProtocol.BASS_SENSITIVITY -> sensitivity
        HelmetProtocol.BASS_GATE -> gate
        HelmetProtocol.BASS_KICK -> kick
        HelmetProtocol.BASS_DECAY -> decay
        HelmetProtocol.BASS_BAND -> band
        HelmetProtocol.BASS_STYLE -> style
        HelmetProtocol.BASS_FLAGS -> flags
        HelmetProtocol.BASS_HUM -> hum
        else -> 0
    }

    fun with(param: Int, value: Int): BassSettings = when (param) {
        HelmetProtocol.BASS_SENSITIVITY -> copy(sensitivity = value)
        HelmetProtocol.BASS_GATE -> copy(gate = value)
        HelmetProtocol.BASS_KICK -> copy(kick = value)
        HelmetProtocol.BASS_DECAY -> copy(decay = value)
        HelmetProtocol.BASS_BAND -> copy(band = value)
        HelmetProtocol.BASS_STYLE -> copy(style = value)
        HelmetProtocol.BASS_FLAGS -> copy(flags = value)
        HelmetProtocol.BASS_HUM -> copy(hum = value)
        else -> this
    }
}

data class HelmetState(
    val mode: Int = 0,
    val brightness: Int = 0,
    val on: Boolean = true,
    val outputLevel: Int = 0,
    val r: Int = 255,
    val g: Int = 255,
    val b: Int = 255,
    val speed: Int = HelmetProtocol.SPEED_NORMAL,
    val order: Int = 0,
    val count: Int = 60,
    val maxMa: Int = 500,
    val skip: Int = 7,
    val bass: BassSettings = BassSettings(),
    // Which parts of the state the connected firmware actually sent.
    val hasColour: Boolean = false,
    val hasSpeed: Boolean = false,
    val hasStrip: Boolean = false,
    val hasSkip: Boolean = false,
    val hasBass: Boolean = false,
    val hasHum: Boolean = false,
) {
    /** Pixels a pattern is drawn across: the strip less the hidden head. */
    val lit: Int get() = (count - skip).coerceAtLeast(0)
}

/** One reading of the helmet's bass detector, in the noise gate's units. */
data class BassMeter(
    val level: Int,
    val trigger: Int,
    val out: Int,
    val kicks: Int,
)
