#!/usr/bin/env python3
"""Verify the Samsung KernelSU Next module inputs against the ZZHL target."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
TARGET = ROOT / "targets/zzhl-WIP"


def run(*args: str) -> str:
    return subprocess.run(
        args, check=True, text=True, stdout=subprocess.PIPE
    ).stdout


def undefined_symbols(module: Path) -> set[str]:
    result: set[str] = set()
    for line in run("llvm-nm", "-u", str(module)).splitlines():
        parts = line.split()
        if parts:
            result.add(parts[-1])
    return result


def kallsyms(path: Path) -> set[str]:
    result: set[str] = set()
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 3:
            result.add(parts[2])
    return result


def symvers(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 2:
            result[parts[1]] = parts[0].lower()
    return result


def module_versions(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in run("modprobe", "--dump-modversions", str(path)).splitlines():
        parts = line.split()
        if len(parts) >= 2:
            result[parts[1]] = parts[0].lower()
    return result


def btf_layout(path: Path, name: str) -> str:
    return run("pahole", "-F", "btf", "-C", name, str(path))


def report_path(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--module",
        type=Path,
        default=TARGET
        / "kernelsu-next/out/kernelsu-next-zzhl-v3.4.0/android13-5.15_kernelsu.ko",
    )
    parser.add_argument(
        "--reference-symvers",
        type=Path,
        default=ROOT
        / "targets/afzh3/kernelsu-next/out/afzh3-kernel/vmlinux.symvers",
    )
    parser.add_argument(
        "--reference-modules",
        type=Path,
        default=TARGET / "kernelsu-next/reference-modules",
    )
    parser.add_argument(
        "--reference-btf",
        type=Path,
        default=Path(
            "/home/matias/Projects/SM-S918B_16_Opensource/device-afzh3/vmlinux.btf"
        ),
    )
    args = parser.parse_args()

    required = [
        args.module,
        args.reference_symvers,
        args.reference_btf,
        TARGET / "firmware/kallsyms_ZZHL.kallsyms",
        TARGET / "firmware/vmlinux_ZZHL.btf",
    ]
    missing_files = [str(path) for path in required if not path.is_file()]
    if missing_files:
        raise SystemExit("missing inputs: " + ", ".join(missing_files))

    undefined = undefined_symbols(args.module)
    target_symbols = kallsyms(TARGET / "firmware/kallsyms_ZZHL.kallsyms")
    missing_symbols = sorted(undefined - target_symbols)

    reference = symvers(args.reference_symvers)
    observed: dict[str, set[str]] = {}
    module_files = sorted(args.reference_modules.rglob("*.ko"))
    for module in module_files:
        for name, crc in module_versions(module).items():
            observed.setdefault(name, set()).add(crc)
    common = set(reference) & set(observed)
    mismatches = {
        name: {"reference": reference[name], "zzhl": sorted(observed[name])}
        for name in sorted(common)
        if observed[name] != {reference[name]}
    }

    structures = (
        "cred",
        "task_struct",
        "work_struct",
        "completion",
        "ucounts",
        "user_struct",
        "user_namespace",
    )
    target_btf = TARGET / "firmware/vmlinux_ZZHL.btf"
    btf_mismatches = [
        name
        for name in structures
        if btf_layout(args.reference_btf, name) != btf_layout(target_btf, name)
    ]

    vermagic = run("modinfo", "-F", "vermagic", str(args.module)).strip()
    expected_release = "5.15.197-android13-8-34343818-abS918BXXUAZZHL"
    report = {
        "module": report_path(args.module),
        "sha256": hashlib.sha256(args.module.read_bytes()).hexdigest(),
        "vermagic": vermagic,
        "undefined_symbols": len(undefined),
        "missing_symbols": missing_symbols,
        "reference_modules": len(module_files),
        "common_symbol_crcs": len(common),
        "crc_mismatches": mismatches,
        "btf_structures": list(structures),
        "btf_mismatches": btf_mismatches,
        "passed": (
            not missing_symbols
            and bool(module_files)
            and not mismatches
            and not btf_mismatches
            and vermagic.startswith(expected_release + " ")
        ),
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
