package dev.busung.s25uroot

enum class ExecutionStage {
    Preparing,
    Stabilizing,
    StartingExploit,
    LocatingKernel,
    VerifyingKernelAccess,
    StartingTemporaryRoot,
    BuildingPipeBridge,
    LoadingKernelSu,
    VerifyingRoot,
}

data class ExecutionProgress(
    val stage: ExecutionStage,
    val detail: String? = null,
)

data class StabilizationMetrics(
    val temperatureCelsius: String,
    val availableMemoryMb: Int,
    val sample: Int,
    val requiredSamples: Int,
)

private val stabilizationSamplePattern = Regex(
    """(?m)^\[launcher] gate=(\d+)/(\d+).*?temp=(\d+(?:\.\d+)?)C mem=(\d+)MB\b""",
)

internal fun parseStabilizationMetrics(rawLog: String): StabilizationMetrics? {
    val match = stabilizationSamplePattern.findAll(rawLog).lastOrNull() ?: return null
    return StabilizationMetrics(
        temperatureCelsius = match.groupValues[3],
        availableMemoryMb = match.groupValues[4].toIntOrNull() ?: return null,
        sample = match.groupValues[1].toIntOrNull() ?: return null,
        requiredSamples = match.groupValues[2].toIntOrNull() ?: return null,
    )
}

internal fun executionJourneyProgress(
    stage: ExecutionStage,
    metrics: StabilizationMetrics?,
): Float? {
    if (stage == ExecutionStage.Preparing) return null
    val completedStages = stage.ordinal.toFloat()
    val stageFraction = if (stage == ExecutionStage.Stabilizing) {
        metrics ?: return null
        (metrics.sample.toFloat() / metrics.requiredSamples.coerceAtLeast(1)).coerceIn(0f, 1f)
    } else 0f
    return (completedStages + stageFraction) / ExecutionStage.entries.size
}

private val launcherGatePattern = Regex(
    "gate=(\\d+/\\d+).*temp=([^ ]+) mem=([^ ]+) runnable=(\\d+).*" +
        "psi=([^ ]+).*mm=([^ ]+) slabs=([^ ]+)",
)
private val exploitAttemptPattern = Regex("exploit attempt=(\\d+)/(\\d+)")

internal fun parseExecutionProgress(
    rawLog: String,
    initial: ExecutionStage = ExecutionStage.Preparing,
    initialDetail: String? = null,
): ExecutionProgress {
    var stage = initial
    var detail: String? = initialDetail

    fun advance(candidate: ExecutionStage) {
        if (candidate.ordinal > stage.ordinal) {
            stage = candidate
            detail = null
        }
    }

    fun describe(candidate: ExecutionStage, message: String) {
        advance(candidate)
        if (stage == candidate) detail = message
    }

    rawLog.lineSequence().forEach { rawLine ->
        val line = rawLine.trim()
        val lowered = line.lowercase()
        when {
            lowered.startsWith("[launcher] gate=") -> {
                advance(ExecutionStage.Stabilizing)
                launcherGatePattern.find(line)?.let { match ->
                    val phase = if ("phase=cooldown" in lowered) "Cooldown" else "Estabilização"
                    describe(ExecutionStage.Stabilizing, "$phase ${match.groupValues[1]} · ${match.groupValues[2]} · " +
                        "${match.groupValues[3]} livres · ${match.groupValues[4]} tarefas · " +
                        "PSI ${match.groupValues[5]} · mm ${match.groupValues[6]} · " +
                        "${match.groupValues[7]} slabs")
                }
            }
            "[launcher] pipe-gate=pass" in lowered -> {
                describe(ExecutionStage.Stabilizing, "Capacidade dos pipes aprovada; aguardando liberação do launcher.")
            }
            "[launcher] cooldown:" in lowered -> {
                describe(ExecutionStage.Stabilizing, "Confirmando estabilidade após o teste dos pipes.")
            }
            "[launcher] estabilidade confirmada" in lowered ||
                "[launcher] estabilidade máxima confirmada" in lowered ->
                describe(ExecutionStage.Stabilizing, "Verificações do launcher aprovadas; aguardando início do payload.")
            "[launcher] execve:" in lowered || "stage=preparing-kernel-access" in lowered ->
                advance(ExecutionStage.StartingExploit)
            "starting exploit" in lowered -> advance(ExecutionStage.StartingExploit)
            "exploit attempt=" in lowered -> {
                advance(ExecutionStage.StartingExploit)
                exploitAttemptPattern.find(lowered)?.let { match ->
                    describe(ExecutionStage.StartingExploit, "Tentativa ${match.groupValues[1]} de ${match.groupValues[2]}.")
                }
            }
            "verify callback never invoked" in lowered -> {
                detail = "Callback ainda não acionado; repetindo antes de qualquer mutação."
            }
            "stage=locating-kernel" in lowered -> advance(ExecutionStage.LocatingKernel)
            "stage=kernel-location-ready" in lowered ->
                describe(ExecutionStage.LocatingKernel, "Kernel localizado; preparando acesso.")
            "[groom]" in lowered ->
                describe(ExecutionStage.LocatingKernel, "Kernel localizado; preparando memória para acesso.")
            "stage=verifying-kernel-access" in lowered ->
                advance(ExecutionStage.VerifyingKernelAccess)
            "stage=starting-temporary-root" in lowered ->
                advance(ExecutionStage.StartingTemporaryRoot)
            "stage=kernel-mutation-pending" in lowered ->
                describe(ExecutionStage.StartingTemporaryRoot, "Etapa crítica em andamento. Não interrompa a execução.")
            "[pipe_rw]" in lowered -> {
                advance(ExecutionStage.BuildingPipeBridge)
                val message = when {
                    "selection accept" in lowered -> "Candidato de pipe validado."
                    "ready attempt=" in lowered -> "Leitura e escrita comprovadas; preparando root temporário."
                    "candidate test" in lowered -> "Testando acesso pelo candidato selecionado."
                    "selection" in lowered -> "Procurando um pipe adequado para acesso à memória."
                    else -> null
                }
                if (message != null) describe(ExecutionStage.BuildingPipeBridge, message)
            }
            "[root_umh] queued" in lowered ->
                describe(ExecutionStage.BuildingPipeBridge, "Solicitando início do serviço de root temporário.")
            "[root_umh] result" in lowered ->
                describe(ExecutionStage.BuildingPipeBridge, "Aguardando confirmação do root temporário.")
            "temporary-root-ready" in lowered -> advance(ExecutionStage.LoadingKernelSu)
            "kernelsu control verified" in lowered ->
                describe(ExecutionStage.VerifyingRoot, "Canal de controle do KernelSU confirmado.")
        }
    }
    return ExecutionProgress(stage, detail)
}
