package dev.busung.s25uroot

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FatalPayloadFailureTest {
    @Test
    fun preMutationFailuresRemainRetryable() {
        assertFalse(
            payloadFailureRequiresReboot(
                "[groom] failed before reaching the reclaim step\n" +
                    "[supervisor] attempt failed before mutation",
            ),
        )
    }

    @Test
    fun kernelAccessStageRequiresCleanBootAfterFailure() {
        assertTrue(
            payloadFailureRequiresReboot(
                "[*] stage=verifying-kernel-access\n[immediate] verify=0",
            ),
        )
    }

    @Test
    fun explicitUnsafeRetryMessageRequiresReboot() {
        assertTrue(
            payloadFailureRequiresReboot(
                "[supervisor] kernel mutation reached; refusing unsafe retry before reboot",
            ),
        )
    }

    @Test
    fun pipeAndWorkqueueFailuresRequireReboot() {
        assertTrue(payloadFailureRequiresReboot("[pipe_rw] terminal failure attempt=1/12"))
        assertTrue(payloadFailureRequiresReboot("[root_umh] queued work=ffffff8000000000"))
    }
}
