package dev.busung.s25uroot

import android.content.Context
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.view.HapticFeedbackConstants
import android.view.View
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.annotation.StringRes
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.togetherWith
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Error
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Security
import androidx.compose.material.icons.rounded.Thermostat
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearWavyProgressIndicator
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.busung.s25uroot.ui.theme.ColorScheme_success
import dev.busung.s25uroot.ui.theme.RootMyGalaxyTheme
import kotlinx.coroutines.delay
import java.util.Locale

class InstallActivity : ComponentActivity() {
    private val installViewModel by viewModels<InstallViewModel>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val profileId = intent.getStringExtra(EXTRA_PROFILE_ID)
        val uninstallRoot = intent.getBooleanExtra(EXTRA_UNINSTALL_ROOT, false)
        val startInstall = savedInstanceState == null && AppPreferences.consumeInstallRequest(
            this,
            intent.getStringExtra(EXTRA_INSTALL_REQUEST_ID),
        )
        intent.removeExtra(EXTRA_INSTALL_REQUEST_ID)
        setContent {
            RootMyGalaxyTheme(
                accentColor = AppPreferences.accentColor(this),
                themeMode = AppPreferences.themeMode(this),
            ) {
                val installState by installViewModel.state.collectAsStateWithLifecycle()
                BackHandler(enabled = installState.busy) {}
                LaunchedEffect(startInstall, profileId) {
                    if (startInstall) {
                        if (uninstallRoot) installViewModel.uninstallRoot(profileId)
                        else installViewModel.install(profileId)
                    }
                }
                InstallScreen(
                    installState = installState,
                    uninstallRoot = uninstallRoot,
                    onRetry = {
                        if (uninstallRoot) installViewModel.uninstallRoot(profileId)
                        else installViewModel.install(profileId)
                    },
                    onClose = ::finish,
                )
            }
        }
    }

    companion object {
        const val EXTRA_INSTALL_REQUEST_ID = "install_request_id"
        const val EXTRA_PROFILE_ID = "profile_id"
        const val EXTRA_UNINSTALL_ROOT = "uninstall_root"
    }
}

internal data class InstallerStep(
    @StringRes val title: Int,
    @StringRes val detail: Int,
    val icon: ImageVector,
)

internal val installerSteps = listOf(
    InstallerStep(R.string.step_support_title, R.string.step_support_detail, Icons.Rounded.Security),
    InstallerStep(R.string.step_download_title, R.string.step_download_detail, Icons.Rounded.CloudDownload),
    InstallerStep(R.string.step_exploit_title, R.string.step_exploit_detail, Icons.Rounded.Memory),
    InstallerStep(R.string.step_ksu_title, R.string.step_ksu_detail, Icons.Rounded.Check),
)

private fun vibrateSuccess(context: Context) {
    val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        context.getSystemService(VibratorManager::class.java)?.defaultVibrator
    } else {
        @Suppress("DEPRECATION")
        context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
    } ?: return
    if (!vibrator.hasVibrator()) return
    // Crisp iOS-style double tap.
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        vibrator.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_DOUBLE_CLICK))
    } else {
        val timings = longArrayOf(0, 22, 55, 22)
        val amplitudes = intArrayOf(0, 255, 0, 255)
        vibrator.vibrate(VibrationEffect.createWaveform(timings, amplitudes, -1))
    }
}

private fun clickHaptic(view: View) {
    view.performHapticFeedback(
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            HapticFeedbackConstants.CONFIRM
        } else {
            HapticFeedbackConstants.LONG_PRESS
        },
    )
}

@Composable
private fun InstallScreen(
    installState: InstallUiState,
    uninstallRoot: Boolean,
    onRetry: () -> Unit,
    onClose: () -> Unit,
) {
    val view = LocalView.current
    val context = LocalContext.current
    val scrollState = rememberScrollState()
    var uptimeMillis by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    LaunchedEffect(Unit) {
        while (true) {
            uptimeMillis = SystemClock.elapsedRealtime()
            delay(1_000)
        }
    }
    var viewportCoords by remember { mutableStateOf<LayoutCoordinates?>(null) }
    var activeStepCoords by remember { mutableStateOf<LayoutCoordinates?>(null) }

    // Celebrate a freshly acquired root with a vibration (not on a screen that
    // simply opens already rooted).
    var previousPhase by remember { mutableStateOf<InstallPhase?>(null) }
    LaunchedEffect(installState.phase) {
        if (installState.phase == InstallPhase.Installed &&
            previousPhase != null &&
            previousPhase != InstallPhase.Installed
        ) {
            vibrateSuccess(context)
        }
        previousPhase = installState.phase
    }

    // Smoothly keep the active step centred as execution advances.
    LaunchedEffect(installState.executionStage, installState.phase) {
        if (!installState.busy) return@LaunchedEffect
        if (installState.executionStage == ExecutionStage.Stabilizing) {
            scrollState.animateScrollTo(0)
            return@LaunchedEffect
        }
        delay(90)
        val viewport = viewportCoords ?: return@LaunchedEffect
        val row = activeStepCoords ?: return@LaunchedEffect
        if (!viewport.isAttached || !row.isAttached) return@LaunchedEffect
        val rowTop = viewport.localPositionOf(row, Offset.Zero).y
        val target = (scrollState.value + rowTop - (viewport.size.height - row.size.height) / 2f)
            .coerceIn(0f, scrollState.maxValue.toFloat())
        scrollState.animateScrollTo(
            target.toInt(),
            animationSpec = tween(durationMillis = 650, easing = FastOutSlowInEasing),
        )
    }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 20.dp),
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 20.dp, bottom = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text(
                    text = stringResource(R.string.install_title),
                    style = MaterialTheme.typography.headlineLarge,
                    modifier = Modifier.weight(1f),
                )
                Surface(
                    shape = CircleShape,
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                ) {
                    Text(
                        text = stringResource(R.string.device_uptime, formatElapsedTime(uptimeMillis)),
                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 7.dp),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .onGloballyPositioned { viewportCoords = it }
                    .verticalScroll(scrollState),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                if (installState.busy) {
                    Text(
                        text = stringResource(R.string.install_keep_open),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }

                if (installState.phase == InstallPhase.Installed) {
                    SuccessCard(installState, uninstallRoot)
                } else {
                    StatusProgressCard(installState, uninstallRoot)
                }
                ExecutionStepper(
                    installState = installState,
                    onActiveStepPositioned = { activeStepCoords = it },
                )
                InstallerLog(installState)
                Spacer(Modifier.height(4.dp))
            }

            if (!installState.busy) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 12.dp, bottom = 20.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    if (installState.phase == InstallPhase.Failed) {
                        FilledTonalButton(
                            onClick = {
                                clickHaptic(view)
                                onClose()
                            },
                            modifier = Modifier.weight(1f),
                        ) {
                            Text(stringResource(R.string.action_close))
                        }
                        Button(
                            onClick = {
                                clickHaptic(view)
                                onRetry()
                            },
                            modifier = Modifier.weight(1f),
                        ) {
                            Text(stringResource(R.string.action_retry))
                        }
                    } else if (installState.phase == InstallPhase.Installed) {
                        Button(
                            onClick = {
                                clickHaptic(view)
                                onClose()
                            },
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text(stringResource(R.string.action_done))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun SuccessCard(installState: InstallUiState, uninstallRoot: Boolean) {
    val success = ColorScheme_success
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .animateContentSize(),
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(
            containerColor = success.container,
            contentColor = success.onContainer,
        ),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                Box(
                    modifier = Modifier
                        .size(52.dp)
                        .clip(CircleShape)
                        .background(success.color),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        Icons.Rounded.Check,
                        contentDescription = null,
                        tint = success.onColor,
                        modifier = Modifier.size(30.dp),
                    )
                }
                Text(
                    text = if (uninstallRoot) installState.message
                        else stringResource(R.string.install_success_title),
                    style = MaterialTheme.typography.headlineSmall,
                    modifier = Modifier.weight(1f),
                )
            }
            Text(
                text = installPhaseDetail(installState, uninstallRoot),
                style = MaterialTheme.typography.bodyMedium,
                color = success.onContainer.copy(alpha = 0.85f),
            )
            installState.completionDurationMillis?.let { duration ->
                Text(
                    text = stringResource(R.string.install_duration, formatElapsedTime(duration)),
                    style = MaterialTheme.typography.titleMedium,
                )
            }
            if (installState.rootActive) {
                Surface(shape = CircleShape, color = success.color) {
                    Text(
                        text = stringResource(R.string.root_live_active),
                        modifier = Modifier.padding(horizontal = 18.dp, vertical = 9.dp),
                        color = success.onColor,
                        style = MaterialTheme.typography.labelLarge,
                    )
                }
            }
        }
    }
}

@Composable
private fun StatusProgressCard(installState: InstallUiState, uninstallRoot: Boolean) {
    val failed = installState.phase == InstallPhase.Failed
    val requestedProgress = if (installState.phase == InstallPhase.Checking ||
        installState.phase == InstallPhase.Downloading
    ) null else executionJourneyProgress(installState.executionStage, installState.stabilizationMetrics)
    var highestProgress by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(installState.phase, requestedProgress) {
        if (installState.phase == InstallPhase.Checking) {
            highestProgress = 0f
        } else if (requestedProgress != null) {
            highestProgress = maxOf(highestProgress, requestedProgress)
        }
    }
    val animatedProgress by animateFloatAsState(
        targetValue = highestProgress,
        animationSpec = tween(650, easing = FastOutSlowInEasing),
        label = "journey-progress",
    )

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .animateContentSize(),
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(
            containerColor = if (failed) {
                MaterialTheme.colorScheme.errorContainer
            } else {
                MaterialTheme.colorScheme.primaryContainer
            },
            contentColor = if (failed) {
                MaterialTheme.colorScheme.onErrorContainer
            } else {
                MaterialTheme.colorScheme.onPrimaryContainer
            },
        ),
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(18.dp),
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                AnimatedContent(targetState = installState.busy, label = "install-status-icon") { busy ->
                    if (busy) {
                        LoadingIndicator(
                            modifier = Modifier.size(44.dp),
                            color = MaterialTheme.colorScheme.onPrimaryContainer,
                        )
                    } else {
                        Icon(
                            Icons.Rounded.Error,
                            contentDescription = null,
                            modifier = Modifier.size(44.dp),
                        )
                    }
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = if (installState.busy) executionStageTitle(installState.executionStage)
                            else installState.message,
                        style = MaterialTheme.typography.titleLarge,
                    )
                    Text(
                        text = if (installState.busy) {
                            if (installState.executionStage == ExecutionStage.Stabilizing) {
                                stringResource(
                                    if (installState.stabilizationMetrics == null) R.string.stabilization_waiting
                                    else R.string.stabilization_explanation,
                                )
                            } else {
                                installState.executionDetail?.let { localizedExecutionDetail(it) }
                                    ?: executionStageDetail(installState.executionStage)
                            }
                        } else {
                            installPhaseDetail(installState, uninstallRoot)
                        },
                        color = LocalContentColor.current.copy(alpha = 0.78f),
                    )
                }
            }
            if (!failed && installState.executionStage == ExecutionStage.Stabilizing) {
                val metrics = installState.stabilizationMetrics
                AnimatedVisibility(visible = metrics != null) {
                    if (metrics != null) {
                        StabilizationPanel(metrics)
                    }
                }
            }
            if (!failed) {
                if (requestedProgress == null) {
                    LinearWavyProgressIndicator(
                        modifier = Modifier.fillMaxWidth(),
                        color = LocalContentColor.current,
                        trackColor = LocalContentColor.current.copy(alpha = 0.2f),
                    )
                } else {
                    LinearWavyProgressIndicator(
                        progress = { animatedProgress },
                        modifier = Modifier.fillMaxWidth(),
                        color = LocalContentColor.current,
                        trackColor = LocalContentColor.current.copy(alpha = 0.2f),
                    )
                }
            }
        }
    }
}

@Composable
private fun StabilizationPanel(metrics: StabilizationMetrics) {
    val progress by animateFloatAsState(
        targetValue = (metrics.sample.toFloat() / metrics.requiredSamples.coerceAtLeast(1)).coerceIn(0f, 1f),
        animationSpec = tween(650, easing = FastOutSlowInEasing),
        label = "stabilization-progress",
    )
    Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    stringResource(R.string.stabilization_readings),
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    stringResource(R.string.stabilization_live),
                    style = MaterialTheme.typography.bodySmall,
                    color = LocalContentColor.current.copy(alpha = 0.75f),
                )
            }
            AnimatedContent(
                targetState = "${metrics.sample}/${metrics.requiredSamples}",
                transitionSpec = {
                    (fadeIn(tween(280)) + scaleIn(initialScale = 0.8f))
                        .togetherWith(fadeOut(tween(180)) + scaleOut(targetScale = 1.1f))
                },
                label = "stabilization-count",
            ) { count ->
                Text(count, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.SemiBold)
            }
        }
        LinearWavyProgressIndicator(
            progress = { progress },
            modifier = Modifier.fillMaxWidth(),
            color = LocalContentColor.current,
            trackColor = LocalContentColor.current.copy(alpha = 0.2f),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StabilizationMetric(
                label = stringResource(R.string.stabilization_temperature),
                value = stringResource(R.string.stabilization_temperature_value, metrics.temperatureCelsius),
                icon = Icons.Rounded.Thermostat,
                containerColor = MaterialTheme.colorScheme.tertiaryContainer,
                contentColor = MaterialTheme.colorScheme.onTertiaryContainer,
                modifier = Modifier.weight(1f),
            )
            StabilizationMetric(
                label = stringResource(R.string.stabilization_memory),
                value = formatMemoryGb(metrics.availableMemoryMb),
                icon = Icons.Rounded.Memory,
                containerColor = MaterialTheme.colorScheme.secondaryContainer,
                contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun StabilizationMetric(
    label: String,
    value: String,
    icon: ImageVector,
    containerColor: Color,
    contentColor: Color,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier = modifier,
        shape = MaterialTheme.shapes.large,
        color = containerColor,
        contentColor = contentColor,
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(22.dp))
            Text(label, style = MaterialTheme.typography.labelMedium)
            AnimatedContent(
                targetState = value,
                transitionSpec = {
                    (fadeIn(tween(280)) + scaleIn(initialScale = 0.9f))
                        .togetherWith(fadeOut(tween(160)))
                },
                label = "metric-value",
            ) { currentValue ->
                Text(currentValue, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
            }
        }
    }
}

private fun formatMemoryGb(availableMemoryMb: Int): String =
    String.format(Locale.getDefault(), "%.1f GB", availableMemoryMb / 1_024.0)

private fun formatElapsedTime(millis: Long): String {
    val seconds = millis.coerceAtLeast(0L) / 1_000L
    val hours = seconds / 3_600L
    val minutes = (seconds % 3_600L) / 60L
    val remainingSeconds = seconds % 60L
    return if (hours > 0) "%d:%02d:%02d".format(hours, minutes, remainingSeconds)
        else "%d:%02d".format(minutes, remainingSeconds)
}

private enum class StepStatus { Completed, Active, Pending, Failed }

@Composable
private fun ExecutionStepper(
    installState: InstallUiState,
    onActiveStepPositioned: (LayoutCoordinates) -> Unit,
) {
    val current = installState.executionStage
    val installed = installState.phase == InstallPhase.Installed
    val failed = installState.phase == InstallPhase.Failed
    val stages = ExecutionStage.entries
    val view = LocalView.current
    var expanded by remember { mutableStateOf(true) }
    LaunchedEffect(installState.phase) {
        if (installed) expanded = false
        if (failed) expanded = true
    }

    // Per-step timing. Each stage records when it first became current and when
    // execution moved past it; the active stage keeps counting against `now`.
    val starts = remember { mutableStateMapOf<ExecutionStage, Long>() }
    val ends = remember { mutableStateMapOf<ExecutionStage, Long>() }
    var now by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    LaunchedEffect(installState.busy) {
        while (installState.busy) {
            now = SystemClock.elapsedRealtime()
            delay(500)
        }
        now = SystemClock.elapsedRealtime()
    }
    LaunchedEffect(installState.phase, current) {
        val t = SystemClock.elapsedRealtime()
        if (installState.phase == InstallPhase.Checking) {
            starts.clear()
            ends.clear()
        }
        if (starts[current] == null) starts[current] = t
        stages.forEach { s ->
            if (s.ordinal < current.ordinal && starts[s] != null && ends[s] == null) {
                ends[s] = t
            }
        }
        if ((installed || failed) && ends[current] == null) ends[current] = t
    }

    fun durationText(stage: ExecutionStage): String? {
        val start = starts[stage] ?: return null
        val end = ends[stage] ?: now
        val elapsed = (end - start).coerceAtLeast(0L)
        if (ends[stage] != null && elapsed < 1_000L) return "<1 s"
        val seconds = elapsed / 1_000L
        return "%d:%02d".format(seconds / 60, seconds % 60)
    }

    val chevronRotation by animateFloatAsState(if (expanded) 180f else 0f, label = "steps-chevron")
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .animateContentSize(),
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
        ),
    ) {
        Column(modifier = Modifier.padding(bottom = if (expanded) 4.dp else 0.dp)) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable {
                        clickHaptic(view)
                        expanded = !expanded
                    }
                    .padding(horizontal = 20.dp, vertical = 18.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Column(Modifier.weight(1f)) {
                    Text(
                        text = stringResource(R.string.execution_journey_title),
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        text = if (installed) stringResource(R.string.execution_all_completed)
                            else stringResource(
                                R.string.execution_stage_counter,
                                (current.ordinal + 1).coerceAtMost(stages.size),
                            ),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Icon(
                    Icons.Rounded.ExpandMore,
                    contentDescription = if (expanded) stringResource(R.string.execution_collapse)
                        else stringResource(R.string.execution_expand),
                    modifier = Modifier.rotate(chevronRotation),
                )
            }
            AnimatedVisibility(visible = expanded) {
                Column(modifier = Modifier.padding(start = 20.dp, end = 20.dp, bottom = 12.dp)) {
                    stages.forEachIndexed { index, stage ->
                val status = when {
                    installed -> StepStatus.Completed
                    failed && stage == current -> StepStatus.Failed
                    stage.ordinal < current.ordinal -> StepStatus.Completed
                    stage.ordinal == current.ordinal -> StepStatus.Active
                    else -> StepStatus.Pending
                }
                val isLast = index == stages.lastIndex
                val attach = status == StepStatus.Active || (installed && isLast)
                StepRow(
                    modifier = if (attach) {
                        Modifier.onGloballyPositioned { onActiveStepPositioned(it) }
                    } else {
                        Modifier
                    },
                    stage = stage,
                    stepNumber = index + 1,
                    status = status,
                    busy = installState.busy,
                    isFirst = index == 0,
                    isLast = isLast,
                    durationText = durationText(stage),
                    titleOverride = null,
                )
                    }
                }
            }
        }
    }
}

@Composable
private fun StepRow(
    stage: ExecutionStage,
    stepNumber: Int,
    status: StepStatus,
    busy: Boolean,
    isFirst: Boolean,
    isLast: Boolean,
    durationText: String?,
    titleOverride: String? = null,
    modifier: Modifier = Modifier,
) {
    val success = ColorScheme_success
    val title = titleOverride ?: executionStageTitle(stage)
    val colorSpec = tween<Color>(durationMillis = 500, easing = FastOutSlowInEasing)
    val incomingColor by animateColorAsState(
        targetValue = if (status == StepStatus.Pending) {
            MaterialTheme.colorScheme.outlineVariant
        } else {
            success.color
        },
        animationSpec = colorSpec,
        label = "incoming",
    )
    val outgoingColor by animateColorAsState(
        targetValue = if (status == StepStatus.Completed) {
            success.color
        } else {
            MaterialTheme.colorScheme.outlineVariant
        },
        animationSpec = colorSpec,
        label = "outgoing",
    )
    Row(modifier = modifier.height(IntrinsicSize.Min)) {
        // The dot is vertically centered in the item: equal-weight segments
        // above and below push it to the middle, and the segments join the
        // neighbouring dots to form the timeline.
        Column(
            modifier = Modifier
                .fillMaxHeight()
                .width(30.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Box(
                modifier = Modifier
                    .width(2.dp)
                    .weight(1f)
                    .background(if (isFirst) Color.Transparent else incomingColor),
            )
            StepIndicator(stage, stepNumber, status, busy)
            Box(
                modifier = Modifier
                    .width(2.dp)
                    .weight(1f)
                    .background(if (isLast) Color.Transparent else outgoingColor),
            )
        }
        Column(
            modifier = Modifier
                .padding(start = 14.dp, top = 10.dp, bottom = 10.dp)
                .weight(1f),
            verticalArrangement = Arrangement.spacedBy(3.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(modifier = Modifier.weight(1f)) {
                    if (status == StepStatus.Active) {
                        ShimmerTitle(title)
                    } else {
                        val titleColor by animateColorAsState(
                            targetValue = when (status) {
                                StepStatus.Completed -> success.color
                                StepStatus.Failed -> MaterialTheme.colorScheme.error
                                else -> MaterialTheme.colorScheme.onSurface
                            },
                            animationSpec = colorSpec,
                            label = "title",
                        )
                        Text(
                            text = title,
                            style = MaterialTheme.typography.titleMedium,
                            color = titleColor,
                        )
                    }
                }
                if (!isLast) {
                    val notStarted = durationText == null && status == StepStatus.Pending
                    Text(
                        text = if (notStarted) "" else durationText.orEmpty(),
                        style = MaterialTheme.typography.labelMedium,
                        fontFamily = FontFamily.Monospace,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(
                            alpha = if (notStarted) 0.4f else 1f,
                        ),
                        modifier = Modifier.padding(start = 8.dp),
                    )
                }
            }
            if (status == StepStatus.Active || status == StepStatus.Failed) {
                Text(
                    text = executionStageDetail(stage),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun ShimmerTitle(text: String) {
    // White title with a gray spotlight sweeping across it.
    val base = MaterialTheme.colorScheme.onSurface
    val spotlight = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f)
    var widthPx by remember { mutableFloatStateOf(0f) }
    val transition = rememberInfiniteTransition(label = "shimmer")
    val progress by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 1500, easing = LinearEasing),
            repeatMode = RepeatMode.Restart,
        ),
        label = "shimmer-progress",
    )
    val brush = if (widthPx > 0f) {
        val band = widthPx * 0.4f
        val startX = -band + (widthPx + band) * progress
        Brush.linearGradient(
            colors = listOf(base, spotlight, base),
            start = Offset(startX, 0f),
            end = Offset(startX + band, 0f),
        )
    } else {
        SolidColor(base)
    }
    Text(
        text = text,
        style = MaterialTheme.typography.titleMedium.copy(brush = brush),
        fontWeight = FontWeight.SemiBold,
        modifier = Modifier.onGloballyPositioned { widthPx = it.size.width.toFloat() },
    )
}

@Composable
private fun StepIndicator(
    stage: ExecutionStage,
    stepNumber: Int,
    status: StepStatus,
    busy: Boolean,
) {
    val success = ColorScheme_success
    val size = 30.dp
    val backgroundColor by animateColorAsState(
        targetValue = when (status) {
            StepStatus.Completed -> success.color
            StepStatus.Failed -> MaterialTheme.colorScheme.error
            StepStatus.Active -> MaterialTheme.colorScheme.outline
            StepStatus.Pending -> Color.Transparent
        },
        animationSpec = tween(durationMillis = 500, easing = FastOutSlowInEasing),
        label = "dot-bg",
    )
    Box(
        modifier = Modifier
            .size(size)
            .clip(CircleShape)
            .background(backgroundColor)
            .then(
                if (status == StepStatus.Pending) {
                    Modifier.border(2.dp, MaterialTheme.colorScheme.outlineVariant, CircleShape)
                } else {
                    Modifier
                },
            ),
        contentAlignment = Alignment.Center,
    ) {
        AnimatedContent(
            targetState = status,
            transitionSpec = {
                (fadeIn(tween(240)) + scaleIn(initialScale = 0.5f, animationSpec = tween(240)))
                    .togetherWith(fadeOut(tween(160)) + scaleOut(targetScale = 0.5f, animationSpec = tween(160)))
            },
            label = "dot-content",
        ) { current ->
            when (current) {
                StepStatus.Completed -> Icon(
                    Icons.Rounded.Check,
                    contentDescription = null,
                    tint = success.onColor,
                    modifier = Modifier.size(18.dp),
                )
                StepStatus.Failed -> Icon(
                    Icons.Rounded.Error,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onError,
                    modifier = Modifier.size(18.dp),
                )
                StepStatus.Active -> if (busy) {
                    LoadingIndicator(
                        modifier = Modifier.size(18.dp),
                        color = MaterialTheme.colorScheme.surface,
                    )
                } else {
                    Box(Modifier.size(18.dp))
                }
                StepStatus.Pending -> Text(
                    text = stepNumber.toString(),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun localizedExecutionDetail(detail: String): String {
    val gate = Regex("^(Estabilização|Cooldown) (\\d+/\\d+) · (.+?) · (.+?) livres · (\\d+) tarefas · PSI (.+?) · mm (.+?) · (\\d+) slabs$")
        .matchEntire(detail)
    if (gate != null) {
        val phase = if (gate.groupValues[1] == "Cooldown")
            stringResource(R.string.execution_gate_cooldown) else stringResource(R.string.execution_gate_stabilizing)
        return stringResource(
            R.string.execution_gate_progress, phase, gate.groupValues[2], gate.groupValues[3],
            gate.groupValues[4], gate.groupValues[5], gate.groupValues[6],
            gate.groupValues[7], gate.groupValues[8],
        )
    }
    val attempt = Regex("^Tentativa (\\d+) de (\\d+)\\.$").matchEntire(detail)
    if (attempt != null) return stringResource(R.string.execution_attempt, attempt.groupValues[1], attempt.groupValues[2])
    val resource = when (detail) {
        "Capacidade dos pipes aprovada; aguardando liberação do launcher." -> R.string.execution_pipe_capacity_passed
        "Confirmando estabilidade após o teste dos pipes." -> R.string.execution_pipe_cooldown
        "Verificações do launcher aprovadas; aguardando início do payload." -> R.string.execution_launcher_passed
        "Callback ainda não acionado; repetindo antes de qualquer mutação." -> R.string.execution_callback_retry
        "Kernel localizado; preparando acesso." -> R.string.execution_kernel_located
        "Kernel localizado; preparando memória para acesso." -> R.string.execution_memory_preparing
        "Etapa crítica em andamento. Não interrompa a execução." -> R.string.execution_critical_step
        "Candidato de pipe validado." -> R.string.execution_pipe_validated
        "Leitura e escrita comprovadas; preparando root temporário." -> R.string.execution_pipe_ready
        "Testando acesso pelo candidato selecionado." -> R.string.execution_pipe_testing
        "Procurando um pipe adequado para acesso à memória." -> R.string.execution_pipe_searching
        "Solicitando início do serviço de root temporário." -> R.string.execution_root_requesting
        "Aguardando confirmação do root temporário." -> R.string.execution_root_waiting
        "Canal de controle do KernelSU confirmado." -> R.string.execution_root_verified
        else -> return detail
    }
    return stringResource(resource)
}

@Composable
private fun executionStageTitle(stage: ExecutionStage): String = stringResource(
    when (stage) {
        ExecutionStage.Preparing -> R.string.execution_preparing_title
        ExecutionStage.Stabilizing -> R.string.execution_stabilizing_title
        ExecutionStage.StartingExploit -> R.string.execution_starting_title
        ExecutionStage.LocatingKernel -> R.string.execution_locating_title
        ExecutionStage.VerifyingKernelAccess -> R.string.execution_verifying_title
        ExecutionStage.StartingTemporaryRoot -> R.string.execution_temporary_root_title
        ExecutionStage.BuildingPipeBridge -> R.string.execution_pipe_title
        ExecutionStage.LoadingKernelSu -> R.string.execution_ksu_title
        ExecutionStage.VerifyingRoot -> R.string.execution_root_title
    },
)

@Composable
private fun executionStageDetail(stage: ExecutionStage): String = stringResource(
    when (stage) {
        ExecutionStage.Preparing -> R.string.execution_preparing_detail
        ExecutionStage.Stabilizing -> R.string.execution_stabilizing_detail
        ExecutionStage.StartingExploit -> R.string.execution_starting_detail
        ExecutionStage.LocatingKernel -> R.string.execution_locating_detail
        ExecutionStage.VerifyingKernelAccess -> R.string.execution_verifying_detail
        ExecutionStage.StartingTemporaryRoot -> R.string.execution_temporary_root_detail
        ExecutionStage.BuildingPipeBridge -> R.string.execution_pipe_detail
        ExecutionStage.LoadingKernelSu -> R.string.execution_ksu_detail
        ExecutionStage.VerifyingRoot -> R.string.execution_root_detail
    },
)

private fun executionStageIcon(stage: ExecutionStage): String = when (stage) {
    ExecutionStage.Preparing -> "📦"
    ExecutionStage.Stabilizing -> "🌡️"
    ExecutionStage.StartingExploit -> "🚀"
    ExecutionStage.LocatingKernel -> "🧭"
    ExecutionStage.VerifyingKernelAccess -> "🔎"
    ExecutionStage.StartingTemporaryRoot -> "🔐"
    ExecutionStage.BuildingPipeBridge -> "🌉"
    ExecutionStage.LoadingKernelSu -> "⚙️"
    ExecutionStage.VerifyingRoot -> "✅"
}

@Composable
private fun InstallerLog(installState: InstallUiState) {
    val view = LocalView.current
    var expanded by remember { mutableStateOf(false) }
    LaunchedEffect(installState.phase) {
        if (installState.phase == InstallPhase.Failed) expanded = true
    }
    val logScrollState = rememberScrollState()
    LaunchedEffect(installState.log, expanded) {
        if (expanded) logScrollState.scrollTo(logScrollState.maxValue)
    }
    val chevronRotation by animateFloatAsState(if (expanded) 180f else 0f, label = "log-chevron")

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
        ),
    ) {
        Column(modifier = Modifier.fillMaxWidth()) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable {
                        clickHaptic(view)
                        expanded = !expanded
                    }
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = stringResource(R.string.install_live_progress),
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Text(
                        text = stringResource(R.string.install_log_hint),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                    )
                }
                Icon(
                    Icons.Rounded.ExpandMore,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.rotate(chevronRotation),
                )
            }
            AnimatedVisibility(visible = expanded) {
                Text(
                    text = installState.log.ifBlank { stringResource(R.string.install_preparing) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 16.dp, end = 16.dp, bottom = 16.dp)
                        .heightIn(max = 240.dp)
                        .verticalScroll(logScrollState),
                    fontFamily = FontFamily.Monospace,
                    fontSize = 12.sp,
                    lineHeight = 18.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.85f),
                )
            }
        }
    }
}

@Composable
private fun installPhaseDetail(installState: InstallUiState, uninstallRoot: Boolean): String {
    if (installState.phase == InstallPhase.WaitingForBootAllocator) {
        val remainingMillis = installState.bootAllocatorRemainingMillis ?: 0L
        val seconds = remainingMillis / 1_000L + if (remainingMillis % 1_000L > 0L) 1L else 0L
        return stringResource(R.string.status_waiting_boot_allocator, seconds)
    }
    return stringResource(when (installState.phase) {
        InstallPhase.Checking -> R.string.phase_checking
        InstallPhase.Ready -> R.string.phase_ready
        InstallPhase.Downloading -> R.string.phase_downloading
        InstallPhase.WaitingForBootAllocator -> R.string.phase_waiting_boot_allocator
        InstallPhase.Exploiting -> R.string.phase_exploiting
        InstallPhase.LoadingKernelSu -> if (uninstallRoot) R.string.phase_uninstalling_root else R.string.phase_loading_ksu
        InstallPhase.Installed -> if (uninstallRoot) R.string.phase_root_uninstalled else R.string.phase_installed
        InstallPhase.Failed -> R.string.phase_failed
    })
}

internal fun installPhaseProgress(
    phase: InstallPhase,
    elapsedMillis: Long,
    rootDurationMillis: Long,
    bootAllocatorRemainingMillis: Long? = null,
    bootAllocatorTotalMillis: Long? = null,
): Float {
    if (phase == InstallPhase.Installed) return 1f
    val durationMillis = when (phase) {
        InstallPhase.Checking -> 8_000L
        InstallPhase.Ready -> 1L
        InstallPhase.Downloading -> 5_000L
        // The first run uses AppPreferences' 2-minute default. Later runs use
        // the exact successful exploit duration saved by InstallViewModel.
        InstallPhase.WaitingForBootAllocator -> bootAllocatorTotalMillis?.coerceAtLeast(1_000L) ?: 1_000L
        InstallPhase.Exploiting -> rootDurationMillis.coerceAtLeast(1_000L)
        InstallPhase.LoadingKernelSu -> 12_000L
        InstallPhase.Installed -> 1L
        InstallPhase.Failed -> 1L
    }
    val progress = if (phase == InstallPhase.WaitingForBootAllocator &&
        bootAllocatorRemainingMillis != null && bootAllocatorTotalMillis != null &&
        bootAllocatorTotalMillis > 0L
    ) {
        1f - (bootAllocatorRemainingMillis.toFloat() / bootAllocatorTotalMillis.toFloat())
    } else {
        elapsedMillis.coerceAtLeast(0L).toFloat() / durationMillis
    }
    return progress
        .coerceIn(0f, if (phase == InstallPhase.WaitingForBootAllocator) 1f else 0.99f)
}
