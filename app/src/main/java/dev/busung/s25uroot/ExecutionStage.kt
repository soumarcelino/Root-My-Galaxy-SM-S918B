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

private val launcherGatePattern = Regex(
    "gate=(\\d+/5).*temp=([^ ]+) mem=([^ ]+) runnable=(\\d+).*" +
        "psi=([^ ]+).*mm=([^ ]+) slabs=([^ ]+)",
)
private val exploitAttemptPattern = Regex("exploit attempt=(\\d+)/(\\d+)")

internal fun parseExecutionProgress(
    rawLog: String,
    initial: ExecutionStage = ExecutionStage.Preparing,
): ExecutionProgress {
    var stage = initial
    var detail: String? = null

    fun advance(candidate: ExecutionStage) {
        if (candidate.ordinal > stage.ordinal) stage = candidate
    }

    rawLog.lineSequence().forEach { rawLine ->
        val line = rawLine.trim()
        val lowered = line.lowercase()
        when {
            lowered.startsWith("[launcher] gate=") -> {
                advance(ExecutionStage.Stabilizing)
                launcherGatePattern.find(line)?.let { match ->
                    detail = "Estável ${match.groupValues[1]} · ${match.groupValues[2]} · " +
                        "${match.groupValues[3]} livres · ${match.groupValues[4]} tarefas · " +
                        "PSI ${match.groupValues[5]} · mm ${match.groupValues[6]} · " +
                        "${match.groupValues[7]} slabs"
                }
            }
            "[launcher] pipe-gate=pass" in lowered -> {
                advance(ExecutionStage.Stabilizing)
                detail = "Capacidade de 480 pipes aprovada; confirmando cooldown."
            }
            "[launcher] estabilidade máxima confirmada" in lowered -> {
                advance(ExecutionStage.Stabilizing)
                detail = "Temperatura, pressão, slab, pipes e cooldown aprovados."
            }
            "starting exploit" in lowered -> advance(ExecutionStage.StartingExploit)
            "exploit attempt=" in lowered -> {
                advance(ExecutionStage.StartingExploit)
                exploitAttemptPattern.find(lowered)?.let { match ->
                    detail = "Tentativa ${match.groupValues[1]} de ${match.groupValues[2]}."
                }
            }
            "verify callback never invoked" in lowered -> {
                detail = "Callback ainda não acionado; repetindo antes de qualquer mutação."
            }
            "stage=locating-kernel" in lowered -> advance(ExecutionStage.LocatingKernel)
            "stage=verifying-kernel-access" in lowered ->
                advance(ExecutionStage.VerifyingKernelAccess)
            "stage=starting-temporary-root" in lowered ->
                advance(ExecutionStage.StartingTemporaryRoot)
            "[pipe_rw]" in lowered -> {
                advance(ExecutionStage.BuildingPipeBridge)
                detail = when {
                    "selection accept" in lowered -> "Candidato de pipe validado no mesmo spray."
                    "ready attempt=" in lowered -> "Ponte de leitura e escrita física comprovada."
                    else -> detail
                }
            }
            "temporary-root-ready" in lowered -> advance(ExecutionStage.LoadingKernelSu)
            "kernelsu control verified" in lowered -> advance(ExecutionStage.VerifyingRoot)
        }
    }
    return ExecutionProgress(stage, detail)
}
