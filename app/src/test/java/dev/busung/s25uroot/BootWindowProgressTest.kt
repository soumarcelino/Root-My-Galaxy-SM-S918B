package dev.busung.s25uroot

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class BootWindowProgressTest {
    @Test
    fun quietWindowProgressUsesRemainingTime() {
        assertEquals(0f, installPhaseProgress(InstallPhase.WaitingForBootAllocator, 0, 120_000, 120_000, 120_000), 0.0001f)
        assertEquals(0.5f, installPhaseProgress(InstallPhase.WaitingForBootAllocator, 0, 120_000, 60_000, 120_000), 0.0001f)
        assertEquals(1f, installPhaseProgress(InstallPhase.WaitingForBootAllocator, 0, 120_000, 0, 120_000), 0.0001f)
    }

    @Test
    fun trackerHandlesStreamChunksAndQuietPeriod() {
        val tracker = BootAllocatorLogTracker()
        val partial = "[*] waiting for boot allocator quiet window seconds=120 uptime=100"
        assertNull(tracker.update(partial, 100_000))
        val window = tracker.update("$partial\n", 100_000)
        assertNotNull(window)
        assertEquals(120_000L, window!!.remainingMillis(100_000))
        assertEquals(60_000L, tracker.update("$partial\n", 160_000)!!.remainingMillis(160_000))
        assertNull(tracker.update("$partial\n[launcher] preload supervisor pid=42\n", 160_000))
    }
}
