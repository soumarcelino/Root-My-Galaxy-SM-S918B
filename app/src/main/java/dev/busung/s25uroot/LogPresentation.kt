package dev.busung.s25uroot

internal const val LOG_DISPLAY_COLUMNS = 42

private val ANSI_ESCAPE = Regex("\u001B\\[[0-?]*[ -/]*[@-~]")
private val TIME_PREFIX = Regex("^\\[\\d{2}:\\d{2}:\\d{2}\\.\\d{3}]\\s*")
private val TAG_PREFIX = Regex("^\\[([^]]{1,24})]\\s*")
private val WHITESPACE = Regex("\\s+")
private val STAGE = Regex("\\bstage=([a-z0-9-]+)", RegexOption.IGNORE_CASE)
private val LAUNCHER_GATE = Regex(
    "\\bgate=(\\d+)/(\\d+).*?temp=([^ ]+).*?mem=([^ ]+)",
    RegexOption.IGNORE_CASE,
)
private val EXPLOIT_ATTEMPT = Regex("\\bexploit attempt=(\\d+)/(\\d+)", RegexOption.IGNORE_CASE)
private val EXPLOIT_START = Regex("\\bstarting exploit attempts=(\\d+)", RegexOption.IGNORE_CASE)
private val DEBUG_PHASE = Regex("\\bdbg phase=([^ ]+).*?dt=([^ ]+)", RegexOption.IGNORE_CASE)
private val EXEC_FACTORY = Regex(
    "\\bexec factory ready objects=(\\d+) cpu=(\\d+) elapsed=([^ ]+)",
    RegexOption.IGNORE_CASE,
)
private val SHIZUKU_CHECK = Regex(
    "\\bshizuku-gate=check attempt=(\\d+) remaining=([^ ]+) connected=(\\d+)",
    RegexOption.IGNORE_CASE,
)

internal fun formatLogForDisplay(
    rawLog: String,
    maxColumns: Int = LOG_DISPLAY_COLUMNS,
): String {
    require(maxColumns >= 20)
    return rawLog.lineSequence()
        .flatMap { formatLogLine(it, maxColumns).asSequence() }
        .joinToString("\n")
}

private fun formatLogLine(rawLine: String, maxColumns: Int): List<String> {
    var value = ANSI_ESCAPE.replace(rawLine, "").replace("\r", "").trim()
    if (value.isEmpty()) return emptyList()

    value = TIME_PREFIX.replace(value, "")
    var source: String? = null
    var level: String? = null
    while (true) {
        val match = TAG_PREFIX.find(value) ?: break
        if (match.range.first != 0) break
        val tag = match.groupValues[1]
        value = value.removeRange(match.range).trimStart()
        when (tag) {
            "+" -> level = "OK"
            "-" -> level = "Error"
            "!" -> level = "Warning"
            "*" -> Unit
            else -> source = sourceLabel(tag)
        }
    }

    val message = compactKnownMessage(value).ifBlank { "Event recorded" }
    val heading = listOfNotNull(source, level).distinct().joinToString(" · ")
    val display = if (heading.isBlank()) message else "$heading | $message"
    return wrapDisplayLine(display, maxColumns)
}

private fun sourceLabel(tag: String): String = when {
    tag.equals("launcher", ignoreCase = true) -> "Launcher"
    tag.equals("app", ignoreCase = true) ||
        tag.equals("diag", ignoreCase = true) ||
        tag.equals("GUI", ignoreCase = true) -> "App"
    tag.equals("kaslr", ignoreCase = true) -> "Kernel"
    tag.equals("groom", ignoreCase = true) -> "Memory"
    tag.equals("pipe_rw", ignoreCase = true) -> "Pipe"
    tag.equals("aar_aaw", ignoreCase = true) ||
        tag.equals("immediate", ignoreCase = true) -> "Access"
    tag.equals("root_umh", ignoreCase = true) -> "Root"
    tag.startsWith("futex", ignoreCase = true) -> "Trigger"
    tag.equals("cpu", ignoreCase = true) -> "CPU"
    else -> tag.replace('_', ' ').take(12)
}

private fun compactKnownMessage(value: String): String {
    STAGE.find(value)?.let { match ->
        val stage = match.groupValues[1]
        return when (stage) {
            "preparing-kernel-access" -> "Preparing kernel access"
            "locating-kernel" -> "Locating kernel"
            "kernel-location-ready" -> "Kernel located"
            "verifying-kernel-access" -> "Verifying kernel access"
            "starting-temporary-root" -> "Starting temporary root"
            "kernel-mutation-pending" -> "Kernel mutation in progress"
            "temporary-root-ready" -> "Temporary root ready"
            else -> "Stage · ${stage.replace('-', ' ')}"
        }
    }
    LAUNCHER_GATE.find(value)?.let { match ->
        return "Stability ${match.groupValues[1]}/${match.groupValues[2]}" +
            " · ${match.groupValues[3]} · ${match.groupValues[4]}"
    }
    EXPLOIT_ATTEMPT.find(value)?.let { match ->
        return "Exploit attempt ${match.groupValues[1]}/${match.groupValues[2]}"
    }
    EXPLOIT_START.find(value)?.let { match ->
        return "Exploit started · ${match.groupValues[1]} attempts"
    }
    DEBUG_PHASE.find(value)?.let { match ->
        return "${humanizePhase(match.groupValues[1])} · ${match.groupValues[2]}"
    }
    EXEC_FACTORY.find(value)?.let { match ->
        return "Factory ready · ${match.groupValues[1]} objs" +
            " · CPU ${match.groupValues[2]} · ${match.groupValues[3]}"
    }
    SHIZUKU_CHECK.find(value)?.let { match ->
        val state = if (match.groupValues[3] == "1") "connected" else "waiting"
        return "Shizuku $state · try ${match.groupValues[1]} · ${match.groupValues[2]}"
    }
    if (value.contains("estabilidade confirmada", ignoreCase = true)) {
        return "Stability confirmed"
    }
    if (value.contains("payload=exec", ignoreCase = true) ||
        value.contains("execve: carregando payload", ignoreCase = true)
    ) {
        return "Starting payload"
    }
    if (value.contains("pipe-gate=pass", ignoreCase = true)) {
        return "Pipe capacity ready"
    }
    if (value.contains("uptime-gate=pass", ignoreCase = true)) {
        return "Minimum uptime ready"
    }
    if (value.contains("uptime-gate=fail", ignoreCase = true)) {
        return "Minimum uptime failed"
    }
    return value
        .replace("elapsed=", "time=")
        .replace("remaining=", "left=")
        .replace("attempt=", "try=")
        .replace("objects=", "objs=")
        .replace("samples=", "checks=")
        .replace(";", " ·")
        .replace(WHITESPACE, " ")
        .trim()
}

private fun humanizePhase(value: String): String = value
    .removeSuffix("-done")
    .split('-')
    .joinToString(" ") { word -> word.replaceFirstChar(Char::uppercase) }

private fun wrapDisplayLine(text: String, maxColumns: Int): List<String> {
    val words = text.split(WHITESPACE).filter(String::isNotBlank)
    if (words.isEmpty()) return emptyList()
    val lines = mutableListOf<String>()
    var current = ""
    words.forEach { rawWord ->
        val continuation = if (lines.isEmpty()) "" else "  "
        val wordLimit = maxColumns - continuation.length
        val word = compactToken(rawWord, wordLimit)
        val candidate = if (current.isEmpty()) "$continuation$word" else "$current $word"
        if (candidate.length <= maxColumns) {
            current = candidate
        } else {
            if (current.isNotEmpty()) lines += current
            current = "  ${compactToken(rawWord, maxColumns - 2)}"
        }
    }
    if (current.isNotEmpty()) lines += current
    return lines
}

private fun compactToken(value: String, limit: Int): String {
    if (value.length <= limit) return value
    if (limit <= 3) return value.take(limit)
    val head = (limit - 1) / 2
    val tail = limit - head - 1
    return value.take(head) + "…" + value.takeLast(tail)
}
