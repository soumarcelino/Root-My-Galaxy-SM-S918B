"""Boot reconstruction and kernel-analysis helpers."""

from __future__ import annotations

import bz2
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct

from .common import digest, executable, note, run, run_logged
from .errors import BopeError


BTF_MAGIC = b"\x9f\xeb\x01\x00"


@dataclass(frozen=True)
class KernelIdentity:
    release: str
    version_info: str
    full_version: str


@dataclass(frozen=True)
class KernelArtifacts:
    boot: Path
    kernel: Path
    kallsyms: Path
    elf: Path
    btf: Path
    identity: KernelIdentity
    base_address: int


def _decode_bsdiff_offset(raw: bytes) -> int:
    if len(raw) != 8:
        raise BopeError("truncated BSDIFF40 integer")
    value = int.from_bytes(raw, "little")
    negative = value >> 63
    value &= (1 << 63) - 1
    return -value if negative else value


def apply_bsdiff(
    source_path: Path,
    patch_path: Path,
    output_path: Path,
    *,
    expected_size: int,
    expected_sha1: str,
) -> Path:
    source = source_path.read_bytes()
    patch = patch_path.read_bytes()
    if patch[:8] != b"BSDIFF40":
        magic = patch[:8].decode(errors="replace")
        raise BopeError(
            f"unsupported boot patch format {magic!r}; this version supports BSDIFF40"
        )
    control_size = _decode_bsdiff_offset(patch[8:16])
    diff_size = _decode_bsdiff_offset(patch[16:24])
    output_size = _decode_bsdiff_offset(patch[24:32])
    if min(control_size, diff_size, output_size) < 0:
        raise BopeError("negative BSDIFF40 block size")
    if output_size != expected_size:
        raise BopeError(
            f"OTA boot size mismatch: patch={output_size} metadata={expected_size}"
        )
    diff_at = 32 + control_size
    extra_at = diff_at + diff_size
    try:
        control = bz2.decompress(patch[32:diff_at])
        differences = bz2.decompress(patch[diff_at:extra_at])
        extra = bz2.decompress(patch[extra_at:])
    except OSError as error:
        raise BopeError(f"invalid BSDIFF40 bzip2 stream: {error}") from error

    output = bytearray(output_size)
    old_at = new_at = control_at = differences_at = extra_offset = 0
    while new_at < output_size:
        if control_at + 24 > len(control):
            raise BopeError("truncated BSDIFF40 control stream")
        add = _decode_bsdiff_offset(control[control_at : control_at + 8])
        copy = _decode_bsdiff_offset(control[control_at + 8 : control_at + 16])
        seek = _decode_bsdiff_offset(control[control_at + 16 : control_at + 24])
        control_at += 24
        if add < 0 or copy < 0 or new_at + add + copy > output_size:
            raise BopeError("invalid BSDIFF40 control tuple")
        if differences_at + add > len(differences) or extra_offset + copy > len(extra):
            raise BopeError("BSDIFF40 data stream is shorter than its control tuples")
        for index in range(add):
            source_index = old_at + index
            old_byte = source[source_index] if 0 <= source_index < len(source) else 0
            output[new_at + index] = (differences[differences_at + index] + old_byte) & 0xFF
        new_at += add
        old_at += add
        differences_at += add
        output[new_at : new_at + copy] = extra[extra_offset : extra_offset + copy]
        new_at += copy
        old_at += seek
        extra_offset += copy

    actual = hashlib.sha1(output).hexdigest()
    if actual != expected_sha1.lower():
        raise BopeError(f"reconstructed boot SHA-1 mismatch: {actual} != {expected_sha1}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    return output_path


def find_source_boot(
    expected_sha1: str,
    expected_size: int,
    roots: list[Path],
) -> Path:
    names = ("boot*.img", "boot.img")
    seen: set[Path] = set()
    for root in roots:
        root = root.expanduser()
        if not root.exists():
            continue
        candidates: list[Path] = []
        if root.is_file():
            candidates.append(root)
        else:
            for pattern in names:
                candidates.extend(root.rglob(pattern))
        for candidate in sorted(candidates):
            try:
                resolved = candidate.resolve()
                if resolved in seen or candidate.stat().st_size != expected_size:
                    continue
                seen.add(resolved)
                if digest(candidate, "sha1") == expected_sha1.lower():
                    return resolved
            except OSError:
                continue
    raise BopeError(
        "source boot image was not found by SHA-1; place it under targets/, "
        "beside the OTA, or pass --source-boot"
    )


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def _unpack_boot_v3_v4(boot: Path, output: Path) -> Path:
    header = boot.read_bytes()[:4096]
    if header[:8] != b"ANDROID!" or len(header) < 44:
        raise BopeError(f"unsupported Android boot image: {boot}")
    kernel_size, ramdisk_size = struct.unpack_from("<II", header, 8)
    header_size = struct.unpack_from("<I", header, 20)[0]
    header_version = struct.unpack_from("<I", header, 40)[0]
    if header_version not in (3, 4) or not 0 < header_size <= 4096:
        raise BopeError(
            f"internal unpacker supports boot header v3/v4, found v{header_version}"
        )
    kernel_offset = _align(header_size, 4096)
    with boot.open("rb") as source:
        source.seek(kernel_offset)
        kernel = source.read(kernel_size)
    if len(kernel) != kernel_size:
        raise BopeError("boot image contains a truncated kernel")
    output.mkdir(parents=True, exist_ok=True)
    kernel_path = output / "kernel"
    kernel_path.write_bytes(kernel)
    (output / "boot-info.txt").write_text(
        f"header_version={header_version}\nheader_size={header_size}\n"
        f"kernel_size={kernel_size}\nramdisk_size={ramdisk_size}\n"
    )
    return kernel_path


def unpack_boot(boot: Path, output: Path, unpacker: Path | None = None) -> Path:
    if output.exists():
        shutil.rmtree(output)
    located = shutil.which("unpack_bootimg") if unpacker is None else str(unpacker)
    if located:
        run([located, "--boot_img", boot, "--out", output], capture=True)
        kernel = output / "kernel"
        if kernel.is_file():
            return kernel
    return _unpack_boot_v3_v4(boot, output)


def extract_btf(kernel: Path, destination: Path, pahole: Path) -> Path:
    data = kernel.read_bytes()
    candidates: list[tuple[int, bytes]] = []
    at = 0
    while True:
        at = data.find(BTF_MAGIC, at)
        if at < 0:
            break
        if at + 24 <= len(data):
            magic, version, flags, header_len, type_off, type_len, str_off, str_len = struct.unpack_from(
                "<HBBIIIII", data, at
            )
            size = header_len + max(type_off + type_len, str_off + str_len)
            if (
                magic == 0xEB9F
                and version == 1
                and header_len >= 24
                and size > header_len
                and at + size <= len(data)
            ):
                candidates.append((at, data[at : at + size]))
        at += 4
    if not candidates:
        raise BopeError("no valid embedded BTF blob was found in the kernel")
    candidates.sort(key=lambda item: len(item[1]), reverse=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    for offset, blob in candidates:
        destination.write_bytes(blob)
        try:
            run([pahole, "-F", "btf", "-C", "task_struct", destination])
            note(f"embedded BTF extracted at kernel offset 0x{offset:x}")
            return destination
        except BopeError:
            continue
    destination.unlink(missing_ok=True)
    raise BopeError("embedded BTF candidates were rejected by pahole")


def kernel_identity(kernel: Path) -> KernelIdentity:
    data = kernel.read_bytes()
    match = re.search(rb"Linux version [0-9][^\0\r\n]{20,2048}", data)
    if not match:
        raise BopeError("kernel Linux version string was not found")
    full = match.group(0).decode(errors="replace")
    release_match = re.match(r"Linux version\s+(\S+)", full)
    version_match = re.search(r"(#\d+\s+SMP(?:\s+PREEMPT)?\s+.+)$", full)
    if not release_match or not version_match:
        raise BopeError(f"could not parse kernel identity: {full[:240]}")
    return KernelIdentity(
        release=release_match.group(1),
        version_info=version_match.group(1),
        full_version=full,
    )


def prepare_kernel(
    boot: Path,
    firmware_dir: Path,
    slug_upper: str,
    *,
    base_address: int,
    unpacker: Path | None,
    kallsyms_finder: Path,
    vmlinux_to_elf: Path,
    pahole: Path,
    logs_dir: Path,
) -> KernelArtifacts:
    unpacked = firmware_dir / "unpacked"
    note("unpacking reconstructed boot image")
    kernel = unpack_boot(boot, unpacked, unpacker)
    identity = kernel_identity(kernel)
    kallsyms = firmware_dir / f"kallsyms_{slug_upper}.kallsyms"
    elf = firmware_dir / f"vmlinux_{slug_upper}.elf"
    btf = firmware_dir / f"vmlinux_{slug_upper}.btf"

    note("recovering kallsyms from the raw kernel")
    run_logged(
        [
            kallsyms_finder,
            kernel,
            "--output",
            kallsyms,
            "--base-address",
            hex(base_address),
        ],
        logs_dir / "kallsyms-finder.log",
    )
    note("reconstructing the symbolized vmlinux ELF")
    run_logged(
        [vmlinux_to_elf, kernel, elf, "--base-address", hex(base_address)],
        logs_dir / "vmlinux-to-elf.log",
    )
    note("extracting and validating embedded BTF")
    extract_btf(kernel, btf, pahole)
    return KernelArtifacts(
        boot=boot,
        kernel=kernel,
        kallsyms=kallsyms,
        elf=elf,
        btf=btf,
        identity=identity,
        base_address=base_address,
    )


def default_kallsyms_finder() -> Path:
    configured = os.environ.get("BOPE_KALLSYMS_FINDER")
    return executable(
        "kallsyms-finder",
        explicit=Path(configured) if configured else None,
        fallbacks=[
            Path.home() / "Projects/build-do-firmware/dump/.venv/bin/kallsyms-finder"
        ],
    )


def default_vmlinux_to_elf() -> Path:
    configured = os.environ.get("BOPE_VMLINUX_TO_ELF")
    return executable(
        "vmlinux-to-elf",
        explicit=Path(configured) if configured else None,
        fallbacks=[
            Path.home() / "Projects/build-do-firmware/dump/.venv/bin/vmlinux-to-elf"
        ],
    )
