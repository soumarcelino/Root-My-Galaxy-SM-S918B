package dev.busung.s25uroot

internal data class BootAllocatorWindow(
    val totalMillis: Long,
    val deadlineMillis: Long,
) {
    fun remainingMillis(nowMillis: Long): Long =
        (deadlineMillis - nowMillis).coerceIn(0L, totalMillis)
}

/** Consumes complete log lines once; the countdown continues between log messages. */
internal class BootAllocatorLogTracker {
    private var consumedLength = 0
    private var window: BootAllocatorWindow? = null

    fun update(rawLog: String, nowMillis: Long): BootAllocatorWindow? {
        if (rawLog.length < consumedLength) {
            consumedLength = 0
            window = null
        }
        while (true) {
            val end = rawLog.indexOf('\n', consumedLength)
            if (end < 0) break
            val line = ANSI_ESCAPE.replace(rawLog.substring(consumedLength, end), "")
            consumedLength = end + 1
            val wait = WAIT_PATTERN.find(line)
            if (wait != null) {
                val seconds = wait.groupValues[1].toLongOrNull() ?: continue
                if (seconds !in 0..Long.MAX_VALUE / 1_000L) continue
                val totalMillis = seconds * 1_000L
                // Some payloads report CLOCK_BOOTTIME uptime; others report only seconds.
                val reportedUptime = wait.groupValues[2].toLongOrNull()
                val startedAt = reportedUptime
                    ?.takeIf { it in 0..nowMillis / 1_000L }
                    ?.times(1_000L)
                    ?: nowMillis
                if (totalMillis > Long.MAX_VALUE - startedAt) continue
                window = BootAllocatorWindow(totalMillis, startedAt + totalMillis)
            } else if (EXECUTION_PATTERN.containsMatchIn(line)) {
                window = null
            }
        }
        window = window?.takeIf { it.remainingMillis(nowMillis) > 0L }
        return window
    }

    companion object {
        private val WAIT_PATTERN = Regex(
            "waiting for boot allocator quiet window seconds=(\\d+)(?:\\s+uptime=(\\d+))?",
        )
        private val EXECUTION_PATTERN = Regex(
            "preload supervisor pid=|exploit attempt=\\d+/\\d+|stage=temporary-root-ready|exploit completed",
        )
        private val ANSI_ESCAPE = Regex("\u001B\\[[0-?]*[ -/]*[@-~]")
    }
}
