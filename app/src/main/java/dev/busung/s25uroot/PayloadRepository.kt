package dev.busung.s25uroot

import android.content.Context
import android.system.Os
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import org.json.JSONObject

data class VerifiedPayloads(
    val profile: TargetProfile,
    val exploit: File,
    val kernelSu: File,
)

class PayloadRepository(private val context: Context) {
    fun loadTargets(): List<TargetProfile> {
        /* v0.2.24+: 完全离线——manifest 内嵌于 APK assets，不访问任何网络。 */
        val manifestBytes = context.assets.open("targets-v3.json").use { input ->
            input.readBytes()
        }
        return SupportManifest.parse(manifestBytes).targets
    }

    fun resolveTarget(snapshot: DeviceSnapshot): TargetProfile = loadTargets()
        .firstOrNull { it.matches(snapshot) }
        ?: error(context.getString(R.string.repo_no_profile))

    fun resolveTarget(profileId: String): TargetProfile = loadTargets()
        .firstOrNull { it.profileId == profileId }
        ?: error(context.getString(R.string.repo_profile_missing, profileId))

    fun download(profile: TargetProfile, onProgress: (String) -> Unit): VerifiedPayloads {
        val directory = File(context.filesDir, "payloads/${profile.profileId}").apply { mkdirs() }
        /* v0.2.28+: 强制使用内嵌 assets，绝不网络下载（杜绝版本漂移）。*/
        val exploit = bundledAssetFor(profile.exploit, directory, onProgress,
            context.getString(R.string.artifact_exploit_bundled))
            ?: error("bundled exploit missing: ${profile.exploit.url}")
        val kernelSu = bundledAssetFor(profile.kernelSu, directory, onProgress,
            context.getString(R.string.artifact_kernelsu_bundled))
            ?: error("bundled KernelSU missing: ${profile.kernelSu.url}")
        Os.chmod(exploit.absolutePath, 0b100100100)
        Os.chmod(kernelSu.absolutePath, 0b100100100)
        return VerifiedPayloads(profile, exploit, kernelSu)
    }

    private fun bundledAssetFor(
        artifact: RemoteArtifact,
        directory: File,
        onProgress: (String) -> Unit,
        label: String,
    ): File? {
        if (!artifact.url.startsWith(ASSET_PREFIX)) return null
        val name = artifact.url.removePrefix(ASSET_PREFIX)
        require(name.isNotBlank() && File(name).name == name) { "Invalid bundled asset name" }
        return bundledAsset(name, directory, onProgress, label)
    }

    /** 从 APK assets 解出内嵌文件；失败返回 null（由调用方 fallback 到下载）。 */
    private fun bundledAsset(name: String, directory: File, onProgress: (String) -> Unit, label: String): File? {
        return try {
            val destination = File(directory, name)
            // v0.2.36: 第二次运行修复——上次解出的文件被 chmod 0444（只读），
            // 再次 FileOutputStream 写入会 Permission denied。
            // 先删除旧文件（幂等），确保每次都能全新写入；删除失败则用 .tmp 新名兜底。
            if (destination.exists()) {
                if (!destination.delete()) {
                    // 只读文件删不掉（极端情况）→ 换新名写入，避免 Permission denied
                    val alt = File(directory, "${name}.${System.currentTimeMillis()}.tmp")
                    FileOutputStream(alt).use { output ->
                        context.assets.open(name).use { input -> input.copyTo(output) }
                        output.fd.sync()
                    }
                    onProgress(label)
                    return alt
                }
            }
            FileOutputStream(destination).use { output ->
                context.assets.open(name).use { input ->
                    input.copyTo(output)
                }
                output.fd.sync()
            }
            onProgress(label)
            destination
        } catch (e: Throwable) {
            null
        }
    }

    private fun downloadArtifact(
        artifact: RemoteArtifact,
        destination: File,
        label: String,
        onProgress: (String) -> Unit,
    ): File {
        onProgress(context.getString(R.string.repo_downloading, label))
        val temporary = File(destination.parentFile, "${destination.name}.part")
        val connection = open(artifact.url)
        require(connection.contentLengthLong == -1L || connection.contentLengthLong == artifact.size) {
            context.getString(R.string.repo_size_mismatch, label)
        }
        var total = 0L
        connection.inputStream.use { input ->
            FileOutputStream(temporary).use { output ->
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    total += count
                    require(total <= artifact.size) {
                        context.getString(R.string.repo_size_exceeded, label)
                    }
                    output.write(buffer, 0, count)
                }
                output.fd.sync()
            }
        }
        connection.disconnect()
        require(total == artifact.size) { context.getString(R.string.repo_incomplete, label) }
        if (destination.exists()) destination.delete()
        require(temporary.renameTo(destination)) {
            context.getString(R.string.repo_finalize_failed, label)
        }
        onProgress(context.getString(R.string.repo_verified, label))
        return destination
    }

    private fun resolveMainCommit(): String {
        val response = downloadBytes(COMMIT_API_URL, MAX_COMMIT_RESPONSE_BYTES)
        val commit = JSONObject(response.toString(Charsets.UTF_8))
            .getJSONObject("object")
            .getString("sha")
        require(commit.matches(Regex("[0-9a-f]{40}"))) { context.getString(R.string.repo_commit_invalid) }
        return commit
    }

    private fun rawUrl(commit: String, path: String) =
        "$RAW_REPOSITORY@$commit/$path"

    private fun pinArtifactUrl(url: String, commit: String): String {
        require(url.startsWith(MUTABLE_RAW_PREFIX)) { context.getString(R.string.repo_url_invalid) }
        return "$RAW_REPOSITORY@$commit/${url.removePrefix(MUTABLE_RAW_PREFIX)}"
    }

    private fun downloadBytes(url: String, maximum: Int): ByteArray {
        val connection = open(url)
        val bytes = connection.inputStream.use { input ->
            val output = ByteArrayOutputStream()
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                require(output.size() + count <= maximum) {
                    context.getString(R.string.repo_response_too_large)
                }
                output.write(buffer, 0, count)
            }
            output.toByteArray()
        }
        connection.disconnect()
        return bytes
    }

    private fun open(url: String): HttpURLConnection =
        (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 15_000
            readTimeout = 60_000
            instanceFollowRedirects = true
            setRequestProperty("User-Agent", "S25URoot/${BuildConfig.VERSION_NAME}")
            connect()
            require(responseCode == HttpURLConnection.HTTP_OK) { "HTTP $responseCode" }
        }

    companion object {
        private const val ASSET_PREFIX = "asset://"
        private const val COMMIT_API_URL =
            "https://api.github.com/repos/youyoudezhuzhu/rmg-f731u/git/ref/heads/main"
        /* v0.2.21: raw.githubusercontent.com 在大陆常被 CDN 缓存旧文件，
         * 导致 App 永远拉到旧 exploit（校验 size 相同但内容旧）。
         * 改用 jsdelivr CDN（全球节点、无污染缓存、commit-pinned 不可变）。 */
        private const val RAW_REPOSITORY =
            "https://cdn.jsdelivr.net/gh/youyoudezhuzhu/rmg-f731u"
        private const val MUTABLE_RAW_PREFIX = "$RAW_REPOSITORY@main/"
        private const val MAX_COMMIT_RESPONSE_BYTES = 16 * 1024
        private const val MAX_MANIFEST_BYTES = 256 * 1024
    }
}
