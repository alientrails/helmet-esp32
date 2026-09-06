package com.helmet.ledcontrol

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

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
        super.onCreate(savedInstanceState)
        ble = HelmetBle(applicationContext)

        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    HelmetScreen(ble = ble, onConnectClick = { requestScan() })
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
fun HelmetScreen(ble: HelmetBle, onConnectClick: () -> Unit) {
    val connectionState by ble.connectionState.collectAsState()
    val deviceName by ble.deviceName.collectAsState()
    val state by ble.state.collectAsState()
    val error by ble.lastError.collectAsState()
    var brightnessSlider by remember { mutableStateOf<Float?>(null) }

    Scaffold(
        topBar = {
            TopAppBar(title = { Text("Helmet LED") })
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            ConnectionCard(
                connectionState = connectionState,
                deviceName = deviceName,
                error = error,
                onConnectClick = onConnectClick,
                onDisconnectClick = { ble.disconnect() },
            )

            if (connectionState == ConnectionState.CONNECTED) {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("Lights on", style = MaterialTheme.typography.titleMedium)
                        Switch(checked = state.on, onCheckedChange = { ble.setOn(it) })
                    }
                }

                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Brightness", style = MaterialTheme.typography.titleMedium)
                        Slider(
                            value = brightnessSlider ?: (state.brightness / 255f),
                            onValueChange = { brightnessSlider = it },
                            onValueChangeFinished = {
                                brightnessSlider?.let { ble.setBrightness((it * 255).toInt()) }
                                brightnessSlider = null
                            },
                        )
                    }
                }

                Text("Mode", style = MaterialTheme.typography.titleMedium)
                LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(HelmetProtocol.MODE_NAMES.size) { index ->
                        ModeRow(
                            name = HelmetProtocol.MODE_NAMES[index],
                            selected = state.mode == index,
                            onClick = { ble.setMode(index) },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ConnectionCard(
    connectionState: ConnectionState,
    deviceName: String?,
    error: String?,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            val statusText = when (connectionState) {
                ConnectionState.DISCONNECTED -> "Disconnected"
                ConnectionState.SCANNING -> "Scanning…"
                ConnectionState.CONNECTING -> "Connecting…"
                ConnectionState.CONNECTED -> "Connected to ${deviceName ?: "helmet"}"
            }
            Text(statusText, style = MaterialTheme.typography.titleMedium)
            if (error != null) {
                Text(error, color = MaterialTheme.colorScheme.error)
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                if (connectionState == ConnectionState.DISCONNECTED) {
                    Button(onClick = onConnectClick) { Text("Connect") }
                } else if (connectionState == ConnectionState.CONNECTED) {
                    Button(onClick = onDisconnectClick) { Text("Disconnect") }
                }
            }
        }
    }
}

@Composable
private fun ModeRow(name: String, selected: Boolean, onClick: () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(name, style = MaterialTheme.typography.bodyLarge)
            Button(onClick = onClick, enabled = !selected) {
                Text(if (selected) "Active" else "Select")
            }
        }
    }
}
