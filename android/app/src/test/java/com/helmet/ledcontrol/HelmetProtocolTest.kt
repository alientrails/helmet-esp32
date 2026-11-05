package com.helmet.ledcontrol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Pins the app's reading of the helmet's BLE bytes to exactly what the
 * firmware sends (src/ble.cpp, pushState / pushMeter / applyControl). If one of
 * these fails after a firmware change, the two sides have drifted.
 *
 *   ./gradlew testDebugUnitTest
 */
class HelmetProtocolTest {

    private fun bytes(vararg v: Int) = ByteArray(v.size) { v[it].toByte() }

    // What firmware 1.7.0 sends: 20 bytes.
    private val full = bytes(
        8, 200, 1, 190, // mode Bass, brightness, on, outputLevel
        0x00, 0xA8, 0xFF, // r, g, b
        64, // speed
        3, // order RBG
        120, // count
        150, // maxMa / 10 -> 1500 mA
        7, // skip
        200, 52, 90, 255, 1, 2, 0x03, 0, // bass: sens, gate, kick, decay, band, style, flags, hum
    )

    @Test fun parsesEveryFieldOfTheCurrentState() {
        val s = HelmetProtocol.parseState(full, HelmetState())!!
        assertEquals(HelmetProtocol.MODE_BASS, s.mode)
        assertEquals(200, s.brightness)
        assertTrue(s.on)
        assertEquals(190, s.outputLevel)
        assertEquals(0x00, s.r)
        assertEquals(0xA8, s.g)
        assertEquals(0xFF, s.b)
        assertEquals(64, s.speed)
        assertEquals(3, s.order)
        assertEquals(120, s.count)
        assertEquals(1500, s.maxMa)
        assertEquals(7, s.skip)
        assertEquals(113, s.lit)
        assertEquals(BassSettings(200, 52, 90, 255, 1, 2, 0x03, 0), s.bass)
        assertTrue(s.hasColour && s.hasSpeed && s.hasStrip && s.hasSkip && s.hasBass && s.hasHum)
    }

    @Test fun bytesAbove127AreNotNegative() {
        // Kotlin bytes are signed; a forgotten `and 0xFF` turns 200 into -56.
        val s = HelmetProtocol.parseState(full, HelmetState())!!
        assertTrue(s.brightness > 127 && s.bass.decay == 255)
    }

    @Test fun olderFirmwareKeepsWhatItDidNotSend() {
        // Firmware 1.6.0 stopped after skip: 12 bytes, no bass.
        val prev = HelmetState(bass = BassSettings(gate = 99))
        val s = HelmetProtocol.parseState(full.copyOf(12), prev)!!
        assertEquals(7, s.skip)
        assertTrue(s.hasSkip)
        assertFalse(s.hasBass)
        assertFalse(s.hasHum)
        assertEquals(99, s.bass.gate) // untouched
    }

    @Test fun bassWithoutHumKeepsThePreviousHum() {
        // 19 bytes: bass settings from before the hum filter existed.
        val prev = HelmetState(bass = BassSettings(hum = 1))
        val s = HelmetProtocol.parseState(full.copyOf(19), prev)!!
        assertTrue(s.hasBass)
        assertFalse(s.hasHum)
        assertEquals(1, s.bass.hum)
        assertEquals(52, s.bass.gate)
    }

    @Test fun theOldestFirmwareSendsOnlyFourBytes() {
        val s = HelmetProtocol.parseState(full.copyOf(4), HelmetState())!!
        assertEquals(HelmetProtocol.MODE_BASS, s.mode)
        assertFalse(s.hasColour || s.hasSpeed || s.hasStrip || s.hasSkip || s.hasBass)
        assertNull(HelmetProtocol.parseState(full.copyOf(3), HelmetState()))
    }

    @Test fun parsesTheMeter() {
        assertEquals(BassMeter(150, 96, 214, 255), HelmetProtocol.parseMeter(bytes(150, 96, 214, 255)))
        assertNull(HelmetProtocol.parseMeter(bytes(1, 2, 3)))
    }

    @Test fun encodesCommandsTheWayApplyControlReadsThem() {
        assertArrayEquals(bytes(0x0C, HelmetProtocol.BASS_GATE, 52), HelmetProtocol.setBass(HelmetProtocol.BASS_GATE, 52))
        assertArrayEquals(bytes(0x0C, HelmetProtocol.BASS_HUM, 255), HelmetProtocol.setBass(HelmetProtocol.BASS_HUM, 999))
        assertArrayEquals(bytes(0x06, 0x00, 0xA8, 0xFF), HelmetProtocol.setColor(0x00, 0xA8, 0xFF))
        // The current budget goes in tens of mA, clamped to the firmware's range.
        assertArrayEquals(bytes(0x0A, 150), HelmetProtocol.setMaxMa(1500))
        assertArrayEquals(bytes(0x0A, 10), HelmetProtocol.setMaxMa(20))
        assertArrayEquals(bytes(0x0A, 200), HelmetProtocol.setMaxMa(5000))
        assertArrayEquals(bytes(0x0B, 7), HelmetProtocol.command(HelmetProtocol.OP_SET_SKIP, 7))
    }

    @Test fun decayMatchesTheFirmwareFormula() {
        // BassDsp::decayMs(): 40 ms at 0, 700 ms at 255; the CYD shows 117 ms
        // at the default of 96.
        assertEquals(40, HelmetProtocol.decayMs(0))
        assertEquals(117, HelmetProtocol.decayMs(96))
        assertEquals(700, HelmetProtocol.decayMs(255))
    }

    @Test fun bassSettingsRoundTripByParamId() {
        var b = BassSettings()
        for (p in HelmetProtocol.BASS_SENSITIVITY..HelmetProtocol.BASS_HUM) {
            b = b.with(p, 10 + p)
            assertEquals(10 + p, b.get(p))
        }
    }

    @Test fun modeAndOrderTablesMatchTheFirmware() {
        assertEquals(16, HelmetProtocol.MODE_NAMES.size) // MODE_COUNT
        assertEquals("Bass", HelmetProtocol.MODE_NAMES[HelmetProtocol.MODE_BASS])
        assertEquals("Chase", HelmetProtocol.MODE_NAMES.last())
        assertEquals(6, HelmetProtocol.ORDER_NAMES.size) // ORDER_COUNT
        assertEquals(12, HelmetProtocol.PRESETS.size)
    }
}
