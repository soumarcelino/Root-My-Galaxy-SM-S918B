package dev.busung.s25uroot

private val BOPE_SUCCESS = Regex(
    "(?m)^BOPE :: Success\\r?\\n[ \\t]+Root achieved in (\\d+) seconds[ \\t]*$",
)

internal fun bopeRootDurationMillis(log: String): Long? {
    val seconds = BOPE_SUCCESS.findAll(log).lastOrNull()
        ?.groupValues
        ?.get(1)
        ?.toLongOrNull()
        ?: return null
    if (seconds > Long.MAX_VALUE / 1_000L) return null
    return seconds * 1_000L
}
