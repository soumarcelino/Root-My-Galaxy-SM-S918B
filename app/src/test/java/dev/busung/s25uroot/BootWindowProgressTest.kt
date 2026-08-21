package dev.busung.s25uroot

import org.junit.Assert.assertEquals
import org.junit.Test

class BootWindowProgressTest {
    @Test
    fun followsAndRepeatsThe120SecondBootWindow() {
        assertEquals(0f, bootWindowProgress(0, 120_000), 0.0001f)
        assertEquals(0.5f, bootWindowProgress(60_000, 120_000), 0.0001f)
        assertEquals(0.999f, bootWindowProgress(119_880, 120_000), 0.0001f)
        assertEquals(0f, bootWindowProgress(120_000, 120_000), 0.0001f)
        assertEquals(0.5f, bootWindowProgress(180_000, 120_000), 0.0001f)
        assertEquals(0.5f, bootWindowProgress(45_000, 30_000), 0.0001f)
    }
}
