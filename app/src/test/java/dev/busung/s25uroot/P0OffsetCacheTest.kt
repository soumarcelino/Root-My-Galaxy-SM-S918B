package dev.busung.s25uroot

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class P0OffsetCacheTest {
    @Test
    fun acceptsRealZzi8SlidesAt32KiBAlignment() {
        assertEquals("0x148000", normalizeP0Offset("148000"))
        assertEquals("0x148000", normalizeP0Offset("0x148000"))
        assertEquals("0x1f8000", normalizeP0Offset("1F8000"))
    }

    @Test
    fun rejectsStaleMalformedOrMisalignedSlides() {
        assertNull(normalizeP0Offset(null))
        assertNull(normalizeP0Offset(""))
        assertNull(normalizeP0Offset("148001"))
        assertNull(normalizeP0Offset("200000"))
        assertNull(normalizeP0Offset("not-a-slide"))
    }
}
