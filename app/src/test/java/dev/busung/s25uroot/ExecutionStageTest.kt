package dev.busung.s25uroot

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ExecutionStageTest {
    @Test
    fun followsLauncherAndPayloadMarkers() {
        val log = """
            [launcher] gate=3/5 phase=baseline temp=37.0C mem=6800MB runnable=1 load=3.0 psi=2.0/0.0/0.0 mm=704/704 slabs=22 uptime=130s boot=1
            [launcher] pipe-gate=pass pipes=480 target_pages=32
            [*] starting exploit attempts=24
            [*] stage=locating-kernel
            [*] stage=verifying-kernel-access
            [*] stage=starting-temporary-root
            [pipe_rw] selection accept slab=1 pipe=31 candidates=2
            [+] stage=temporary-root-ready
            KernelSU control verified version=33214
        """.trimIndent()

        val progress = parseExecutionProgress(log)

        assertEquals(ExecutionStage.VerifyingRoot, progress.stage)
        assertTrue(progress.detail.orEmpty().contains("pipe"))
    }

    @Test
    fun neverMovesBackwardsWhenOlderLogIsPublished() {
        val progress = parseExecutionProgress(
            "[launcher] gate=1/5 phase=baseline temp=37.0C mem=6800MB runnable=1 " +
                "psi=2.0/0.0/0.0 mm=704/704 slabs=22",
            ExecutionStage.BuildingPipeBridge,
        )

        assertEquals(ExecutionStage.BuildingPipeBridge, progress.stage)
    }

    @Test
    fun exposesSafeRetryWhenFutexCallbackMisses() {
        val progress = parseExecutionProgress(
            """
                [*] exploit attempt=12/24
                [*] stage=locating-kernel
                [futex-v14] verify callback never invoked
            """.trimIndent(),
        )

        assertEquals(ExecutionStage.LocatingKernel, progress.stage)
        assertTrue(progress.detail.orEmpty().contains("antes de qualquer mutação"))
    }
}
