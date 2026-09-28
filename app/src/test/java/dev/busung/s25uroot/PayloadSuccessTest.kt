package dev.busung.s25uroot

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PayloadSuccessTest {
    @Test
    fun parsesPayloadOnlyDurationFromTerminalBopeMarker() {
        val log = """
            [launcher] gate=2/2 phase=fast
            [BOPE] Brazilian Open Payload Engine initialized
            BOPE :: Success
                    Root achieved in 47 seconds
        """.trimIndent()

        assertEquals(47_000L, bopeRootDurationMillis(log))
    }

    @Test
    fun ignoresLegacyAndMalformedMarkers() {
        assertNull(bopeRootDurationMillis("[App] root acquired in 90 seconds"))
        assertNull(bopeRootDurationMillis("BOPE :: Success\\nRoot achieved in nope seconds"))
    }
}
