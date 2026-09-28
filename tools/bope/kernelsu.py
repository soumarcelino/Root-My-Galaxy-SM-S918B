"""Retarget and audit the KernelSU Next late-load bundle."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct

from elftools.elf.elffile import ELFFile

from .common import executable, note, run, run_logged
from .errors import BopeError


KSU_BTF_STRUCTURES = (
    "cred",
    "task_struct",
    "work_struct",
    "completion",
    "ucounts",
    "user_struct",
    "user_namespace",
)


def retarget_vermagic(source: Path, destination: Path, old_release: str, new_release: str) -> None:
    old = f"vermagic={old_release}".encode()
    new = f"vermagic={new_release}".encode()
    if len(old) != len(new):
        raise BopeError(
            "KernelSU vermagic releases differ in byte length; rebuild the module "
            "instead of applying an in-place retarget"
        )
    data = source.read_bytes()
    count = data.count(old)
    if count != 1:
        raise BopeError(f"expected one donor vermagic string, found {count}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data.replace(old, new, 1))


def _read_virtual_address(elf: ELFFile, address: int, size: int) -> bytes:
    for segment in elf.iter_segments():
        if segment["p_type"] != "PT_LOAD":
            continue
        start = segment["p_vaddr"]
        end = start + segment["p_filesz"]
        if start <= address and address + size <= end:
            elf.stream.seek(segment["p_offset"] + address - start)
            return elf.stream.read(size)
    raise BopeError(f"vmlinux address 0x{address:x} is not backed by PT_LOAD")


def target_symvers(vmlinux: Path) -> dict[str, int]:
    with vmlinux.open("rb") as stream:
        elf = ELFFile(stream)
        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            raise BopeError("vmlinux has no .symtab")
        symbols = {symbol.name: symbol["st_value"] for symbol in symtab.iter_symbols()}
        ranges = (
            (
                symbols["__start___ksymtab"],
                symbols["__stop___ksymtab"],
                symbols["__start___kcrctab"],
            ),
            (
                symbols["__start___ksymtab_gpl"],
                symbols["__stop___ksymtab_gpl"],
                symbols["__start___kcrctab_gpl"],
            ),
        )
        result: dict[str, int] = {}
        for name, address in symbols.items():
            if not name.startswith("__ksymtab_"):
                continue
            exported = name.removeprefix("__ksymtab_")
            for table_start, table_stop, crc_start in ranges:
                if table_start <= address < table_stop:
                    offset = address - table_start
                    if offset % 12:
                        raise BopeError(f"unaligned kernel_symbol entry: {exported}")
                    crc_address = crc_start + (offset // 12) * 4
                    result[exported] = struct.unpack(
                        "<I", _read_virtual_address(elf, crc_address, 4)
                    )[0]
                    break
        return result


def _undefined_symbols(module: Path, llvm_nm: Path) -> set[str]:
    symbols: set[str] = set()
    for line in run([llvm_nm, "-u", module]).splitlines():
        parts = line.split()
        if parts:
            symbols.add(parts[-1])
    return symbols


def _kallsyms(path: Path) -> set[str]:
    result: set[str] = set()
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 3:
            result.add(parts[2])
    return result


def _module_versions(module: Path, modprobe: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in run([modprobe, "--dump-modversions", module]).splitlines():
        parts = line.split()
        if len(parts) >= 2:
            result[parts[1]] = int(parts[0], 16)
    return result


def _btf_layout(path: Path, structure: str, pahole: Path) -> str:
    return run([pahole, "-F", "btf", "-C", structure, path])


def audit_module(
    module: Path,
    *,
    target_release: str,
    kallsyms: Path,
    vmlinux: Path,
    donor_btf: Path,
    target_btf: Path,
    llvm_nm: Path,
    pahole: Path,
    modinfo: Path,
    modprobe: Path,
) -> dict[str, object]:
    undefined = _undefined_symbols(module, llvm_nm)
    missing_symbols = sorted(undefined - _kallsyms(kallsyms))
    versions = _module_versions(module, modprobe)
    target_versions = target_symvers(vmlinux)
    crc_mismatches = {
        name: {
            "module": f"0x{versions[name]:08x}",
            "target": f"0x{target_versions[name]:08x}",
        }
        for name in sorted(versions.keys() & target_versions.keys())
        if versions[name] != target_versions[name]
    }
    missing_crc_targets = sorted(versions.keys() - target_versions.keys())
    btf_mismatches = [
        structure
        for structure in KSU_BTF_STRUCTURES
        if _btf_layout(donor_btf, structure, pahole)
        != _btf_layout(target_btf, structure, pahole)
    ]
    vermagic = run([modinfo, "-F", "vermagic", module]).strip()
    passed = (
        not missing_symbols
        and not crc_mismatches
        and not missing_crc_targets
        and not btf_mismatches
        and vermagic.startswith(target_release + " ")
    )
    return {
        "passed": passed,
        "module": str(module),
        "sha256": hashlib.sha256(module.read_bytes()).hexdigest(),
        "vermagic": vermagic,
        "undefined_symbols": len(undefined),
        "missing_symbols": missing_symbols,
        "module_version_entries": len(versions),
        "matched_symbol_crcs": len(versions.keys() & target_versions.keys()),
        "crc_mismatches": crc_mismatches,
        "missing_crc_targets": missing_crc_targets,
        "btf_structures": list(KSU_BTF_STRUCTURES),
        "btf_mismatches": btf_mismatches,
    }


def _find_donor_module(donor: Path) -> Path:
    candidates = sorted((donor / "kernelsu-next/out").glob("**/*kernelsu.ko"))
    if not candidates:
        raise BopeError(f"donor KernelSU module was not found under {donor}")
    return candidates[-1]


def _find_ksu_source(explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit.expanduser())
    configured = os.environ.get("BOPE_KSU_NEXT_SRC") or os.environ.get("KSU_NEXT_SRC")
    if configured:
        candidates.append(Path(configured).expanduser())
    candidates.extend(
        [
            Path.home() / "Projects/github/KernelSU-Next-v3.4.0-zzhl",
            Path.home() / "Projects/github/KernelSU-Next-v3.4.0",
        ]
    )
    for candidate in candidates:
        if (candidate / "userspace/ksud/Cargo.toml").is_file():
            return candidate.resolve()
    raise BopeError("KernelSU Next source was not found; pass --ksu-source")


def _find_ndk(explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit.expanduser())
    if os.environ.get("ANDROID_NDK_HOME"):
        candidates.append(Path(os.environ["ANDROID_NDK_HOME"]).expanduser())
    candidates.extend(
        [
            Path.home() / "Android/Sdk/ndk/28.2.13676358",
            Path("/opt/android-sdk/ndk/28.2.13676358"),
        ]
    )
    for candidate in candidates:
        if (candidate / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang").is_file():
            return candidate.resolve()
    raise BopeError("Android NDK was not found; set ANDROID_NDK_HOME")


def build_ksud(
    module: Path,
    destination: Path,
    *,
    ksu_source: Path | None,
    ndk: Path | None,
    log: Path,
) -> Path:
    source = _find_ksu_source(ksu_source)
    ndk_root = _find_ndk(ndk)
    cargo = executable("cargo")
    linker = ndk_root / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
    llvm_root = ndk_root / "toolchains/llvm/prebuilt/linux-x86_64"
    strip = llvm_root / "bin/llvm-strip"
    embedded = source / "userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko"
    embedded.parent.mkdir(parents=True, exist_ok=True)
    previous = embedded.read_bytes() if embedded.is_file() else None
    try:
        shutil.copyfile(module, embedded)
        os.utime(embedded, None)
        environment = os.environ.copy()
        environment["CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER"] = str(linker)
        environment["LIBCLANG_PATH"] = str(llvm_root / "lib")
        run_logged(
            [cargo, "build", "--locked", "--release", "--target", "aarch64-linux-android"],
            log,
            cwd=source / "userspace/ksud",
            env=environment,
        )
        built = source / "userspace/ksud/target/aarch64-linux-android/release/ksud"
        if not built.is_file():
            raise BopeError(f"KernelSU build completed without ksud: {built}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        run([strip, "--strip-all", "-o", destination, built])
        destination.chmod(0o755)
    finally:
        if previous is None:
            embedded.unlink(missing_ok=True)
        else:
            embedded.write_bytes(previous)
    return destination


def prepare_kernelsu(
    donor: Path,
    target: Path,
    *,
    slug: str,
    donor_release: str,
    target_release: str,
    target_kallsyms: Path,
    target_vmlinux: Path,
    target_btf: Path,
    llvm_nm: Path,
    pahole: Path,
    ksu_source: Path | None,
    ndk: Path | None,
    logs_dir: Path,
) -> dict[str, object]:
    donor_module = _find_donor_module(donor)
    donor_btf_candidates = sorted((donor / "firmware").glob("vmlinux_*.btf"))
    if len(donor_btf_candidates) != 1:
        raise BopeError(
            f"expected one donor BTF blob, found {len(donor_btf_candidates)} under "
            f"{donor / 'firmware'}"
        )
    version_match = re.search(r"v(\d+\.\d+\.\d+)", str(donor_module))
    version = version_match.group(1) if version_match else "3.4.0"
    output = target / f"kernelsu-next/out/kernelsu-next-{slug}-v{version}"
    module = output / "android13-5.15_kernelsu.ko"
    ksud = output / f"ksud-next-v{version}"
    note(f"retargeting KernelSU Next v{version} module vermagic")
    retarget_vermagic(donor_module, module, donor_release, target_release)
    report = audit_module(
        module,
        target_release=target_release,
        kallsyms=target_kallsyms,
        vmlinux=target_vmlinux,
        donor_btf=donor_btf_candidates[0],
        target_btf=target_btf,
        llvm_nm=llvm_nm,
        pahole=pahole,
        modinfo=executable("modinfo"),
        modprobe=executable("modprobe"),
    )
    if not report["passed"]:
        raise BopeError(
            "KernelSU compatibility audit failed: " + json.dumps(report, sort_keys=True)
        )
    note("building ksud with the retargeted module embedded")
    build_ksud(module, ksud, ksu_source=ksu_source, ndk=ndk, log=logs_dir / "ksud-build.log")
    report["version"] = version
    report["module"] = str(module.relative_to(target))
    report["ksud"] = {
        "path": str(ksud.relative_to(target)),
        "sha256": hashlib.sha256(ksud.read_bytes()).hexdigest(),
    }
    (output / "compatibility.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    (output / "SHA256SUMS").write_text(
        f"{hashlib.sha256(module.read_bytes()).hexdigest()}  {module.name}\n"
        f"{hashlib.sha256(ksud.read_bytes()).hexdigest()}  {ksud.name}\n"
    )
    return report
