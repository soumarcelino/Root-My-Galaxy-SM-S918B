from __future__ import annotations

import bz2
import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile

from tools.bope.contract import field_offset, structure_size
from tools.bope.kernel import apply_bsdiff
from tools.bope.ota import inspect_ota


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


if __name__ == "__main__":
    unittest.main()
