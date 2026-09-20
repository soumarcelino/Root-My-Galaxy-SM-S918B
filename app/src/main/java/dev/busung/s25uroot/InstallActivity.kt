package dev.busung.s25uroot

import android.os.Build
import android.os.Bundle
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
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Error
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Security
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.busung.s25uroot.ui.theme.RootMyGalaxyTheme
import kotlinx.coroutines.delay

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
    val logScrollState = rememberScrollState()
    val view = LocalView.current
    LaunchedEffect(installState.log) {
        logScrollState.scrollTo(logScrollState.maxValue)
    }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Column(
                modifier = Modifier.padding(top = 28.dp, bottom = 4.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text(
                    text = stringResource(R.string.install_title),
                    style = MaterialTheme.typography.headlineLarge,
                )
                Text(
                    text = if (installState.busy) {
                        stringResource(R.string.install_keep_open)
                    } else {
                        installState.message
                    },
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            InstallerStatusCard(installState, uninstallRoot)
            ExecutionJourneyCard(installState)
            InstallerLog(
                output = installState.log,
                modifier = Modifier.weight(1f),
                scrollState = logScrollState,
            )

            if (!installState.busy) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(bottom = 20.dp),
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
private fun InstallerStatusCard(installState: InstallUiState, uninstallRoot: Boolean) {
    val progress = if (installState.phase == InstallPhase.Installed) 1f else {
        (installState.executionStage.ordinal + 1f) / ExecutionStage.entries.size
    }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .animateContentSize(),
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.cardColors(
            containerColor = when (installState.phase) {
                InstallPhase.Failed -> MaterialTheme.colorScheme.errorContainer
                else -> MaterialTheme.colorScheme.primaryContainer
            },
            contentColor = if (installState.phase == InstallPhase.Failed) {
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
                AnimatedContent(targetState = installState.phase, label = "install-status-icon") { phase ->
                    when {
                        installState.busy -> LoadingIndicator(
                            modifier = Modifier.size(44.dp),
                            color = MaterialTheme.colorScheme.onPrimaryContainer,
                        )
                        phase == InstallPhase.Installed -> Icon(
                            Icons.Rounded.Check,
                            contentDescription = null,
                            modifier = Modifier.size(44.dp),
                        )
                        else -> Icon(
                            Icons.Rounded.Error,
                            contentDescription = null,
                            modifier = Modifier.size(44.dp),
                        )
                    }
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = installState.message,
                        style = MaterialTheme.typography.titleLarge,
                    )
                    Text(
                        text = installPhaseDetail(installState.phase, uninstallRoot),
                        color = LocalContentColor.current.copy(alpha = 0.78f),
                    )
                }
            }
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier.fillMaxWidth(),
                color = LocalContentColor.current,
                trackColor = LocalContentColor.current.copy(alpha = 0.2f),
                drawStopIndicator = {},
            )
        }
    }
}

@Composable
private fun ExecutionJourneyCard(installState: InstallUiState) {
    val stage = installState.executionStage
    val stageNumber = stage.ordinal + 1
    val progress = stageNumber.toFloat() / ExecutionStage.entries.size
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainerHighest,
        ),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                Text(text = executionStageIcon(stage), fontSize = 30.sp)
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = stringResource(R.string.execution_stage_counter, stageNumber),
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.primary,
                    )
                    Text(
                        text = executionStageTitle(stage),
                        style = MaterialTheme.typography.titleMedium,
                    )
                    Text(
                        text = installState.executionDetail ?: executionStageDetail(stage),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier.fillMaxWidth(),
                drawStopIndicator = {},
            )
            Surface(
                shape = MaterialTheme.shapes.small,
                color = if (installState.rootActive) {
                    MaterialTheme.colorScheme.tertiaryContainer
                } else {
                    MaterialTheme.colorScheme.surfaceContainer
                },
            ) {
                Text(
                    text = if (installState.rootActive) {
                        stringResource(R.string.root_live_active)
                    } else {
                        stringResource(R.string.root_live_waiting)
                    },
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                    color = if (installState.rootActive) {
                        MaterialTheme.colorScheme.onTertiaryContainer
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
                    style = MaterialTheme.typography.labelLarge,
                )
            }
        }
    }
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
private fun InstallerLog(
    output: String,
    modifier: Modifier,
    scrollState: androidx.compose.foundation.ScrollState,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.large,
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(stringResource(R.string.install_live_progress), style = MaterialTheme.typography.titleMedium)
            Text(
                text = output.ifBlank { stringResource(R.string.install_preparing) },
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .verticalScroll(scrollState),
                fontFamily = FontFamily.Monospace,
                fontSize = 12.sp,
                lineHeight = 18.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun installPhaseDetail(phase: InstallPhase, uninstallRoot: Boolean): String = stringResource(
    when (phase) {
        InstallPhase.Checking -> R.string.phase_checking
        InstallPhase.Ready -> R.string.phase_ready
        InstallPhase.Downloading -> R.string.phase_downloading
        InstallPhase.Exploiting -> R.string.phase_exploiting
        InstallPhase.LoadingKernelSu -> if (uninstallRoot) R.string.phase_uninstalling_root else R.string.phase_loading_ksu
        InstallPhase.Installed -> if (uninstallRoot) R.string.phase_root_uninstalled else R.string.phase_installed
        InstallPhase.Failed -> R.string.phase_failed
    },
)

internal fun bootWindowProgress(uptimeMillis: Long, windowMillis: Long): Float {
    val safeWindowMillis = windowMillis.coerceAtLeast(1L)
    return (uptimeMillis.coerceAtLeast(0L) % safeWindowMillis).toFloat() / safeWindowMillis
}
