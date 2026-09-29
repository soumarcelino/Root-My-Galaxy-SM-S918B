package dev.busung.s25uroot

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TargetProfileTest {
    private val profile = TargetProfile(
        profileId = "galaxy-s25-series-kernel-6.6.98",
        displayName = "Galaxy S25 series",
        models = setOf("SM-S931B", "SM-S938N"),
        kernelVersions = setOf("6.6.98"),
        exploit = RemoteArtifact("https://example.invalid/exploit", 1),
        kernelSu = RemoteArtifact("https://example.invalid/ksud", 1),
        helper = RemoteArtifact("https://example.invalid/helper", 1),
    )

    @Test
    fun matchesRegionalS25OnSameKernelVersion() {
        assertTrue(profile.matches(snapshot("SM-S931B", "6.6.98-android15-8-build-a")))
        assertTrue(profile.matches(snapshot("SM-S938N", "6.6.98-android15-8-build-b")))
    }

    @Test
    fun rejectsUnlistedModelOrKernelVersion() {
        assertFalse(profile.matches(snapshot("SM-S928B", "6.6.98-android15-8-build")))
        assertFalse(profile.matches(snapshot("SM-S938N", "6.6.102-android15-8-build")))
    }

    @Test
    fun detectsOnlyOneUi9PropertyRange() {
        assertTrue(snapshot("SM-S918B", "5.15.197-test").copy(oneUiVersion = 90_000).isOneUi9)
        assertTrue(snapshot("SM-S918B", "5.15.197-test").copy(oneUiVersion = 99_999).isOneUi9)
        assertFalse(snapshot("SM-S918B", "5.15.197-test").copy(oneUiVersion = 80_000).isOneUi9)
        assertFalse(snapshot("SM-S918B", "5.15.197-test").copy(oneUiVersion = 100_000).isOneUi9)
    }

    @Test
    fun exactFirmwareIgnoresRegionalFingerprint() {
        val device = snapshot("SM-S918B", "5.15.189-android13-8-33413713-abS918BXXSAFZF5").copy(
            buildId = "BP4A.251205.006.S918BXXSAFZF5",
            fingerprint = "samsung/dm3qxxx/dm3q:16/BP4A.251205.006/S918BXXSAFZF5:user/release-keys",
            kernelVersionInfo = "#1 SMP PREEMPT Tue Jun 9 09:47:44 UTC 2026",
        )
        val fzf5 = profile.copy(
            profileId = "dm3q-S918BXXSAFZF5",
            models = setOf("SM-S918B"),
            kernelVersions = setOf("5.15.189"),
            buildDisplays = setOf(device.buildId),
            fingerprints = setOf(device.fingerprint),
            kernelReleases = setOf(device.kernelRelease),
            kernelVersionInfos = setOf(device.kernelVersionInfo),
        )
        assertTrue(fzf5.matches(device))
        assertTrue(fzf5.matches(device.copy(
            fingerprint = device.fingerprint.replace("dm3qxxx", "dm3qxeea"),
        )))
        assertFalse(fzf5.matches(device.copy(model = "SM-S918N")))
        for (firmware in listOf("FZG1", "FZH3")) {
            assertFalse(fzf5.matches(device.copy(buildId = device.buildId.replace("FZF5", firmware))))
            assertFalse(fzf5.matches(device.copy(kernelRelease = device.kernelRelease.replace("FZF5", firmware))))
        }
        assertFalse(fzf5.matches(device.copy(kernelVersionInfo = "different build")))
    }

    @Test
    fun fzi1IdentityDoesNotRequireExactFingerprint() {
        val device = snapshot(
            "SM-S918B",
            "5.15.189-android13-8-33413713-abS918BXXSAFZI1",
        ).copy(
            buildId = "BP4A.251205.006.S918BXXSAFZI1",
            fingerprint = "samsung/dm3qxxx/dm3q:16/BP4A.251205.006/S918BXXSAFZI1:user/release-keys",
            kernelVersionInfo = "#1 SMP PREEMPT Mon Sep 7 11:01:09 UTC 2026",
        )
        val fzi1 = profile.copy(
            profileId = "dm3q-S918BXXSAFZI1-ksunext",
            models = setOf(device.model),
            kernelVersions = setOf(device.kernelVersion),
            buildDisplays = setOf(device.buildId),
            fingerprints = emptySet(),
            kernelReleases = setOf(device.kernelRelease),
            kernelVersionInfos = setOf(device.kernelVersionInfo),
        )

        assertTrue(fzi1.matches(device))
        assertTrue(fzi1.matches(device.copy(fingerprint = device.fingerprint.replace("FZI1", "FZH3"))))
    }

    @Test
    fun s916bFzf5MatchesOnlyItsExactKernelBuild() {
        val device = snapshot(
            "SM-S916B",
            "5.15.189-android13-8-33413713-abS916BXXSAFZF5",
        ).copy(
            buildId = "BP4A.251205.006.S916BXXSAFZF5",
            fingerprint = "samsung/dm2qxeea/dm2q:16/BP4A.251205.006/S916BXXSAFZF5:user/release-keys",
            kernelVersionInfo = "#1 SMP PREEMPT Tue Jun 9 09:38:52 UTC 2026",
        )
        val fzf5 = profile.copy(
            profileId = "dm2q-S916BXXSAFZF5-ksunext",
            models = setOf(device.model),
            kernelVersions = setOf(device.kernelVersion),
            buildDisplays = setOf(device.buildId),
            fingerprints = setOf(device.fingerprint),
            kernelReleases = setOf(device.kernelRelease),
            kernelVersionInfos = setOf(device.kernelVersionInfo),
        )

        assertTrue(fzf5.matches(device))
        assertFalse(fzf5.matches(device.copy(model = "SM-S918B")))
        assertFalse(fzf5.matches(device.copy(kernelRelease = device.kernelRelease.replace("FZF5", "FZG1"))))
        assertFalse(fzf5.matches(device.copy(kernelVersionInfo = "different build")))
    }

    private fun snapshot(
        model: String,
        kernelRelease: String,
    ) = DeviceSnapshot(
        manufacturer = "samsung",
        model = model,
        device = "unused",
        kernelRelease = kernelRelease,
        kernelVersionInfo = "",
        machine = "aarch64",
        buildId = "BP4A.251205.006.S938BCZG1",
        fingerprint = "samsung/example",
        androidRelease = "16",
        sdk = 36,
        abi = "arm64-v8a",
        pageSize = 4096,
    )
}
