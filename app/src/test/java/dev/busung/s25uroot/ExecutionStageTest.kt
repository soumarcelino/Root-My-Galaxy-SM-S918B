package dev.busung.s25uroot

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ExecutionStageTest {
    @Test
    fun journeyWaitsForFirstStabilitySampleThenAdvancesByStage() {
        assertNull(executionJourneyProgress(ExecutionStage.Preparing, null))
        assertNull(executionJourneyProgress(ExecutionStage.Stabilizing, null))
        val sample = StabilizationMetrics("39.8", 6840, sample = 2, requiredSamples = 3)
        assertEquals((1f + 2f / 3f) / 9f, executionJourneyProgress(ExecutionStage.Stabilizing, sample)!!, 0.0001f)
        assertEquals(2f / 9f, executionJourneyProgress(ExecutionStage.StartingExploit, sample)!!, 0.0001f)
        assertEquals(8f / 9f, executionJourneyProgress(ExecutionStage.VerifyingRoot, null)!!, 0.0001f)
    }

    @Test
    fun readsLatestStabilizationSample() {
        val metrics = parseStabilizationMetrics(
            """
                [launcher] gate=1/3 phase=baseline temp=43.2C mem=6200MB runnable=2
                [launcher] gate=2/3 phase=baseline temp=39.8C mem=6840MB runnable=1
            """.trimIndent(),
        )

        assertEquals("39.8", metrics?.temperatureCelsius)
        assertEquals(6840, metrics?.availableMemoryMb)
        assertEquals(2, metrics?.sample)
        assertEquals(3, metrics?.requiredSamples)
        assertNull(parseStabilizationMetrics("[launcher] waiting for quiet window"))
    }

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
        assertEquals("Canal de controle do KernelSU confirmado.", progress.detail)
    }

    @Test
    fun neverMovesBackwardsWhenOlderLogIsPublished() {
        val progress = parseExecutionProgress(
            "[launcher] gate=1/5 phase=baseline temp=37.0C mem=6800MB runnable=1 " +
                "psi=2.0/0.0/0.0 mm=704/704 slabs=22",
            ExecutionStage.BuildingPipeBridge,
        )

        assertEquals(ExecutionStage.BuildingPipeBridge, progress.stage)
        assertNull(progress.detail)
    }

    @Test
    fun replaysLatestDeviceRunInOrder() {
        val log = checkNotNull(javaClass.getResource("/afzh3-last-success.log")).readText()
        var progress = ExecutionProgress(ExecutionStage.Preparing)
        val stages = linkedSetOf(progress.stage)
        log.lineSequence().forEach { line ->
            progress = parseExecutionProgress(line, progress.stage, progress.detail)
            stages.add(progress.stage)
            when {
                "gate=0/3 phase=baseline" in line ->
                    assertTrue(progress.detail.orEmpty().startsWith("Estabilização 0/3"))
                "gate=3/3 phase=cooldown" in line ->
                    assertTrue(progress.detail.orEmpty().startsWith("Cooldown 3/3"))
                "pipe-gate=pass" in line ->
                    assertTrue(progress.detail.orEmpty().contains("aguardando liberação"))
                "stage=locating-kernel" in line || "stage=temporary-root-ready" in line ->
                    assertNull(progress.detail)
            }
        }
        assertEquals(ExecutionStage.entries.toList(), stages.toList())
        assertEquals(parseExecutionProgress(log), progress)
        assertEquals("Canal de controle do KernelSU confirmado.", progress.detail)
    }

    @Test
    fun oldLogCannotReplaceCurrentDetail() {
        val progress = parseExecutionProgress(
            "[launcher] pipe-gate=pass pipes=480 target_pages=32",
            ExecutionStage.VerifyingRoot,
            "Canal de controle do KernelSU confirmado.",
        )
        assertEquals(ExecutionStage.VerifyingRoot, progress.stage)
        assertEquals("Canal de controle do KernelSU confirmado.", progress.detail)
    }

    @Test
    fun acceptsLauncherWithoutCooldown() {
        val progress = parseExecutionProgress("[launcher] estabilidade confirmada: métricas+slab+pipe")
        assertEquals(ExecutionStage.Stabilizing, progress.stage)
        assertTrue(progress.detail.orEmpty().contains("aprovadas"))
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
