package dev.busung.s25uroot

import android.app.Application
import android.os.SystemClock
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.currentCoroutineContext
import java.io.File
import java.io.InputStream
import kotlin.time.Duration.Companion.milliseconds

enum class InstallPhase {
    Checking,
    Ready,
    Downloading,
    Exploiting,
    LoadingKernelSu,
    Installed,
    Failed,
}

data class InstallUiState(
    val phase: InstallPhase = InstallPhase.Checking,
    val message: String = "",
    val probeOutput: String = "",
    val log: String = "",
    val executionStage: ExecutionStage = ExecutionStage.Preparing,
    val executionDetail: String? = null,
    val rootActive: Boolean = false,
) {
    val busy: Boolean
        get() = phase in setOf(
            InstallPhase.Checking,
            InstallPhase.Downloading,
            InstallPhase.Exploiting,
            InstallPhase.LoadingKernelSu,
        )

}

data class TargetCatalogUiState(
    val loading: Boolean = false,
    val profiles: List<TargetProfile> = emptyList(),
    val error: String? = null,
)

private data class CommandResult(val code: Int, val output: String)

class InstallViewModel(application: Application) : AndroidViewModel(application) {
    private val app = application
    private val repository = PayloadRepository(application)
    private val historyStore = InstallHistoryStore(application)
    private val mutableState = MutableStateFlow(InstallUiState())
    private val mutableHistory = MutableStateFlow(historyStore.closeInterruptedRuns())
    private val mutableTargetCatalog = MutableStateFlow(TargetCatalogUiState())
    private var discoveryJob: Job? = null
    private var installJob: Job? = null
    private var activeHistoryEntry: InstallHistoryEntry? = null
    private var fullRunLog = ""
    val state: StateFlow<InstallUiState> = mutableState.asStateFlow()
    val history: StateFlow<List<InstallHistoryEntry>> = mutableHistory.asStateFlow()
    val targetCatalog: StateFlow<TargetCatalogUiState> = mutableTargetCatalog.asStateFlow()

    init {
        refresh()
    }

    fun refresh() {
        if (installJob?.isActive == true) return
        mutableHistory.value = historyStore.load()
        discoveryJob?.cancel()
        discoveryJob = viewModelScope.launch(Dispatchers.IO) {
            val probe = NativeProbe.run()
            if (detectInstalled()) {
                mutableState.value = InstallUiState(
                    phase = InstallPhase.Installed,
                    message = app.getString(R.string.status_ksu_active),
                    probeOutput = probe,
                    log = probe,
                    executionStage = ExecutionStage.VerifyingRoot,
                    rootActive = true,
                )
                return@launch
            }
            try {
                val profile = repository.resolveTarget(DeviceSnapshot.current())
                mutableState.value = InstallUiState(
                    phase = InstallPhase.Ready,
                    message = app.getString(R.string.status_not_installed),
                    probeOutput = probe,
                    log = "$probe\n${app.getString(R.string.log_profile, profile.profileId)}",
                )
            } catch (error: Throwable) {
                mutableState.value = InstallUiState(
                    phase = InstallPhase.Failed,
                    message = app.getString(R.string.status_support_failed),
                    probeOutput = probe,
                    log = "$probe\n[-] ${error.message ?: error.javaClass.simpleName}",
                )
            }
        }
    }

    fun deleteHistoryEntries(ids: Collection<String>) {
        val runningId = activeHistoryEntry?.id
        val toDelete = ids.filterNot { it == runningId }
        if (toDelete.isEmpty()) return
        toDelete.forEach(historyStore::delete)
        mutableHistory.value = mutableHistory.value.filterNot { it.id in toDelete }
    }

    fun loadTargetCatalog() {
        if (mutableTargetCatalog.value.loading) return
        viewModelScope.launch(Dispatchers.IO) {
            mutableTargetCatalog.value = TargetCatalogUiState(loading = true)
            mutableTargetCatalog.value = try {
                TargetCatalogUiState(
                    profiles = repository.loadTargets().sortedWith(
                        compareByDescending<TargetProfile> { it.specificity }
                            .thenBy(TargetProfile::displayName)
                            .thenBy(TargetProfile::profileId),
                    ),
                )
            } catch (error: Throwable) {
                TargetCatalogUiState(error = error.message ?: error.javaClass.simpleName)
            }
        }
    }

    fun install(profileId: String? = null) {
        if (installJob?.isActive == true || mutableState.value.phase == InstallPhase.Installed) return
        discoveryJob?.cancel()
        installJob = viewModelScope.launch(Dispatchers.IO) {
            mutableState.value = InstallUiState(
                phase = InstallPhase.Checking,
                probeOutput = mutableState.value.probeOutput,
            )
            startHistory()
            try {
                if (shizukuEnabled()) {
                    appendLog(app.getString(R.string.log_shizuku_prepare))
                    if (!ShizukuController.isRunning() && !ShizukuController.pingUntilRunning()) {
                        error(app.getString(R.string.error_shizuku_unavailable))
                    }
                    if (!ShizukuController.isGranted() && !ShizukuController.requestPermission()) {
                        error(app.getString(R.string.error_shizuku_permission))
                    }
                    appendLog(app.getString(R.string.log_shizuku_permission))
                }
                setPhase(InstallPhase.Checking, app.getString(R.string.status_checking_github))
                val profile = if (profileId == null) {
                    repository.resolveTarget(DeviceSnapshot.current())
                } else {
                    repository.resolveTarget(profileId)
                }
                appendLog(app.getString(R.string.log_profile, profile.profileId))
                updateHistoryProfile(profile.profileId)

                setPhase(InstallPhase.Downloading, app.getString(R.string.status_downloading_payload))
                val payloads = repository.download(profile) { appendLog("[*] $it") }
                appendLog(app.getString(R.string.log_download_verified))

                setPhase(InstallPhase.Exploiting, app.getString(R.string.status_exploit_running))
                executeExploit(payloads)

                setPhase(InstallPhase.LoadingKernelSu, app.getString(R.string.status_ksu_loading))
                installKernelSu(payloads)

                setPhase(InstallPhase.Installed, app.getString(R.string.status_ksu_active))
                appendLog(app.getString(R.string.log_install_complete))
                finishHistory(InstallRunResult.Succeeded)
            } catch (error: Throwable) {
                appendLog("[-] ${error.message ?: error.javaClass.simpleName}")
                setPhase(InstallPhase.Failed, app.getString(R.string.status_install_failed))
                finishHistory(InstallRunResult.Failed)
            }
        }
    }

    fun uninstallRoot(profileId: String? = null) {
        if (installJob?.isActive == true) return
        discoveryJob?.cancel()
        installJob = viewModelScope.launch(Dispatchers.IO) {
            mutableState.value = InstallUiState(
                phase = InstallPhase.Checking,
                probeOutput = mutableState.value.probeOutput,
            )
            startHistory()
            try {
                if (shizukuEnabled()) {
                    appendLog(app.getString(R.string.log_shizuku_prepare))
                    if (!ShizukuController.isRunning() && !ShizukuController.pingUntilRunning()) {
                        error(app.getString(R.string.error_shizuku_unavailable))
                    }
                    if (!ShizukuController.isGranted() && !ShizukuController.requestPermission()) {
                        error(app.getString(R.string.error_shizuku_permission))
                    }
                    appendLog(app.getString(R.string.log_shizuku_permission))
                }
                setPhase(InstallPhase.Checking, app.getString(R.string.status_checking_github))
                val profile = if (profileId == null) {
                    repository.resolveTarget(DeviceSnapshot.current())
                } else {
                    repository.resolveTarget(profileId)
                }
                appendLog(app.getString(R.string.log_profile, profile.profileId))
                updateHistoryProfile(profile.profileId)
                setPhase(InstallPhase.Downloading, app.getString(R.string.status_downloading_payload))
                val payloads = repository.download(profile) { appendLog("[*] $it") }
                appendLog(app.getString(R.string.log_download_verified))
                setPhase(InstallPhase.Exploiting, app.getString(R.string.status_exploit_running))
                executeExploit(payloads)
                setPhase(InstallPhase.LoadingKernelSu, app.getString(R.string.status_uninstalling_root))
                removeRootFiles(payloads.helper)
                setPhase(InstallPhase.Installed, app.getString(R.string.status_root_uninstalled))
                finishHistory(InstallRunResult.Succeeded)
            } catch (error: Throwable) {
                appendLog("[-] ${error.message ?: error.javaClass.simpleName}")
                setPhase(InstallPhase.Failed, app.getString(R.string.status_install_failed))
                finishHistory(InstallRunResult.Failed)
            }
        }
    }

    private suspend fun executeExploit(payloads: VerifiedPayloads) {
        val payload = payloads.exploit
        val launcher = payloads.launcher
        val shizuku = shizukuEnabled()
        appendLog("[diag] shizukuEnabled=$shizuku isRunning=${ShizukuController.isRunning()} isGranted=${ShizukuController.isGranted()}")
        // v0.2.34: pstore dump —— 重启后读上次内核崩溃日志（KDP/DEFEX/RKP 拦截铁证）
        if (shizuku) dumpPstore()
        val helper = helperFile(payloads.helper)
        appendLog("[diag] helper=${helper.absolutePath} launcher=${launcher.absolutePath}")
        if (!shizuku) {
            require(helper.canExecute()) { app.getString(R.string.error_helper_unavailable) }
            require(launcher.canExecute()) { app.getString(R.string.error_launcher_unavailable) }
        }
        val logPrefix = mutableState.value.log
        val bootToken = currentBootToken()
        val process = if (shizuku) {
            val stagedPayload = shizukuStage(payload, SHIZUKU_PAYLOAD_PATH, "755")
            val stagedLauncher = shizukuStage(launcher, SHIZUKU_LAUNCHER_PATH, "755")
            appendLog(
                "[diag] Shizuku branch: launcher=${stagedLauncher.absolutePath} " +
                    "payload=${stagedPayload.absolutePath}",
            )
            val launcherEnv = buildList {
                cachedP0Offset(bootToken)?.let { add("$P0_OFFSET_ENV=$it") }
            }.toTypedArray()
            val command = "exec ${shellQuote(stagedLauncher.absolutePath)} " +
                "--payload ${shellQuote(stagedPayload.absolutePath)} " +
                "--helper ${shellQuote(helper.absolutePath)} 2>&1"
            ShizukuController.exec(
                arrayOf("/system/bin/sh", "-c", command),
                launcherEnv.takeIf { it.isNotEmpty() },
            )
        } else {
            appendLog("[diag] App branch: launcher=${launcher.absolutePath} payload=${payload.absolutePath}")
            val processBuilder = ProcessBuilder(
                launcher.absolutePath,
                "--payload",
                payload.absolutePath,
                "--helper",
                helper.absolutePath,
            ).redirectErrorStream(true)
            processBuilder.environment().apply {
                cachedP0Offset(bootToken)?.let { put(P0_OFFSET_ENV, it) }
            }
            processBuilder.start()
        }
        val captured = StringBuilder()

        try {
            val startedAt = SystemClock.elapsedRealtime()
            var lastProgressAt = startedAt
            var lastUiPublishAt = startedAt
            var lastHistorySaveAt = startedAt
            while (process.isAlive) {
                val chunk = drainProcessOutput(process)
                if (chunk.isNotEmpty()) {
                    captured.append(chunk)
                    cacheP0Offset(bootToken, chunk)
                    lastProgressAt = SystemClock.elapsedRealtime()
                }
                val now = SystemClock.elapsedRealtime()
                if (chunk.isNotEmpty() && now - lastUiPublishAt >= UI_PUBLISH_MILLIS) {
                    val persist = now - lastHistorySaveAt >= HISTORY_SAVE_MILLIS
                    publishExploitLog(logPrefix, captured.toString(), persist)
                    lastUiPublishAt = now
                    if (persist) lastHistorySaveAt = now
                }
                require(now - lastProgressAt < EXPLOIT_STALL_MILLIS) {
                    app.getString(R.string.error_exploit_stalled)
                }
                require(now - startedAt < EXPLOIT_TOTAL_MILLIS) {
                    app.getString(R.string.error_exploit_timeout)
                }
                delay(LOG_POLL_INTERVAL)
            }

            val exitCode = process.waitFor()
            captured.append(drainProcessOutput(process))
            val rawLog = captured.toString()
            cacheP0Offset(bootToken, rawLog)
            publishExploitLog(logPrefix, rawLog, persist = true)
            val earlyOutput = rawLog.trim()
            require(exitCode == 0) {
                app.getString(
                    R.string.error_payload_exit,
                    exitCode,
                    earlyOutput.takeIf(String::isNotBlank)?.let { " ($it)" } ?: "",
                )
            }
            // v0.2.26+: 新架构成功标记是 stage=temporary-root-ready（老架构是 exploit completed done=1 root=1）
            val newArchOk = rawLog.contains("temporary-root-ready")
            val oldArchOk = rawLog.contains("exploit completed") && rawLog.contains("done=1 root=1")
            require(newArchOk || oldArchOk) {
                app.getString(R.string.error_success_marker)
            }
            val rootDurationMillis = SystemClock.elapsedRealtime() - startedAt
            AppPreferences.setLastRootDurationMillis(app, rootDurationMillis)
            appendLog("[+] Root acquired in ${rootDurationMillis / 1_000.0} seconds")
        } finally {
            // Closing the UI must not abort a device-side launcher in a critical section.
            if (process.isAlive && currentCoroutineContext().isActive) {
                process.destroy()
                delay(500.milliseconds)
                if (process.isAlive) process.destroyForcibly()
            }
        }
        appendLog(app.getString(R.string.log_bootstrap_root))
    }

    private fun drainProcessOutput(process: Process): String {
        val buffer = StringBuilder()
        return try {
            drainStream(process.inputStream, buffer)
            drainStream(process.errorStream, buffer)
            buffer.toString()
        } catch (_: Throwable) {
            buffer.toString()
        }
    }

    private fun drainStream(stream: InputStream, buffer: StringBuilder) {
        val data = ByteArray(4096)
        while (stream.available() > 0) {
            val count = stream.read(data)
            if (count <= 0) break
            buffer.append(String(data, 0, count, Charsets.UTF_8))
        }
    }

    private fun publishExploitLog(prefix: String, rawLog: String, persist: Boolean) {
        val progress = parseExecutionProgress(rawLog, mutableState.value.executionStage)
        val completeLog = listOf(prefix, stripAnsi(rawLog))
            .filter(String::isNotBlank)
            .joinToString("\n")
        fullRunLog = completeLog
        mutableState.value = mutableState.value.copy(
            log = logTail(completeLog),
            executionStage = progress.stage,
            executionDetail = progress.detail ?: mutableState.value.executionDetail,
        )
        if (persist) updateHistoryLog()
    }

    private fun installKernelSu(payloads: VerifiedPayloads) {
        val helper = helperFile(payloads.helper)
        if (shizukuEnabled()) {
            // v0.2.26+: helper 硬编码 ksud 路径 /data/local/tmp/ksud-selected（F7310 版 helper）
            shizukuStage(payloads.kernelSu, "/data/local/tmp/ksud-selected", "755")
            shizukuStage(payloads.kernelSu, SHIZUKU_KSUD_STAGE_PATH, "755")
            appendLog(app.getString(R.string.log_ksu_staged))
        } else {
            val source = shellQuote(payloads.kernelSu.absolutePath)
            val stageCommand =
                "/system/bin/cp $source /data/local/tmp/ksud-s25u-kdp && " +
                    "/system/bin/cp $source /data/local/tmp/.ksud-stage && " +
                    "/system/bin/chmod 755 /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage"
            val stage = runHelper(helper, "-c", stageCommand)
            require(stage.code == 0) { app.getString(R.string.error_ksu_stage, stage.output) }
            appendLog(app.getString(R.string.log_ksu_staged))
        }

        val lateLoad = runHelper(helper, "--late-load")
        require(lateLoad.code == 0) {
            app.getString(R.string.error_ksu_verify, lateLoad.code, lateLoad.output)
        }
        if (lateLoad.output.isNotBlank()) appendLog(lateLoad.output)
        require(NativeProbe.isKernelSuActive()) {
            app.getString(R.string.error_ksu_control_inactive)
        }
        storeInstallReceipt()
        mutableState.value = mutableState.value.copy(
            executionStage = ExecutionStage.VerifyingRoot,
            rootActive = true,
        )
        appendLog(app.getString(R.string.log_ksu_control_verified))
    }

    private fun removeRootFiles(helperSource: File) {
        val helper = helperFile(helperSource)
        val cleanup =
            "for target in /data/local/tmp /data/adb/modules; do " +
                "[ -d \"\$target\" ] || continue; " +
                "find \"\$target\" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +; " +
                "done; rm -f -- /data/adb/ksud"
        val result = runHelper(helper, "-c", cleanup)
        require(result.code == 0) {
            app.getString(R.string.error_uninstall_root, result.code, result.output)
        }
    }

    private fun detectInstalled(): Boolean {
        if (NativeProbe.isKernelSuActive()) return true
        val bootToken = currentBootToken() ?: return false
        val receipt = app.getSharedPreferences(INSTALL_RECEIPT, Application.MODE_PRIVATE)
        return receipt.getString(RECEIPT_BOOT_TOKEN, null) == bootToken &&
            receipt.getBoolean(RECEIPT_VERIFIED, false)
    }

    private fun storeInstallReceipt() {
        val bootToken = currentBootToken() ?: error(app.getString(R.string.error_boot_id))
        val stored = app.getSharedPreferences(INSTALL_RECEIPT, Application.MODE_PRIVATE)
            .edit()
            .putString(RECEIPT_BOOT_TOKEN, bootToken)
            .putBoolean(RECEIPT_VERIFIED, true)
            .commit()
        require(stored) { app.getString(R.string.error_receipt) }
    }

    private fun currentBootToken(): String? = runCatching {
        File("/proc/sys/kernel/random/boot_id")
            .readText(Charsets.US_ASCII)
            .trim()
            .takeIf(String::isNotBlank)
    }.getOrNull()

    private fun cachedP0Offset(bootToken: String?): String? {
        if (bootToken == null) return null
        val stored = app.getSharedPreferences(P0_CACHE, Application.MODE_PRIVATE)
        if (stored.getString(P0_CACHE_BOOT_TOKEN, null) != bootToken) return null
        return stored.getString(P0_CACHE_OFFSET, null)
    }

    private fun cacheP0Offset(bootToken: String?, log: String) {
        if (bootToken == null) return
        val match = P0_OFFSET_PATTERN.findAll(log).lastOrNull() ?: return
        val offset = match.groupValues[1].toLongOrNull(16) ?: return
        if (offset !in 0..P0_OFFSET_MAX || offset and P0_OFFSET_MASK != 0L) return
        val value = "0x${offset.toString(16)}"
        val stored = app.getSharedPreferences(P0_CACHE, Application.MODE_PRIVATE)
        if (stored.getString(P0_CACHE_BOOT_TOKEN, null) == bootToken &&
            stored.getString(P0_CACHE_OFFSET, null) == value
        ) return
        stored.edit()
            .putString(P0_CACHE_BOOT_TOKEN, bootToken)
            .putString(P0_CACHE_OFFSET, value)
            .apply()
    }

    /**
     * v0.2.34: dump 上次内核崩溃日志（pstore）——exploit 重启后 KDP/DEFEX/RKP 是否拦截的铁证。
     * 三星 pstore 在 /sys/fs/pstore/ 保存 last_kmsg/dmesg；shell 域可读。
     */
    private fun dumpPstore() {
        try {
            appendLog("--- [pstore] dump start ---")
            val out = ShizukuController.capture(arrayOf("sh", "-c",
                "for f in /sys/fs/pstore/*; do echo \"===== ${'$'}f =====\"; head -c 8192 \"${'$'}f\" 2>/dev/null; echo; done; ls -la /sys/fs/pstore/ 2>/dev/null"))
            if (out.isNotBlank()) appendLog(out) else appendLog("--- [pstore] empty ---")
            appendLog("--- [pstore] dump end ---")
        } catch (t: Throwable) {
            appendLog("--- [pstore] error: ${t.message} ---")
        }
    }

    private fun helperFile(source: File): File =
        if (shizukuEnabled()) {
            shizukuStage(source, SHIZUKU_HELPER_PATH, "755")
        } else {
            source
        }

    private fun shizukuEnabled(): Boolean = AppPreferences.shizukuMode(app)

    private fun shizukuStage(source: File, target: String, mode: String): File {
        val staged = File(target)
        // v0.2.29+: 不再复用旧文件（长度相同但内容可能损坏——bad ELF magic bug）。
        // 总是先删除再写入，确保 /data/local/tmp 下是完整的新 payload。
        try {
            ShizukuController.exec(arrayOf("rm", "-f", target)).waitFor()
            ShizukuController.writeFile(target, mode, source.inputStream())
            // 写后验证：读回前 4 字节必须是 ELF magic (0x7f 'E' 'L' 'F')
            val magic = ShizukuController.capture(arrayOf("sh", "-c", "head -c 4 '$target' | od -An -tx1 | tr -d ' \\n'"))
            check(magic.contains("7f454c46")) {
                "staged $target has bad magic: $magic"
            }
        } catch (error: Throwable) {
            throw IllegalStateException(
                app.getString(R.string.error_shizuku_stage, target, error.message.orEmpty()),
                error,
            )
        }
        return staged
    }

    private fun readProcessOutput(process: Process, shizuku: Boolean): String {
        val stdout = process.inputStream.bufferedReader().use { it.readText() }
        val stderr = if (shizuku) process.errorStream.bufferedReader().use { it.readText() } else ""
        return stdout + stderr
    }

    private fun runHelper(helper: File, vararg arguments: String): CommandResult {
        val process = if (shizukuEnabled()) {
            ShizukuController.exec(arrayOf(helper.absolutePath) + arguments)
        } else {
            ProcessBuilder(listOf(helper.absolutePath) + arguments)
                .redirectErrorStream(true)
                .start()
        }
        val output = readProcessOutput(process, shizukuEnabled())
        return CommandResult(process.waitFor(), stripAnsi(output.trim()))
    }

    private fun shellQuote(value: String) = "'${value.replace("'", "'\\''")}'"

    private fun setPhase(phase: InstallPhase, message: String) {
        val stage = when (phase) {
            InstallPhase.Checking, InstallPhase.Ready, InstallPhase.Downloading ->
                ExecutionStage.Preparing
            InstallPhase.Exploiting -> mutableState.value.executionStage
            InstallPhase.LoadingKernelSu -> ExecutionStage.LoadingKernelSu
            InstallPhase.Installed -> ExecutionStage.VerifyingRoot
            InstallPhase.Failed -> mutableState.value.executionStage
        }
        mutableState.value = mutableState.value.copy(
            phase = phase,
            message = message,
            executionStage = stage,
            rootActive = phase == InstallPhase.Installed || mutableState.value.rootActive,
        )
        appendLog("[*] $message")
    }

    private fun appendLog(line: String) {
        val cleanLine = stripAnsi(line).trim()
        if (cleanLine.isBlank()) return
        val progress = parseExecutionProgress(cleanLine, mutableState.value.executionStage)
        fullRunLog = (fullRunLog + "\n" + cleanLine).trim()
        mutableState.value = mutableState.value.copy(
            log = logTail(fullRunLog),
            executionStage = progress.stage,
            executionDetail = progress.detail ?: mutableState.value.executionDetail,
        )
        updateHistoryLog()
    }

    private fun startHistory() {
        fullRunLog = ""
        val entry = historyStore.create()
        activeHistoryEntry = entry
        publishHistory(entry)
    }

    private fun updateHistory(transform: (InstallHistoryEntry) -> InstallHistoryEntry) {
        val entry = activeHistoryEntry ?: return
        val updated = transform(entry)
        activeHistoryEntry = updated
        historyStore.save(updated)
        publishHistory(updated)
    }

    private fun updateHistoryLog() =
        updateHistory { it.copy(log = fullRunLog) }

    private fun updateHistoryProfile(profileId: String) =
        updateHistory { it.copy(profileId = profileId) }

    private fun finishHistory(result: InstallRunResult) {
        updateHistory { entry ->
            entry.copy(
                completedAtMillis = System.currentTimeMillis(),
                result = result,
                log = fullRunLog,
            )
        }
        activeHistoryEntry = null
    }

    private fun publishHistory(entry: InstallHistoryEntry) {
        mutableHistory.value = (mutableHistory.value.filterNot { it.id == entry.id } + entry)
            .sortedByDescending(InstallHistoryEntry::startedAtMillis)
    }

    companion object {
        private const val EXPLOIT_STALL_MILLIS = 90_000L
        private const val EXPLOIT_TOTAL_MILLIS = 900_000L
        private const val UI_PUBLISH_MILLIS = 1_000L
        private const val HISTORY_SAVE_MILLIS = 10_000L
        private const val DISPLAY_LOG_LINES = 180
        private const val INSTALL_RECEIPT = "install_receipt"
        private const val RECEIPT_BOOT_TOKEN = "kernel_boot_id"
        private const val RECEIPT_VERIFIED = "verified"
        private const val P0_CACHE = "p0_cache"
        private const val P0_CACHE_BOOT_TOKEN = "kernel_boot_id"
        private const val P0_CACHE_OFFSET = "offset"
        private const val P0_OFFSET_ENV = "SLIDE_P0_OFFSET"
        private const val P0_OFFSET_MAX = 0x1f0000L
        private const val P0_OFFSET_MASK = 0xffffL
        private const val SHIZUKU_HELPER_PATH = "/data/local/tmp/ksu-helper"
        private const val SHIZUKU_PAYLOAD_PATH = "/data/local/tmp/ksu-payload"
        private const val SHIZUKU_LAUNCHER_PATH = "/data/local/tmp/stability-launcher"
        private const val SHIZUKU_KSUD_PATH = "/data/local/tmp/ksud-s25u-kdp"
        private const val SHIZUKU_KSUD_STAGE_PATH = "/data/local/tmp/.ksud-stage"
        private val LOG_POLL_INTERVAL = 1_000.milliseconds
        private val ANSI_ESCAPE = Regex("\u001B\\[[0-?]*[ -/]*[@-~]")
        private val P0_OFFSET_PATTERN = Regex(
            "(?:slide-kaslr-ok[^\\n]*slide=|\\[kaslr][^\\n]*p0_offset=)([0-9a-fA-F]{6,16})",
        )

        private fun stripAnsi(value: String): String = ANSI_ESCAPE.replace(value, "").replace("\r", "")

        private fun logTail(value: String): String =
            value.lineSequence().toList().takeLast(DISPLAY_LOG_LINES).joinToString("\n")
    }
}
