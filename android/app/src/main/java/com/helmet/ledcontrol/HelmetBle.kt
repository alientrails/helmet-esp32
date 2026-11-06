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
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class ConnectionState { DISCONNECTED, SCANNING, CONNECTING, CONNECTED }

@SuppressLint("MissingPermission")
class HelmetBle(context: Context) {
    private val appContext = context.applicationContext
    private val adapter: BluetoothAdapter? =
        (appContext.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
    private val mainHandler = Handler(Looper.getMainLooper())

    private var gatt: BluetoothGatt? = null
    private var controlChar: BluetoothGattCharacteristic? = null
    private var stateChar: BluetoothGattCharacteristic? = null
    private var meterChar: BluetoothGattCharacteristic? = null
    private var otaCtrlChar: BluetoothGattCharacteristic? = null
    private var otaDataChar: BluetoothGattCharacteristic? = null
    private var screenChar: BluetoothGattCharacteristic? = null
    private var scanning = false
    private var mtu = DEFAULT_MTU

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _deviceName = MutableStateFlow<String?>(null)
    val deviceName: StateFlow<String?> = _deviceName.asStateFlow()

    private val _state = MutableStateFlow(HelmetState())
    val state: StateFlow<HelmetState> = _state.asStateFlow()

    /** Live bass detector reading; only moves while the helmet is in Bass mode. */
    private val _meter = MutableStateFlow<BassMeter?>(null)
    val meter: StateFlow<BassMeter?> = _meter.asStateFlow()

    private val _lastError = MutableStateFlow<String?>(null)
    val lastError: StateFlow<String?> = _lastError.asStateFlow()

    /** Null unless a firmware update is in flight or has just finished. */
    private val _ota = MutableStateFlow<OtaTransfer?>(null)
    val ota: StateFlow<OtaTransfer?> = _ota.asStateFlow()

    /** True when the connected helmet offers the update characteristics. */
    private val _canUpdate = MutableStateFlow(false)
    val canUpdate: StateFlow<Boolean> = _canUpdate.asStateFlow()

    /**
     * Pocket mode, on its own characteristic rather than in the state payload,
     * which is full. [canScreen] is false against firmware that does not offer
     * it; the app then hides the control instead of sending an opcode the
     * helmet would ignore.
     */
    private val _screenOn = MutableStateFlow(true)
    val screenOn: StateFlow<Boolean> = _screenOn.asStateFlow()

    private val _canScreen = MutableStateFlow(false)
    val canScreen: StateFlow<Boolean> = _canScreen.asStateFlow()

    fun isBluetoothEnabled(): Boolean = adapter?.isEnabled == true

    // ------------------------------------------------------------ GATT queue
    //
    // Android runs exactly one GATT operation at a time per connection and
    // silently fails the rest, so every read, write and descriptor write goes
    // through this queue and the next only starts when the previous one's
    // callback arrives. It all runs on the main thread; callbacks hop over.
    //
    // Writes carry a key, and a write queued behind another with the same key
    // replaces it: dragging a slider keeps at most one write per control in
    // the queue, always the newest value, instead of a backlog the helmet is
    // still working through seconds after you let go.

    private sealed class Op {
        class Write(val char: BluetoothGattCharacteristic, var value: ByteArray, val key: Int) : Op()
        class EnableNotify(val char: BluetoothGattCharacteristic) : Op()
        class Read(val char: BluetoothGattCharacteristic) : Op()
    }

    private val ops = ArrayDeque<Op>()
    private var inFlight: Op? = null

    // A stack that never calls back must not wedge every later command.
    private val opTimeout = Runnable { finishOp() }

    private fun enqueue(op: Op) {
        mainHandler.post {
            // KEY_STREAM is firmware-image data: every chunk is different and
            // all of them have to arrive, so coalescing would quietly eat the
            // middle of the image. Only controls collapse.
            if (op is Op.Write && op.key != KEY_STREAM) {
                val pending = ops.firstOrNull { it is Op.Write && it.key == op.key } as Op.Write?
                if (pending != null) {
                    pending.value = op.value
                    return@post
                }
            }
            ops.addLast(op)
            pump()
        }
    }

    private fun pump() {
        if (inFlight != null) return
        val g = gatt
        if (g == null) {
            ops.clear()
            return
        }
        while (ops.isNotEmpty()) {
            val op = ops.removeFirst()
            if (start(g, op)) {
                inFlight = op
                mainHandler.postDelayed(opTimeout, OP_TIMEOUT_MS)
                return
            }
            // The stack refused it outright; drop it rather than retry forever.
        }
    }

    private fun finishOp() {
        mainHandler.removeCallbacks(opTimeout)
        inFlight = null
        pump()
    }

    private fun clearQueue() {
        mainHandler.post {
            mainHandler.removeCallbacks(opTimeout)
            ops.clear()
            inFlight = null
        }
    }

    private fun start(g: BluetoothGatt, op: Op): Boolean = when (op) {
        is Op.Write -> writeCharacteristic(g, op.char, op.value)
        is Op.Read -> g.readCharacteristic(op.char)
        is Op.EnableNotify -> {
            g.setCharacteristicNotification(op.char, true)
            val cccd = op.char.getDescriptor(HelmetProtocol.CCCD_UUID)
            cccd != null && writeDescriptor(g, cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        }
    }

    private fun writeCharacteristic(
        g: BluetoothGatt,
        c: BluetoothGattCharacteristic,
        value: ByteArray,
    ): Boolean {
        val type = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(c, value, type) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            c.writeType = type
            @Suppress("DEPRECATION")
            c.value = value
            @Suppress("DEPRECATION")
            g.writeCharacteristic(c)
        }
    }

    private fun writeDescriptor(
        g: BluetoothGatt,
        d: BluetoothGattDescriptor,
        value: ByteArray,
    ): Boolean = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        g.writeDescriptor(d, value) == BluetoothStatusCodes.SUCCESS
    } else {
        @Suppress("DEPRECATION")
        d.value = value
        @Suppress("DEPRECATION")
        g.writeDescriptor(d)
    }

    // ------------------------------------------------------------- scanning

    fun startScan() {
        val scanner = adapter?.bluetoothLeScanner
        if (scanner == null) {
            _lastError.value = "Bluetooth is off"
            return
        }
        if (scanning) return
        scanning = true
        _lastError.value = null
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

    // ------------------------------------------------------------ callbacks

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                clearQueue()
                controlChar = null
                stateChar = null
                meterChar = null
                screenChar = null
                _canScreen.value = false
                g.close()
                if (gatt === g) {
                    gatt = null
                }
                _meter.value = null
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
            stateChar = service.getCharacteristic(HelmetProtocol.STATE_UUID)
            // Absent on firmware older than the bass meter; everything else
            // still works without it.
            meterChar = service.getCharacteristic(HelmetProtocol.METER_UUID)
            // Absent on firmware older than 1.8.0, which cannot be updated
            // over the air at all; the app hides the control rather than
            // offering something that would fail.
            otaCtrlChar = service.getCharacteristic(HelmetProtocol.OTA_CTRL_UUID)
            otaDataChar = service.getCharacteristic(HelmetProtocol.OTA_DATA_UUID)
            _canUpdate.value = otaCtrlChar != null && otaDataChar != null
            // Absent on firmware older than 1.10.0, which has no pocket mode.
            screenChar = service.getCharacteristic(HelmetProtocol.SCREEN_UUID)
            _canScreen.value = screenChar != null

            // Subscribe first, then read, so nothing that changes between the
            // read and the subscription is missed.
            stateChar?.let {
                enqueue(Op.EnableNotify(it))
            }
            meterChar?.let { enqueue(Op.EnableNotify(it)) }
            otaCtrlChar?.let { enqueue(Op.EnableNotify(it)) }
            screenChar?.let { enqueue(Op.EnableNotify(it)) }
            stateChar?.let { enqueue(Op.Read(it)) }
            // Read as well as subscribed: a helmet that is already in pocket
            // mode has nothing to notify, and the switch has to come up in the
            // position the helmet is actually in.
            screenChar?.let { enqueue(Op.Read(it)) }
            // Everything but an update fits the default MTU; an image at 20
            // bytes a packet would take the best part of an hour.
            if (otaDataChar != null) {
                g.requestMtu(MAX_MTU)
            }
            _connectionState.value = ConnectionState.CONNECTED
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                mtu = newMtu.coerceIn(DEFAULT_MTU, MAX_MTU)
            }
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            mainHandler.post { finishOp() }
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            mainHandler.post { finishOp() }
        }

        // API 33+ delivers the value directly; older releases only call the
        // deprecated overloads below. Each event arrives through exactly one.
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            onIncoming(characteristic, value)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            @Suppress("DEPRECATION")
            characteristic.value?.let { onIncoming(characteristic, it) }
        }

        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int,
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) onIncoming(characteristic, value)
            mainHandler.post { finishOp() }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            @Suppress("DEPRECATION")
            val value = characteristic.value
            if (status == BluetoothGatt.GATT_SUCCESS && value != null) onIncoming(characteristic, value)
            mainHandler.post { finishOp() }
        }
    }

    private fun onIncoming(characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        when (characteristic.uuid) {
            HelmetProtocol.STATE_UUID ->
                HelmetProtocol.parseState(value, _state.value)?.let { _state.value = it }
            HelmetProtocol.METER_UUID ->
                _meter.value = HelmetProtocol.parseMeter(value)
            HelmetProtocol.OTA_CTRL_UUID ->
                HelmetProtocol.parseOtaStatus(value)?.let { onOtaStatus(it) }
            HelmetProtocol.SCREEN_UUID ->
                HelmetProtocol.parseScreen(value)?.let { _screenOn.value = it }
        }
    }

    // ------------------------------------------------------------- commands
    //
    // Each setter writes the command and updates the local state at once, so
    // the UI never snaps back to the old value while the helmet's
    // confirmation is still on its way.

    private fun send(key: Int, payload: ByteArray) {
        val c = controlChar ?: return
        enqueue(Op.Write(c, payload, key))
    }

    private fun update(change: (HelmetState) -> HelmetState) {
        _state.value = change(_state.value)
    }

    fun setMode(mode: Int) {
        send(HelmetProtocol.OP_SET_MODE.toInt(), HelmetProtocol.command(HelmetProtocol.OP_SET_MODE, mode))
        // The firmware switches the lights on with every mode change.
        update { it.copy(mode = mode, on = true) }
    }

    fun setBrightness(brightness: Int) {
        val v = brightness.coerceIn(HelmetProtocol.BRIGHTNESS_MIN, 255)
        send(HelmetProtocol.OP_SET_BRIGHTNESS.toInt(), HelmetProtocol.command(HelmetProtocol.OP_SET_BRIGHTNESS, v))
        update { it.copy(brightness = v) }
    }

    fun setOn(on: Boolean) {
        send(HelmetProtocol.OP_SET_ON.toInt(), HelmetProtocol.command(HelmetProtocol.OP_SET_ON, if (on) 1 else 0))
        update { it.copy(on = on) }
    }

    fun toggle() {
        send(HelmetProtocol.OP_TOGGLE.toInt(), HelmetProtocol.command(HelmetProtocol.OP_TOGGLE, 0))
        update { it.copy(on = !it.on) }
    }

    fun setSpeed(speed: Int) {
        val v = speed.coerceIn(HelmetProtocol.SPEED_MIN, 255)
        send(HelmetProtocol.OP_SET_SPEED.toInt(), HelmetProtocol.command(HelmetProtocol.OP_SET_SPEED, v))
        update { it.copy(speed = v) }
    }

    fun setColor(r: Int, g: Int, b: Int) {
        send(HelmetProtocol.OP_SET_COLOR.toInt(), HelmetProtocol.setColor(r, g, b))
        update { it.copy(r = r, g = g, b = b) }
    }

    fun setOrder(order: Int) {
        send(HelmetProtocol.OP_SET_ORDER.toInt(), HelmetProtocol.command(HelmetProtocol.OP_SET_ORDER, order))
        update { it.copy(order = order) }
    }

    fun setCount(count: Int) {
        val v = count.coerceIn(1, HelmetProtocol.LED_COUNT_MAX)
        send(HelmetProtocol.OP_SET_COUNT.toInt(), HelmetProtocol.command(HelmetProtocol.OP_SET_COUNT, v))
        // The firmware keeps at least one pixel lit, so a shorter strip can pull
        // the skip down with it.
        update { it.copy(count = v, skip = it.skip.coerceAtMost(v - 1)) }
    }

    fun setSkip(skip: Int) {
        val v = skip.coerceIn(0, HelmetProtocol.LED_SKIP_MAX)
        send(HelmetProtocol.OP_SET_SKIP.toInt(), HelmetProtocol.command(HelmetProtocol.OP_SET_SKIP, v))
        update { it.copy(skip = v.coerceAtMost(it.count - 1)) }
    }

    fun setMaxMa(milliamps: Int) {
        val v = milliamps.coerceIn(HelmetProtocol.MAX_MA_MIN, HelmetProtocol.MAX_MA_MAX)
        send(HelmetProtocol.OP_SET_MAX_MA.toInt(), HelmetProtocol.setMaxMa(v))
        update { it.copy(maxMa = v / 10 * 10) }
    }

    /**
     * Switches the helmet's own screen and controls off or on. The app is the
     * only way in or out of pocket mode, so the local value moves at once
     * rather than waiting for the helmet to confirm — the helmet's own
     * notification lands a moment later and agrees.
     */
    fun setScreen(on: Boolean) {
        if (screenChar == null) return
        send(HelmetProtocol.OP_SET_SCREEN.toInt(), HelmetProtocol.setScreen(on))
        _screenOn.value = on
    }

    fun setBass(param: Int, value: Int) {
        val v = value.coerceIn(0, 255)
        // Each bass setting is its own control, so each gets its own write key.
        send(0x100 + param, HelmetProtocol.setBass(param, v))
        update { it.copy(bass = it.bass.with(param, v)) }
    }

    fun clearError() {
        _lastError.value = null
    }

    /** Surfaces a problem that happened outside the GATT layer, e.g. file I/O. */
    fun reportError(message: String) {
        _lastError.value = message
    }

    // --------------------------------------------------------- firmware update
    //
    // The image goes out over its own characteristic in MTU-sized chunks,
    // written without response because an acknowledgement per chunk would
    // halve the throughput. What keeps the app from running away from the
    // helmet is the window: it never has more than OTA_WINDOW bytes in flight
    // beyond what the helmet's progress notifications have confirmed, so a
    // helmet busy writing flash slows the phone down instead of losing the
    // middle of the image.

    private var image: ByteArray? = null
    private var sent = 0
    // A progress notification sent just before the helmet saw END still
    // arrives after it. Without this the app would send a second END, and the
    // helmet would answer "out of order" for an update that had just worked.
    private var endSent = false

    /**
     * Sends [bytes] to the helmet as a firmware image. Returns false, with
     * [lastError] set, if this helmet or this file cannot take one.
     */
    fun startOta(bytes: ByteArray): Boolean {
        val ctrl = otaCtrlChar
        if (ctrl == null || otaDataChar == null) {
            _lastError.value = "This helmet's firmware cannot be updated over the air"
            return false
        }
        if (_ota.value?.inFlight == true) {
            _lastError.value = "An update is already running"
            return false
        }
        if (!HelmetProtocol.looksLikeFirmware(bytes)) {
            _lastError.value = "That file is not a helmet firmware image"
            return false
        }
        image = bytes
        sent = 0
        endSent = false
        _ota.value = OtaTransfer(
            state = HelmetProtocol.OTA_IDLE,
            error = 0,
            sent = 0,
            acked = 0,
            total = bytes.size,
        )
        // BEGIN goes through the same queue as everything else, so it cannot
        // overtake a control write already on its way.
        enqueue(Op.Write(ctrl, HelmetProtocol.otaBegin(bytes.size, HelmetProtocol.crc32(bytes)), KEY_OTA_CTRL))
        return true
    }

    /** Gives up on an update in flight. The helmet keeps running what it has. */
    fun cancelOta() {
        val ctrl = otaCtrlChar ?: return
        image = null
        sent = 0
        endSent = true  // nothing more of this transfer should go out
        mainHandler.post { ops.removeAll { it is Op.Write && it.key == KEY_STREAM } }
        enqueue(Op.Write(ctrl, HelmetProtocol.otaAbort(), KEY_OTA_CTRL))
    }

    /** Clears a finished or failed update so the UI goes back to normal. */
    fun clearOta() {
        image = null
        sent = 0
        endSent = false
        _ota.value = null
    }

    private fun onOtaStatus(status: OtaStatus) {
        val img = image
        val prev = _ota.value
        _ota.value = OtaTransfer(
            state = status.state,
            error = status.error,
            sent = sent,
            acked = status.received,
            total = prev?.total ?: img?.size ?: 0,
        )
        when (status.state) {
            HelmetProtocol.OTA_RECEIVING -> pumpOta(status.received)
            HelmetProtocol.OTA_DONE, HelmetProtocol.OTA_FAILED -> {
                image = null
                sent = 0
            }
        }
    }

    /** Tops the queue back up to the window, then sends END once all of it is out. */
    private fun pumpOta(acked: Int) {
        if (endSent) return
        val img = image ?: return
        val data = otaDataChar ?: return
        val chunk = (mtu - 3).coerceIn(20, 512)
        while (sent < img.size && sent - acked < OTA_WINDOW) {
            val end = minOf(sent + chunk, img.size)
            enqueue(Op.Write(data, img.copyOfRange(sent, end), KEY_STREAM))
            sent = end
        }
        _ota.value = _ota.value?.copy(sent = sent)
        if (sent >= img.size && !endSent) {
            endSent = true
            otaCtrlChar?.let { enqueue(Op.Write(it, HelmetProtocol.otaEnd(), KEY_OTA_CTRL)) }
        }
    }

    private companion object {
        const val OP_TIMEOUT_MS = 1500L
        const val DEFAULT_MTU = 23
        const val MAX_MTU = 517
        // Write keys. Controls coalesce by key; KEY_STREAM never does.
        const val KEY_STREAM = -1
        const val KEY_OTA_CTRL = -2
        // How far ahead of the helmet's confirmed position the app may get.
        const val OTA_WINDOW = 32 * 1024
    }
}

/** A firmware update in flight, as the UI needs to show it. */
data class OtaTransfer(
    val state: Int,
    val error: Int,
    val sent: Int,
    val acked: Int,
    val total: Int,
) {
    val inFlight: Boolean
        get() = state == HelmetProtocol.OTA_IDLE ||
            state == HelmetProtocol.OTA_RECEIVING ||
            state == HelmetProtocol.OTA_VERIFYING

    /** 0..100, by what the helmet has confirmed rather than what was sent. */
    val percent: Int get() = if (total > 0) (acked.toLong() * 100 / total).toInt() else 0
}
