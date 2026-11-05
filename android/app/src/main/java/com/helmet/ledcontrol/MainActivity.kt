package com.helmet.ledcontrol

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.Crossfade
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.helmet.ledcontrol.ui.AtSpace
import com.helmet.ledcontrol.ui.BassSection
import com.helmet.ledcontrol.ui.BrandSplash
import com.helmet.ledcontrol.ui.ColourSection
import com.helmet.ledcontrol.ui.Eyebrow
import com.helmet.ledcontrol.ui.Hairline
import com.helmet.ledcontrol.ui.ModeGrid
import com.helmet.ledcontrol.ui.Panel
import com.helmet.ledcontrol.ui.PowerSwitch
import com.helmet.ledcontrol.ui.PrimaryButton
import com.helmet.ledcontrol.ui.SecondaryButton
import com.helmet.ledcontrol.ui.SettingSlider
import com.helmet.ledcontrol.ui.Signal
import com.helmet.ledcontrol.ui.SignalTag
import com.helmet.ledcontrol.ui.StripSection
import com.helmet.ledcontrol.ui.SystemSection
import com.helmet.ledcontrol.ui.TrailSlider
import com.helmet.ledcontrol.ui.VerticalHairline
import com.helmet.ledcontrol.ui.theme.AlienTrails
import com.helmet.ledcontrol.ui.theme.AtType
import com.helmet.ledcontrol.ui.theme.HelmetTheme

class MainActivity : ComponentActivity() {
    private lateinit var ble: HelmetBle

    private val requestPermissions = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { granted ->
        if (granted.values.all { it }) {
            ble.startScan()
        }
    }

    private val requestEnableBt = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult(),
    ) { startScanIfPermitted() }

    override fun onCreate(savedInstanceState: Bundle?) {
        // Dark first: void behind the status and navigation bars too.
        val bars = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT)
        enableEdgeToEdge(statusBarStyle = bars, navigationBarStyle = bars)
        super.onCreate(savedInstanceState)
        ble = HelmetBle(applicationContext)

        val version = runCatching {
            packageManager.getPackageInfo(packageName, 0).versionName
        }.getOrNull() ?: "1.0"

        setContent {
            HelmetTheme {
                // Survives rotation, so the logo shows once per launch.
                var showSplash by rememberSaveable { mutableStateOf(true) }
                Crossfade(
                    targetState = showSplash,
                    animationSpec = tween(300),
                    label = "splash",
                ) { splash ->
                    if (splash) {
                        BrandSplash(
                            statusLine = "HELMET LED // v$version",
                            onFinished = { showSplash = false },
                        )
                    } else {
                        HelmetScreen(ble = ble, appVersion = version, onConnectClick = { requestScan() })
                    }
                }
            }
        }
    }

    private fun requiredPermissions(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    private fun requestScan() {
        val missing = requiredPermissions().filter {
            checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            requestPermissions.launch(missing.toTypedArray())
            return
        }
        startScanIfPermitted()
    }

    private fun startScanIfPermitted() {
        if (!ble.isBluetoothEnabled()) {
            requestEnableBt.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            return
        }
        ble.startScan()
    }
}

@Composable
fun HelmetScreen(ble: HelmetBle, appVersion: String, onConnectClick: () -> Unit) {
    val connectionState by ble.connectionState.collectAsState()
    val deviceName by ble.deviceName.collectAsState()
    val state by ble.state.collectAsState()
    val meter by ble.meter.collectAsState()
    val error by ble.lastError.collectAsState()
    var brightnessDrag by remember { mutableStateOf<Float?>(null) }
    val connected = connectionState == ConnectionState.CONNECTED

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(AlienTrails.Void)
            .safeDrawingPadding(),
    ) {
        TopBar(connectionState = connectionState, hasError = error != null)
        Hairline()

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = AtSpace.s6, vertical = AtSpace.s6),
            verticalArrangement = Arrangement.spacedBy(AtSpace.s6),
        ) {
            LinkPanel(
                connectionState = connectionState,
                deviceName = deviceName,
                error = error,
                onConnectClick = onConnectClick,
                onDisconnectClick = { ble.disconnect() },
            )

            if (connected) {
                // Hero: what the helmet is doing right now, and the power switch.
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.Bottom,
                ) {
                    Column(Modifier.weight(1f)) {
                        Eyebrow(if (state.on) "Now running" else "Lights off")
                        Spacer(Modifier.height(AtSpace.s1))
                        Text(
                            HelmetProtocol.MODE_NAMES.getOrElse(state.mode) { "—" },
                            style = AtType.display,
                            color = if (state.on) AlienTrails.Ink else AlienTrails.InkMuted,
                        )
                    }
                    PowerSwitch(checked = state.on, onCheckedChange = { ble.setOn(it) })
                }

                // Brightness and speed together, as on the CYD's header row:
                // both are adjusted mid-ride, so neither hides further down.
                Panel {
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        Eyebrow("Brightness", Modifier.weight(1f))
                        val shown = brightnessDrag ?: (state.brightness / 255f)
                        Text("${(shown * 100).toInt()}%", style = AtType.data, color = AlienTrails.Ink)
                    }
                    Spacer(Modifier.height(AtSpace.s2))
                    TrailSlider(
                        value = brightnessDrag ?: (state.brightness / 255f),
                        // Sent while dragging, so the helmet follows the finger;
                        // the BLE queue keeps only the newest value in flight.
                        onValueChange = {
                            brightnessDrag = it
                            ble.setBrightness((it * 255).toInt())
                        },
                        onValueChangeFinished = { brightnessDrag = null },
                    )
                    if (state.hasSpeed) {
                        Spacer(Modifier.height(AtSpace.s3))
                        SettingSlider(
                            "Speed", state.speed, HelmetProtocol.SPEED_MIN..255,
                            { "${it * 100 / HelmetProtocol.SPEED_NORMAL}%" },
                            { ble.setSpeed(it) },
                        )
                    }
                }

                Column(verticalArrangement = Arrangement.spacedBy(AtSpace.s3)) {
                    Eyebrow("Modes")
                    ModeGrid(
                        names = HelmetProtocol.MODE_NAMES,
                        selected = state.mode,
                        onSelect = { ble.setMode(it) },
                    )
                }

                // The rest of the CYD, in its tab order. Each only shows when
                // the helmet's firmware reports the fields behind it.
                if (state.hasColour) {
                    ColourSection(state, onPick = { r, g, b -> ble.setColor(r, g, b) })
                }
                if (state.hasBass) {
                    BassSection(
                        state = state,
                        meter = meter,
                        onBass = { param, value -> ble.setBass(param, value) },
                        onSelectBass = { ble.setMode(HelmetProtocol.MODE_BASS) },
                    )
                }
                if (state.hasStrip) {
                    StripSection(
                        state = state,
                        onOrder = { ble.setOrder(it) },
                        onCount = { ble.setCount(it) },
                        onSkip = { ble.setSkip(it) },
                        onMaxMa = { ble.setMaxMa(it) },
                    )
                }
                SystemSection(deviceName, appVersion, state)
            } else {
                Text(
                    "Power the helmet, then connect. It advertises as Helmet-LED-XXXX " +
                        "over Bluetooth LE — no Wi-Fi needed.",
                    style = AtType.small,
                    color = AlienTrails.InkMuted,
                )
            }

            Text(
                "// alientrails",
                style = AtType.data,
                color = AlienTrails.InkMuted,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

@Composable
private fun TopBar(connectionState: ConnectionState, hasError: Boolean) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(AlienTrails.Hull)
            .padding(horizontal = AtSpace.s6, vertical = AtSpace.s3),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // The mark as a maker's stamp; 28dp keeps it above the 24px minimum.
        Image(
            painter = painterResource(R.drawable.at_mark),
            contentDescription = "Alien Trails Labs",
            modifier = Modifier.height(28.dp),
        )
        Spacer(Modifier.width(AtSpace.s3))
        VerticalHairline(height = 20.dp)
        Spacer(Modifier.width(AtSpace.s3))
        Text("Helmet LED", style = AtType.heading, color = AlienTrails.Ink, modifier = Modifier.weight(1f))
        val (signal, word) = linkSignal(connectionState, hasError)
        SignalTag(signal, word)
    }
}

private fun linkSignal(state: ConnectionState, hasError: Boolean): Pair<Signal, String> = when {
    state == ConnectionState.CONNECTED -> Signal.ONLINE to "Online"
    state == ConnectionState.SCANNING -> Signal.SCANNING to "Scanning"
    state == ConnectionState.CONNECTING -> Signal.SCANNING to "Linking"
    hasError -> Signal.ALERT to "Offline"
    else -> Signal.IDLE to "Idle"
}

@Composable
private fun LinkPanel(
    connectionState: ConnectionState,
    deviceName: String?,
    error: String?,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit,
) {
    Panel {
        Eyebrow("Link // BLE")
        Spacer(Modifier.height(AtSpace.s2))
        val reading = when (connectionState) {
            ConnectionState.DISCONNECTED -> "no device"
            ConnectionState.SCANNING -> "scanning for helmet-led-*"
            ConnectionState.CONNECTING -> "linking ${deviceName ?: "helmet"}"
            ConnectionState.CONNECTED -> (deviceName ?: "helmet").lowercase()
        }
        Text(reading, style = AtType.code, color = AlienTrails.Ink)
        if (error != null) {
            Spacer(Modifier.height(AtSpace.s2))
            Row(verticalAlignment = Alignment.CenterVertically) {
                SignalTag(Signal.ALERT, "Error")
                Spacer(Modifier.width(AtSpace.s2))
                Text(error, style = AtType.small, color = AlienTrails.InkMuted)
            }
        }
        when (connectionState) {
            ConnectionState.DISCONNECTED -> {
                Spacer(Modifier.height(AtSpace.s4))
                PrimaryButton("Connect", onConnectClick, Modifier.fillMaxWidth())
            }
            ConnectionState.CONNECTED -> {
                Spacer(Modifier.height(AtSpace.s4))
                SecondaryButton("Disconnect", onDisconnectClick, Modifier.fillMaxWidth())
            }
            else -> Box {}
        }
    }
}
