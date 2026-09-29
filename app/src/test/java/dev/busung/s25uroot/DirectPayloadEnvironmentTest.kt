package dev.busung.s25uroot

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class DirectPayloadEnvironmentTest {
    @Test
    fun reproducesLauncherPayloadEnvironment() {
        val environment = directPayloadEnvironment(
            payloadPath = "/data/local/tmp/payload.so",
            helperPath = "/data/user/0/app/helper",
            mmFactoryPath = "/data/local/tmp/mm-factory",
            p0Offset = "0x1234",
        )

        assertEquals("/data/local/tmp/payload.so", environment["LD_PRELOAD"])
        assertEquals("/data/user/0/app/helper", environment["CVE43499_ROOT_HELPER"])
        assertEquals("/data/local/tmp/mm-factory", environment["CVE43499_MM_FACTORY"])
        assertEquals("3", environment["EXPLOIT_ATTEMPTS"])
        assertEquals("45", environment["P0_ATTEMPT_TIMEOUT_SEC"])
        assertEquals("180", environment["EXPLOIT_ATTEMPT_TIMEOUT_SEC"])
        assertEquals("0", environment["BOOT_QUIET_SEC"])
        assertEquals("1", environment["FUTEX_WAIT_SEC"])
        assertEquals("64", environment["KSNITCH_REPEAT"])
        assertEquals("0x1234", environment["SLIDE_P0_OFFSET"])
    }

    @Test
    fun omitsOptionalFactoryAndCachedOffset() {
        val environment = directPayloadEnvironment("payload", "helper", null, null)

        assertFalse(environment.containsKey("CVE43499_MM_FACTORY"))
        assertFalse(environment.containsKey("SLIDE_P0_OFFSET"))
    }
}
