package dev.busung.s25uroot

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Applies best-effort shell optimizations through the required Shizuku runner. */
object StartupOptimizer {
    suspend fun apply(context: Context): String = withContext(Dispatchers.IO) {
        if (!ShizukuController.isGranted()) {
            return@withContext "Shizuku unavailable"
        }

        val packageName = context.packageName
        val keep = setOf(packageName, SHIZUKU_PACKAGE)
        val results = mutableListOf<Boolean>()

        results += runCommand("cmd", "deviceidle", "whitelist", "+$packageName")
        results += runCommand("am", "set-standby-bucket", packageName, "active")
        results += runCommand("am", "kill-all")

        val packages = runCatching {
            ShizukuController.capture(arrayOf("pm", "list", "packages", "-3"))
                .lineSequence()
                .map { it.removePrefix("package:").trim() }
                .filter { it.isNotEmpty() && it !in keep && it.matches(PACKAGE_NAME) }
                .toList()
        }.getOrDefault(emptyList())
        val stopped = packages.count {
            val ok = runCommand("am", "force-stop", "--user", "0", it)
            results += ok
            ok
        }

        "commands=${results.count { it }}/${results.size} stopped=$stopped/${packages.size}"
    }

    private fun runCommand(vararg command: String): Boolean {
        val process = runCatching { ShizukuController.exec(command.toList().toTypedArray()) }.getOrNull()
            ?: return false
        return runCatching { process.waitFor() == 0 }.getOrDefault(false)
    }

    private const val SHIZUKU_PACKAGE = "moe.shizuku.manager"
    private val PACKAGE_NAME = Regex("[A-Za-z0-9_]+(\\.[A-Za-z0-9_]+)+")
}
