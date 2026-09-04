package dev.busung.s25uroot

import org.json.JSONArray
import org.json.JSONObject

data class RemoteArtifact(
    val url: String,
    val size: Long,
)

data class TargetProfile(
    val profileId: String,
    val displayName: String,
    val models: Set<String>,
    val kernelVersions: Set<String>,
    val exploit: RemoteArtifact,
    val kernelSu: RemoteArtifact,
    val helper: RemoteArtifact,
    val buildDisplays: Set<String> = emptySet(),
    val fingerprints: Set<String> = emptySet(),
    val kernelReleases: Set<String> = emptySet(),
    val kernelVersionInfos: Set<String> = emptySet(),
) {
    init {
        require(models.isNotEmpty()) { "Payload must support at least one model" }
        require(kernelVersions.isNotEmpty()) { "Payload must support at least one kernel version" }
    }

    fun matchesDevice(snapshot: DeviceSnapshot): Boolean =
        models.any { it.equals(snapshot.model, ignoreCase = true) }

    fun matchesKernelVersion(snapshot: DeviceSnapshot): Boolean =
        snapshot.kernelVersion in kernelVersions

    fun matchesExactBuild(snapshot: DeviceSnapshot): Boolean =
        (buildDisplays.isEmpty() || snapshot.buildId in buildDisplays) &&
            (fingerprints.isEmpty() || snapshot.fingerprint in fingerprints) &&
            (kernelReleases.isEmpty() || snapshot.kernelRelease in kernelReleases) &&
            (kernelVersionInfos.isEmpty() || snapshot.kernelVersionInfo in kernelVersionInfos)

    fun matches(snapshot: DeviceSnapshot): Boolean =
        matchesDevice(snapshot) && matchesKernelVersion(snapshot) && matchesExactBuild(snapshot)

    val specificity: Int
        get() = listOf(buildDisplays, fingerprints, kernelReleases, kernelVersionInfos)
            .count { it.isNotEmpty() }

    val supportedModels: String
        get() = models.joinToString()

    val supportedKernelVersions: String
        get() = kernelVersions.joinToString()
}

data class SupportManifest(
    val schemaVersion: Int,
    val targets: List<TargetProfile>,
) {
    companion object {
        fun parse(bytes: ByteArray): SupportManifest {
            val root = JSONObject(bytes.toString(Charsets.UTF_8))
            val schemaVersion = root.getInt("schemaVersion")
            require(schemaVersion == 3) { "Unsupported support manifest schema" }
            val payloadsJson = root.getJSONArray("payloads")
            val payloads = buildList {
                for (index in 0 until payloadsJson.length()) {
                    val payload = payloadsJson.getJSONObject(index)
                    val exploit = payload.getJSONObject("exploit")
                    val kernelSu = payload.getJSONObject("kernelsu")
                    add(
                        TargetProfile(
                            profileId = payload.getString("payloadId"),
                            displayName = payload.getString("displayName"),
                            models = payload.getJSONArray("models").strings(),
                            kernelVersions = payload.getJSONArray("kernelVersions").strings(),
                            exploit = RemoteArtifact(
                                url = exploit.getString("url"),
                                size = exploit.getLong("size"),
                            ),
                            kernelSu = RemoteArtifact(
                                url = kernelSu.getString("url"),
                                size = kernelSu.getLong("size"),
                            ),
                            helper = payload.getJSONObject("helper").let { helper ->
                                RemoteArtifact(
                                    url = helper.getString("url"),
                                    size = helper.getLong("size"),
                                )
                            },
                            buildDisplays = payload.optJSONArray("buildDisplays").strings(),
                            fingerprints = payload.optJSONArray("fingerprints").strings(),
                            kernelReleases = payload.optJSONArray("kernelReleases").strings(),
                            kernelVersionInfos = payload.optJSONArray("kernelVersionInfos").strings(),
                        ),
                    )
                }
            }
            return SupportManifest(schemaVersion, payloads)
        }

        private fun JSONArray?.strings(): Set<String> = buildSet {
            this@strings?.let { array ->
                for (index in 0 until array.length()) add(array.getString(index))
            }
        }
    }
}
