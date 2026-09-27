package dev.busung.s25uroot

private val REBOOT_REQUIRED_MARKERS = listOf(
    "stage=kernel-mutation-pending",
    "stage=verifying-kernel-access",
    "stage=starting-temporary-root",
    "stage=credential-mutation-pending",
    "stage=workqueue-mutation-pending",
    "kernel mutation reached",
    "timeout after kernel mutation",
    "refusing unsafe retry before reboot",
    "restore unavailable",
    "[immediate]",
    "[pipe_rw]",
    "[root_umh] queued",
    "stage=temporary-root-ready",
)

internal fun payloadFailureRequiresReboot(payloadLog: String): Boolean {
    val normalized = payloadLog.lowercase()
    return REBOOT_REQUIRED_MARKERS.any(normalized::contains)
}
