package dev.busung.s25uroot

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AppUpdaterTest {
    @Test
    fun olderReleaseIsNotAnUpdate() {
        assertFalse(AppUpdater.isUpdateAvailable("0.2.36", "0.3.0"))
    }

    @Test
    fun sameReleaseWithPrefixAndSuffixIsNotAnUpdate() {
        assertFalse(AppUpdater.isUpdateAvailable("v0.3.0-afzg1", "0.3.0"))
    }

    @Test
    fun newerReleaseIsAnUpdate() {
        assertTrue(AppUpdater.isUpdateAvailable("0.3.1", "0.3.0"))
        assertTrue(AppUpdater.isUpdateAvailable("0.4.0", "0.3.9"))
    }

    @Test
    fun invalidReleaseFailsClosed() {
        assertFalse(AppUpdater.isUpdateAvailable("latest", "0.3.0"))
    }
}
