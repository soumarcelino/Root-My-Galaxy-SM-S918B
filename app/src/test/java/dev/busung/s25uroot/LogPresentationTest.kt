package dev.busung.s25uroot

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class LogPresentationTest {
    @Test
    fun formatsPayloadAndLauncherLogsWithinMeasuredWidth() {
        val raw = """
            [00:00:00.000] [kaslr] source=tracefs base=ffffffc008060000 slide=0000000000060000 p0_offset=00060000 votes=2 sample_ms=0
            [launcher] gate=1/2 phase=baseline temp=37.0C mem=6800MB runnable=1 load=3.0 psi=2.0/0.0/0.0 mm=704/704 slabs=22 uptime=130s boot=1
            [groom] exec factory ready objects=1024 cpu=5 elapsed=224ms
            [*] stage=verifying-kernel-access
        """.trimIndent()

        val display = formatLogForDisplay(raw)

        assertTrue(display.lineSequence().all { it.length <= LOG_DISPLAY_COLUMNS })
        assertTrue("Kernel" in display)
        assertTrue("Stability 1/2" in display)
        assertTrue("Factory ready" in display)
        assertTrue("Verifying kernel access" in display)
    }

    @Test
    fun preservesTerminalBopeSuccessLayout() {
        val raw = "BOPE :: Success\n        Root achieved in 47 seconds"

        assertEquals(raw, formatLogForDisplay(raw))
    }

    @Test
    fun wrapsUnknownMessagesWithoutAutomaticLineBreaks() {
        val raw = "[launcher] " + "message ".repeat(20) + "/data/local/tmp/file-with-a-very-long-name.bin"
        val lines = formatLogForDisplay(raw).lines()

        assertTrue(lines.size > 1)
        assertTrue(lines.all { it.length <= LOG_DISPLAY_COLUMNS })
        assertTrue(lines.drop(1).all { it.startsWith("  ") })
    }

    @Test
    fun fixtureFromDeviceAlwaysFitsOneVisualLine() {
        val raw = checkNotNull(javaClass.getResource("/afzh3-last-success.log")).readText()
        val lines = formatLogForDisplay(raw).lines()

        assertEquals(LOG_DISPLAY_COLUMNS, 42)
        assertTrue(lines.isNotEmpty())
        assertTrue(lines.all { it.length <= LOG_DISPLAY_COLUMNS })
    }
}
