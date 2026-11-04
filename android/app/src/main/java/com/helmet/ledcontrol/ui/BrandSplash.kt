package com.helmet.ledcontrol.ui

import android.provider.Settings
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import com.helmet.ledcontrol.R
import com.helmet.ledcontrol.ui.theme.AlienTrails
import com.helmet.ledcontrol.ui.theme.AtType
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** How long the logo card holds before handing over to the controls. */
const val SPLASH_HOLD_MS = 2500L

/**
 * The Alien Trails Labs boot card: the mark pops in, the wordmark follows, a
 * status line types out like a terminal and ends on a blinking block cursor.
 * Calls [onFinished] after [SPLASH_HOLD_MS]. With system animations off the
 * end state shows at once, no blink.
 */
@Composable
fun BrandSplash(statusLine: String, onFinished: () -> Unit) {
    val context = LocalContext.current
    val reducedMotion = remember {
        Settings.Global.getFloat(
            context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f,
        ) == 0f
    }
    val done by rememberUpdatedState(onFinished)

    val markScale = remember { Animatable(if (reducedMotion) 1f else 0.62f) }
    val markAlpha = remember { Animatable(if (reducedMotion) 1f else 0f) }
    val wordAlpha = remember { Animatable(if (reducedMotion) 1f else 0f) }
    var typed by remember { mutableIntStateOf(if (reducedMotion) statusLine.length else 0) }
    var cursorOn by remember { mutableStateOf(reducedMotion) }

    LaunchedEffect(Unit) {
        if (!reducedMotion) {
            launch {
                delay(80)
                launch { markAlpha.animateTo(1f, tween(220)) }
                markScale.animateTo(
                    1f,
                    spring(dampingRatio = Spring.DampingRatioMediumBouncy, stiffness = Spring.StiffnessLow),
                )
            }
            launch {
                delay(420)
                wordAlpha.animateTo(1f, tween(300))
            }
            launch {
                delay(760)
                cursorOn = true
                for (i in 1..statusLine.length) {
                    typed = i
                    delay(35) // 30–40 ms a character
                }
                while (true) {
                    delay(500)
                    cursorOn = !cursorOn
                }
            }
        }
        delay(SPLASH_HOLD_MS)
        done()
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(AlienTrails.Void),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            // Mark over wordmark, separated by the mark's clear space (a quarter
            // of its height). Both are the untouched raster masters.
            Image(
                painter = painterResource(R.drawable.at_mark),
                contentDescription = "Alien Trails Labs",
                modifier = Modifier
                    .height(132.dp)
                    .graphicsLayer {
                        scaleX = markScale.value
                        scaleY = markScale.value
                        alpha = markAlpha.value
                    },
            )
            Spacer(Modifier.height(33.dp))
            Image(
                painter = painterResource(R.drawable.at_wordmark),
                contentDescription = null,
                modifier = Modifier
                    .width(272.dp)
                    .alpha(wordAlpha.value),
            )
            Spacer(Modifier.height(AtSpace.s12))
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(3.dp),
            ) {
                // The full line is laid out invisibly so typing never re-centres.
                Box {
                    Text(statusLine, style = AtType.label, color = AlienTrails.Void.copy(alpha = 0f))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(statusLine.take(typed), style = AtType.label, color = AlienTrails.InkMuted)
                        Box(
                            Modifier
                                .padding(start = 3.dp)
                                .size(width = 7.dp, height = 14.dp)
                                .alpha(if (cursorOn) 1f else 0f)
                                .background(AlienTrails.Phosphor),
                        )
                    }
                }
            }
        }
    }
}
