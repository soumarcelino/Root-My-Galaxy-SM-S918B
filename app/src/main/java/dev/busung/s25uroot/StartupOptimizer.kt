package dev.busung.s25uroot

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Applies best-effort shell optimizations when Shizuku has been explicitly enabled. */
object StartupOptimizer {
    suspend fun apply(context: Context): String = withContext(Dispatchers.IO) {
        if (!AppPreferences.shizukuMode(context) || !ShizukuController.isGranted()) {
            return@withContext "Shizuku unavailable"
        }

        val packageName = context.packageName
        val keep = setOf(packageName, SHIZUKU_PACKAGE, KSU_MANAGER_PACKAGE)
        val results = mutableListOf<String>()

        runCommand("cmd", "deviceidle", "whitelist", "+$packageName", results = results)
        runCommand("am", "set-standby-bucket", packageName, "active", results = results)
        runCommand("cmd", "power", "set-fixed-performance-mode-enabled", "true", results = results)

        val packages = runCatching {
            ShizukuController.capture(arrayOf("pm", "list", "packages", "-3"))
                .lineSequence()
                .map { it.removePrefix("package:").trim() }
                .filter { it.isNotEmpty() && it !in keep && it.matches(PACKAGE_NAME) }
                .toList()
        }.getOrDefault(emptyList())
        packages.forEach { runCommand("am", "force-stop", "--user", "0", it, results = results) }

        "optimized=${results.count { it == SUCCESS }} stopped=${packages.size}"
    }

    private fun runCommand(vararg command: String, results: MutableList<String>) {
        val process = runCatching { ShizukuController.exec(command.toList().toTypedArray()) }.getOrNull()
        if (process == null) {
            results += FAILURE
            return
        }
        results += if (runCatching { process.waitFor() == 0 }.getOrDefault(false)) SUCCESS else FAILURE
    }

    private const val SUCCESS = "ok"
    private const val FAILURE = "failed"
    private const val SHIZUKU_PACKAGE = "moe.shizuku.manager"
    private const val KSU_MANAGER_PACKAGE = "com.rifsxd.ksunext"
    private val PACKAGE_NAME = Regex("[A-Za-z0-9_]+(\\.[A-Za-z0-9_]+)+")
}
