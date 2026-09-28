package dev.busung.s25uroot

import android.content.Context
import android.os.Build
import android.os.Bundle
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
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Error
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.Handshake
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Security
import androidx.compose.material3.AlertDialog
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.busung.s25uroot.ui.theme.ColorScheme_success
import dev.busung.s25uroot.ui.theme.RootMyGalaxyTheme

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
                    onReboot = installViewModel::rebootDevice,
                    onClose = ::finish,
                )
                installState.shizukuConnectionAlert?.let { message ->
                    AlertDialog(
                        onDismissRequest = installViewModel::dismissShizukuConnectionAlert,
                        title = { Text(stringResource(R.string.shizuku_connection_failed_title)) },
                        text = { Text(message) },
                        confirmButton = {
                            Button(onClick = installViewModel::dismissShizukuConnectionAlert) {
                                Text(stringResource(R.string.action_close))
                            }
                        },
                    )
                }
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

internal enum class VisualStage {
    Preparation,
    Stability,
    Root,
    Finishing,
}

internal fun ExecutionStage.visualStage(): VisualStage = when (this) {
    ExecutionStage.CheckingShizuku,
    ExecutionStage.WaitingForUptime,
    ExecutionStage.Preparing,
    -> VisualStage.Preparation
    ExecutionStage.Stabilizing -> VisualStage.Stability
    ExecutionStage.StartingExploit,
    ExecutionStage.LocatingKernel,
    ExecutionStage.VerifyingKernelAccess,
    ExecutionStage.StartingTemporaryRoot,
    ExecutionStage.BuildingPipeBridge,
    -> VisualStage.Root
    ExecutionStage.LoadingKernelSu,
    ExecutionStage.VerifyingRoot,
    -> VisualStage.Finishing
}

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
    onReboot: () -> Unit,
    onClose: () -> Unit,
) {
    val view = LocalView.current
    val context = LocalContext.current
    val scrollState = rememberScrollState()

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
            }
            Column(
                modifier = Modifier
                    .weight(1f)
                    .verticalScroll(scrollState),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                if (installState.phase == InstallPhase.Installed) {
                    SuccessCard(installState, uninstallRoot)
                } else {
                    StatusProgressCard(installState, uninstallRoot)
                }
                installState.supportHelp?.let { help ->
                    FirmwarePortHelpCard(help)
                }
                if (installState.rebootRequired) {
                    FatalRebootCard(installState)
                }
                GroupedProgress(installState)
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
                                if (installState.rebootRequired) onReboot() else onRetry()
                            },
                            enabled = !installState.rebootInProgress,
                            modifier = Modifier.weight(1f),
                        ) {
                            Text(
                                stringResource(
                                    if (installState.rebootRequired) R.string.action_reboot
                                    else R.string.action_retry,
                                ),
                            )
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
private fun FirmwarePortHelpCard(message: String) {
    val context = LocalContext.current
    val success = ColorScheme_success
    Card(
        onClick = { openProjectIssues(context) },
        modifier = Modifier.fillMaxWidth().animateContentSize(),
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(
            containerColor = success.container,
            contentColor = success.onContainer,
        ),
    ) {
        Row(
            modifier = Modifier.padding(18.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Icon(
                Icons.Rounded.Handshake,
                contentDescription = null,
                modifier = Modifier.size(44.dp),
            )
            Text(
                text = message,
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}

@Composable
private fun FatalRebootCard(installState: InstallUiState) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.errorContainer,
            contentColor = MaterialTheme.colorScheme.onErrorContainer,
        ),
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = stringResource(R.string.fatal_reboot_title),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = stringResource(R.string.fatal_reboot_message),
                style = MaterialTheme.typography.bodyMedium,
            )
            if (installState.rebootInProgress) {
                Text(
                    text = stringResource(R.string.status_rebooting),
                    style = MaterialTheme.typography.labelLarge,
                )
            }
            installState.rebootError?.let { error ->
                Text(
                    text = error,
                    style = MaterialTheme.typography.bodySmall,
                    fontWeight = FontWeight.SemiBold,
                )
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
    val context = LocalContext.current
    val failed = installState.phase == InstallPhase.Failed
    val waitingForUptime = installState.phase == InstallPhase.WaitingForUptime
    val requestedProgress = if (waitingForUptime || installState.phase == InstallPhase.Checking ||
        installState.phase == InstallPhase.Downloading
    ) null else executionJourneyProgress(installState.executionStage, installState.stabilizationMetrics)
    var highestProgress by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(installState.phase, requestedProgress) {
        if (installState.phase == InstallPhase.CheckingShizuku ||
            installState.phase == InstallPhase.Checking
        ) {
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
            .animateContentSize()
            .clickable(enabled = installState.supportHelp != null) {
                openProjectIssues(context)
            },
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
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                AnimatedContent(targetState = installState.busy, label = "install-status-icon") { busy ->
                    if (busy) {
                        LoadingIndicator(
                            modifier = Modifier.size(34.dp),
                            color = MaterialTheme.colorScheme.onPrimaryContainer,
                        )
                    } else {
                        Icon(
                            Icons.Rounded.Error,
                            contentDescription = null,
                            modifier = Modifier.size(34.dp),
                        )
                    }
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = if (installState.busy) visualStageTitle(installState.executionStage.visualStage())
                            else installState.message,
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        text = if (installState.busy) {
                            groupedExecutionDetail(installState)
                        } else if (failed && installState.failureDetail != null) {
                            installState.failureDetail
                        } else {
                            installPhaseDetail(installState, uninstallRoot)
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = LocalContentColor.current.copy(alpha = 0.78f),
                    )
                }
            }
            if (!failed && waitingForUptime) {
                UptimeGatePanel(
                    remainingMillis = installState.uptimeGateRemainingMillis
                        ?: MINIMUM_PAYLOAD_UPTIME_MILLIS,
                    totalMillis = installState.uptimeGateTotalMillis
                        ?: MINIMUM_PAYLOAD_UPTIME_MILLIS,
                )
            }
            if (!failed && !waitingForUptime) {
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
private fun UptimeGatePanel(remainingMillis: Long, totalMillis: Long) {
    val requestedProgress = uptimeGateProgress(remainingMillis, totalMillis)
    val animatedProgress by animateFloatAsState(
        targetValue = requestedProgress,
        animationSpec = tween(250, easing = LinearEasing),
        label = "uptime-gate-progress",
    )
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = stringResource(R.string.uptime_gate_minimum),
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = formatCountdown(remainingMillis),
                style = MaterialTheme.typography.titleMedium,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.SemiBold,
            )
        }
        LinearWavyProgressIndicator(
            progress = { animatedProgress },
            modifier = Modifier.fillMaxWidth(),
            color = LocalContentColor.current,
            trackColor = LocalContentColor.current.copy(alpha = 0.2f),
        )
    }
}

private fun formatElapsedTime(millis: Long): String {
    val seconds = millis.coerceAtLeast(0L) / 1_000L
    val hours = seconds / 3_600L
    val minutes = (seconds % 3_600L) / 60L
    val remainingSeconds = seconds % 60L
    return if (hours > 0) "%d:%02d:%02d".format(hours, minutes, remainingSeconds)
        else "%d:%02d".format(minutes, remainingSeconds)
}

private fun formatCountdown(millis: Long): String {
    val seconds = (millis.coerceAtLeast(0L) + 999L) / 1_000L
    return "%02d:%02d".format(seconds / 60L, seconds % 60L)
}

@Composable
private fun visualStageTitle(stage: VisualStage): String = stringResource(
    when (stage) {
        VisualStage.Preparation -> R.string.visual_stage_preparation
        VisualStage.Stability -> R.string.visual_stage_stability
        VisualStage.Root -> R.string.visual_stage_root
        VisualStage.Finishing -> R.string.visual_stage_finishing
    },
)

@Composable
private fun groupedExecutionDetail(installState: InstallUiState): String = when {
    installState.phase == InstallPhase.CheckingShizuku -> installState.message
    installState.phase == InstallPhase.WaitingForUptime -> installState.message
    installState.executionStage.visualStage() == VisualStage.Preparation ->
        stringResource(R.string.visual_detail_preparation)
    installState.executionStage.visualStage() == VisualStage.Stability -> {
        installState.stabilizationMetrics?.let { metrics ->
            stringResource(
                R.string.visual_detail_stability_progress,
                metrics.sample,
                metrics.requiredSamples,
            )
        } ?: stringResource(R.string.visual_detail_stability)
    }
    installState.executionStage.visualStage() == VisualStage.Root ->
        stringResource(R.string.visual_detail_root)
    else -> stringResource(R.string.visual_detail_finishing)
}

@Composable
private fun GroupedProgress(installState: InstallUiState) {
    val stages = VisualStage.entries
    val current = installState.executionStage.visualStage()
    val currentIndex = current.ordinal
    val installed = installState.phase == InstallPhase.Installed
    val failed = installState.phase == InstallPhase.Failed
    val success = ColorScheme_success
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
        ),
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                text = if (installed) {
                    stringResource(R.string.visual_progress_complete)
                } else {
                    stringResource(
                        R.string.visual_progress_stage,
                        currentIndex + 1,
                        stages.size,
                        visualStageTitle(current),
                    )
                },
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                stages.forEachIndexed { index, _ ->
                    val color = when {
                        installed || index < currentIndex -> success.color
                        failed && index == currentIndex -> MaterialTheme.colorScheme.error
                        index == currentIndex -> MaterialTheme.colorScheme.primary
                        else -> MaterialTheme.colorScheme.outlineVariant
                    }
                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .height(5.dp)
                            .clip(CircleShape)
                            .background(color),
                    )
                }
            }
        }
    }
}

@Composable
private fun InstallerLog(installState: InstallUiState) {
    val view = LocalView.current
    var expanded by remember { mutableStateOf(true) }
    LaunchedEffect(installState.phase) {
        if (installState.phase == InstallPhase.Failed) expanded = true
    }
    val logScrollState = rememberScrollState()
    LaunchedEffect(installState.log, expanded) {
        if (expanded) logScrollState.scrollTo(logScrollState.maxValue)
    }
    val chevronRotation by animateFloatAsState(if (expanded) 180f else 0f, label = "log-chevron")
    val displayLog = remember(installState.log) {
        formatLogForDisplay(installState.log)
    }

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
                    text = displayLog.ifBlank { stringResource(R.string.install_preparing) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 16.dp, end = 16.dp, bottom = 16.dp)
                        .heightIn(max = 240.dp)
                        .verticalScroll(logScrollState),
                    fontFamily = FontFamily.Monospace,
                    fontSize = 12.sp,
                    lineHeight = 18.sp,
                    softWrap = true,
                    overflow = TextOverflow.Clip,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.85f),
                )
            }
        }
    }
}

@Composable
private fun installPhaseDetail(installState: InstallUiState, uninstallRoot: Boolean): String {
    if (installState.phase == InstallPhase.WaitingForUptime) {
        val remainingMillis = installState.uptimeGateRemainingMillis ?: 0L
        val seconds = remainingMillis / 1_000L + if (remainingMillis % 1_000L > 0L) 1L else 0L
        return stringResource(R.string.status_waiting_uptime, seconds)
    }
    if (installState.phase == InstallPhase.WaitingForBootAllocator) {
        val remainingMillis = installState.bootAllocatorRemainingMillis ?: 0L
        val seconds = remainingMillis / 1_000L + if (remainingMillis % 1_000L > 0L) 1L else 0L
        return stringResource(R.string.status_waiting_boot_allocator, seconds)
    }
    return stringResource(when (installState.phase) {
        InstallPhase.CheckingShizuku -> R.string.phase_checking_shizuku
        InstallPhase.Checking -> R.string.phase_checking
        InstallPhase.Ready -> R.string.phase_ready
        InstallPhase.WaitingForUptime -> R.string.phase_waiting_uptime
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
        InstallPhase.CheckingShizuku -> ShizukuController.SHIZUKU_CONNECTION_TIMEOUT_MILLIS
        InstallPhase.Checking -> 8_000L
        InstallPhase.Ready -> 1L
        InstallPhase.WaitingForUptime -> MINIMUM_PAYLOAD_UPTIME_MILLIS
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
