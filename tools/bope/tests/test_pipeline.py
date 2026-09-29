from __future__ import annotations

import bz2
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

from tools.bope.contract import field_offset, structure_size
from tools.bope.errors import BopeError
from tools.bope.factory import extract_factory_member, inspect_factory
from tools.bope.factory_cli import _enforce_exact_runtime_identity
from tools.bope.kernel import apply_bsdiff
from tools.bope.ota import inspect_ota
from tools.bope.target import _kernel_version


def _off(value: int) -> bytes:
    if value < 0:
        value = -value | (1 << 63)
    return value.to_bytes(8, "little")


class OtaInspectionTests(unittest.TestCase):
    def test_selects_boot_patch_without_matching_bootdevice_decoy(self) -> None:
        metadata = "\n".join(
            (
                "pre-build-incremental=S918BXXUAZZHL",
                "post-build-incremental=S918BXXUAZZI8",
                "pre-build=samsung/dm3q/dm3q:17/id/ZZHL:user/release-keys",
                "post-build=samsung/dm3q/dm3q:17/id/ZZI8:user/release-keys",
                "pre-device=dm3q",
            )
        )
        updater = '''
patch_partition("EMMC:/dev/block/bootdevice/by-name/apnhlos:20:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                "EMMC:/dev/block/bootdevice/by-name/apnhlos:10:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                package_extract_file("NON-HLOS.bin.p")) || abort();
patch_partition(concat("EMMC:","/dev/block/bootdevice/by-name/boot",":30:1111111111111111111111111111111111111111"),
                concat("EMMC:","/dev/block/bootdevice/by-name/boot",":30:2222222222222222222222222222222222222222"),
                package_extract_file("boot.img.p")) || abort();
'''
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "ota.zip"
            with zipfile.ZipFile(archive, "w") as ota:
                ota.writestr("META-INF/com/android/metadata", metadata)
                ota.writestr("META-INF/com/google/android/updater-script", updater)
                ota.writestr("NON-HLOS.bin.p", b"decoy")
                ota.writestr("boot.img.p", b"boot")
            result = inspect_ota(archive)
        self.assertEqual(result.patch_entry, "boot.img.p")
        self.assertEqual(result.target_size, 30)
        self.assertEqual(result.target_sha1, "1" * 40)
        self.assertEqual(result.source_sha1, "2" * 40)
        self.assertEqual(result.slug, "zzi8")
        self.assertEqual(result.model, "SM-S918B")


class FactoryInspectionTests(unittest.TestCase):
    donor_build = "S918BXXSAFZH3"
    donor_fingerprint = (
        "samsung/dm3qxxx/dm3q:16/BP4A.251205.006/"
        "S918BXXSAFZH3:user/release-keys"
    )
    target_fingerprint = (
        "samsung/dm3qxxx/dm3q:16/BP4A.251205.006/"
        "S918BXXSAFZI1:user/release-keys"
    )

    @staticmethod
    def _archive(path: Path, payload: bytes = b"lz4-boot") -> None:
        nested = io.BytesIO()
        with tarfile.open(fileobj=nested, mode="w") as ap:
            member = tarfile.TarInfo("boot.img.lz4")
            member.size = len(payload)
            ap.addfile(member, io.BytesIO(payload))
        with zipfile.ZipFile(path, "w") as factory:
            factory.writestr(
                "AP_S918BXXSAFZI1_S918BXXSAFZI1_MQB1_"
                "REV00_user_low_ship_MULTI_CERT_meta_OS16.tar.md5",
                nested.getvalue(),
            )

    def test_inspects_factory_identity_and_streams_exact_boot_member(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "factory.zip"
            self._archive(archive)
            info = inspect_factory(
                archive,
                donor_build=self.donor_build,
                donor_fingerprint=self.donor_fingerprint,
                target_fingerprint=self.target_fingerprint,
                device="dm3q",
            )
            output = extract_factory_member(
                info, root / "boot.img.lz4", member_name=info.boot_entry
            )
            self.assertEqual(output.read_bytes(), b"lz4-boot")
        self.assertEqual(info.post_build, "S918BXXSAFZI1")
        self.assertEqual(info.android_version, "16")
        self.assertEqual(info.slug, "fzi1")
        self.assertEqual(info.model, "SM-S918B")

    def test_rejects_fingerprint_from_another_build(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "factory.zip"
            self._archive(archive)
            with self.assertRaises(BopeError):
                inspect_factory(
                    archive,
                    donor_build=self.donor_build,
                    donor_fingerprint=self.donor_fingerprint,
                    target_fingerprint=self.donor_fingerprint,
                    device="dm3q",
                )

    def test_factory_target_checks_fingerprint_and_kernel_version(self) -> None:
        source = '''static int target_matches(void) {
  struct utsname info;
  return uname(&info) == 0 &&
         strcmp(info.release, TARGET_KERNEL_RELEASE) == 0 &&
         property_equals("ro.product.model", TARGET_MODEL) &&
         property_equals("ro.product.device", TARGET_DEVICE) &&
         property_equals("ro.build.version.incremental", TARGET_BUILD);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            engine = Path(temporary)
            (engine / "src").mkdir()
            orchestrator = engine / "src/00_orchestrator.c"
            orchestrator.write_text(source)
            _enforce_exact_runtime_identity(engine)
            hardened = orchestrator.read_text()
        self.assertIn("TARGET_KERNEL_VERSION", hardened)
        self.assertIn('"ro.build.fingerprint", TARGET_FINGERPRINT', hardened)


class BsdiffTests(unittest.TestCase):
    def test_applies_standard_bsdiff40_patch(self) -> None:
        source = b"abc"
        expected = b"abd!"
        control = _off(3) + _off(1) + _off(0)
        differences = bytes((0, 0, 1))
        patch = (
            b"BSDIFF40"
            + _off(len(bz2.compress(control)))
            + _off(len(bz2.compress(differences)))
            + _off(len(expected))
            + bz2.compress(control)
            + bz2.compress(differences)
            + bz2.compress(b"!")
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "source.img").write_bytes(source)
            (root / "boot.img.p").write_bytes(patch)
            output = apply_bsdiff(
                root / "source.img",
                root / "boot.img.p",
                root / "target.img",
                expected_size=len(expected),
                expected_sha1=hashlib.sha1(expected).hexdigest(),
            )
            self.assertEqual(output.read_bytes(), expected)


class BtfParserTests(unittest.TestCase):
    def test_reads_regular_array_and_function_pointer_members(self) -> None:
        layout = '''
struct example {
        int pid;                    /*     8     4 */
        char comm[16];              /*    12    16 */
        long int (*read)(void *);   /*    32     8 */
};
/* size: 40, cachelines: 1, members: 3 */
'''
        self.assertEqual(field_offset(layout, "pid"), 8)
        self.assertEqual(field_offset(layout, "comm"), 12)
        self.assertEqual(field_offset(layout, "read"), 32)
        self.assertEqual(structure_size(layout), 40)


class AppProfileTests(unittest.TestCase):
    def test_extracts_semantic_kernel_version_from_full_release(self) -> None:
        self.assertEqual(
            _kernel_version("5.15.189-android13-8-33413713-abS918BXXSAFZI1"),
            "5.15.189",
        )

    def test_rejects_unstructured_kernel_release(self) -> None:
        with self.assertRaises(BopeError):
            _kernel_version("android13-5.15")


if __name__ == "__main__":
    unittest.main()
