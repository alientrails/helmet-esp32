package com.helmet.ledcontrol

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.util.UUID
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

// Mirrors include/ble.h in the firmware. Keep opcodes and mode names in sync
// with include/leds.h / src/leds.cpp.
object HelmetProtocol {
    val SERVICE_UUID: UUID = UUID.fromString("7b1e8f00-2a5d-4e1a-9c3a-2f6b1d8a10e0")
    val CONTROL_UUID: UUID = UUID.fromString("7b1e8f01-2a5d-4e1a-9c3a-2f6b1d8a10e0")
    val STATE_UUID: UUID = UUID.fromString("7b1e8f02-2a5d-4e1a-9c3a-2f6b1d8a10e0")
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    const val OP_SET_MODE: Byte = 0x01
    const val OP_SET_BRIGHTNESS: Byte = 0x02
    const val OP_SET_ON: Byte = 0x03
    const val OP_TOGGLE: Byte = 0x04
    const val OP_NUDGE_BRIGHTNESS: Byte = 0x05

    val MODE_NAMES = listOf(
        "Solid", "Breathe", "Heartbeat", "Slow blink",
        "Fast blink", "Strobe", "SOS", "Party", "Bass",
    )
}

enum class ConnectionState { DISCONNECTED, SCANNING, CONNECTING, CONNECTED }

data class HelmetState(
    val mode: Int = 0,
    val brightness: Int = 0,
    val on: Boolean = true,
    val outputLevel: Int = 0,
)

@SuppressLint("MissingPermission")
class HelmetBle(context: Context) {
    private val appContext = context.applicationContext
    private val adapter: BluetoothAdapter? =
        (appContext.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
    private val mainHandler = Handler(Looper.getMainLooper())

    private var gatt: BluetoothGatt? = null
    private var controlChar: BluetoothGattCharacteristic? = null
    private var scanning = false

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _deviceName = MutableStateFlow<String?>(null)
    val deviceName: StateFlow<String?> = _deviceName.asStateFlow()

    private val _state = MutableStateFlow(HelmetState())
    val state: StateFlow<HelmetState> = _state.asStateFlow()

    private val _lastError = MutableStateFlow<String?>(null)
    val lastError: StateFlow<String?> = _lastError.asStateFlow()

    fun isBluetoothEnabled(): Boolean = adapter?.isEnabled == true

    fun startScan() {
        val scanner = adapter?.bluetoothLeScanner
        if (scanner == null) {
            _lastError.value = "Bluetooth is off"
            return
        }
        if (scanning) return
        scanning = true
        _connectionState.value = ConnectionState.SCANNING

        val filters = listOf(
            ScanFilter.Builder()
                .setServiceUuid(android.os.ParcelUuid(HelmetProtocol.SERVICE_UUID))
                .build(),
        )
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner.startScan(filters, settings, scanCallback)

        // Stop scanning after 10s if nothing found.
        mainHandler.postDelayed({
            if (_connectionState.value == ConnectionState.SCANNING) {
                stopScan()
                _lastError.value = "No helmet found"
            }
        }, 10_000)
    }

    fun stopScan() {
        if (!scanning) return
        scanning = false
        adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        if (_connectionState.value == ConnectionState.SCANNING) {
            _connectionState.value = ConnectionState.DISCONNECTED
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            stopScan()
            connect(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            _connectionState.value = ConnectionState.DISCONNECTED
            _lastError.value = "Scan failed ($errorCode)"
        }
    }

    private fun connect(device: BluetoothDevice) {
        _connectionState.value = ConnectionState.CONNECTING
        _deviceName.value = device.name ?: device.address
        gatt = device.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    fun disconnect() {
        stopScan()
        gatt?.disconnect()
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                controlChar = null
                g.close()
                if (gatt === g) {
                    gatt = null
                }
                _connectionState.value = ConnectionState.DISCONNECTED
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    _lastError.value = "Disconnected ($status)"
                }
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                _lastError.value = "Service discovery failed"
                g.disconnect()
                return
            }
            val service = g.getService(HelmetProtocol.SERVICE_UUID)
            if (service == null) {
                _lastError.value = "Helmet service not found"
                g.disconnect()
                return
            }
            controlChar = service.getCharacteristic(HelmetProtocol.CONTROL_UUID)
            val stateChar = service.getCharacteristic(HelmetProtocol.STATE_UUID)
            if (stateChar != null) {
                g.setCharacteristicNotification(stateChar, true)
                val cccd = stateChar.getDescriptor(HelmetProtocol.CCCD_UUID)
                if (cccd != null) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                    } else {
                        @Suppress("DEPRECATION")
                        cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        @Suppress("DEPRECATION")
                        g.writeDescriptor(cccd)
                    }
                }
                g.readCharacteristic(stateChar)
            }
            _connectionState.value = ConnectionState.CONNECTED
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (characteristic.uuid == HelmetProtocol.STATE_UUID) {
                applyState(value)
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            if (characteristic.uuid == HelmetProtocol.STATE_UUID) {
                @Suppress("DEPRECATION")
                characteristic.value?.let { applyState(it) }
            }
        }

        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int,
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS && characteristic.uuid == HelmetProtocol.STATE_UUID) {
                applyState(value)
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS && characteristic.uuid == HelmetProtocol.STATE_UUID) {
                @Suppress("DEPRECATION")
                characteristic.value?.let { applyState(it) }
            }
        }
    }

    private fun applyState(value: ByteArray) {
        if (value.size < 4) return
        _state.value = HelmetState(
            mode = value[0].toInt() and 0xFF,
            brightness = value[1].toInt() and 0xFF,
            on = value[2].toInt() != 0,
            outputLevel = value[3].toInt() and 0xFF,
        )
    }

    private fun writeControl(opcode: Byte, arg: Byte) {
        val g = gatt ?: return
        val c = controlChar ?: return
        val payload = byteArrayOf(opcode, arg)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(c, payload, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
        } else {
            @Suppress("DEPRECATION")
            c.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            @Suppress("DEPRECATION")
            c.value = payload
            @Suppress("DEPRECATION")
            g.writeCharacteristic(c)
        }
    }

    fun setMode(mode: Int) {
        writeControl(HelmetProtocol.OP_SET_MODE, mode.toByte())
        _state.value = _state.value.copy(mode = mode)
    }

    fun setBrightness(brightness: Int) {
        val clamped = brightness.coerceIn(0, 255)
        writeControl(HelmetProtocol.OP_SET_BRIGHTNESS, clamped.toByte())
        _state.value = _state.value.copy(brightness = clamped)
    }

    fun setOn(on: Boolean) {
        writeControl(HelmetProtocol.OP_SET_ON, if (on) 1 else 0)
        _state.value = _state.value.copy(on = on)
    }

    fun toggle() {
        writeControl(HelmetProtocol.OP_TOGGLE, 0)
        _state.value = _state.value.copy(on = !_state.value.on)
    }

    fun clearError() {
        _lastError.value = null
    }
}
