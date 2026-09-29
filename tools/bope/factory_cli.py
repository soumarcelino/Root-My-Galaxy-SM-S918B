"""Transactional Samsung Odin factory firmware to BOPE target pipeline."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile

from .common import (
    atomic_text,
    executable,
    note,
    project_root,
    relative_or_absolute,
    require_file,
)
from .contract import derive_contract, parse_numeric_macros, verify_contract
from .errors import BopeError
from .factory import FactoryInfo, extract_factory_boot, inspect_factory
from .kernel import default_kallsyms_finder, default_vmlinux_to_elf, prepare_kernel
from .kernelsu import prepare_kernelsu
from .target import (
    app_profile,
    build_target,
    create_target_skeleton,
    file_record,
    integrate_app,
)


DEFAULT_BASE_ADDRESS = 0xFFFFFFC008000000


def _string_macro(header: Path, name: str) -> str:
    text = header.read_text(errors="replace")
    match = re.search(
        rf'^#define\s+{re.escape(name)}(?:\s+\\\n\s*|\s+)"([^"]+)"\s*$',
        text,
        re.MULTILINE,
    )
    if not match:
        raise BopeError(f"donor target is missing string macro {name}: {header}")
    return match.group(1)


def _donor_contract(donor: Path, explicit: Path | None) -> Path:
    if explicit:
        return require_file(explicit.expanduser().resolve(), "donor target contract")
    candidates = (
        donor / "brazilian-open-payload-engine/src/target.h",
        donor / "reference/kernel/modern-target/target.h",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise BopeError(
        "donor has no canonical target contract; pass --donor-contract"
    )


def _tool(name: str, explicit: Path | None) -> Path:
    return executable(name, explicit=explicit)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="bope-from-factory",
        description=(
            "Extract boot.img from a Samsung Odin factory package, derive and "
            "verify a BOPE target, audit KernelSU Next, and optionally bundle "
            "the result into the Android app."
        ),
    )
    parser.add_argument("factory", type=Path, help="Samsung Odin factory ZIP")
    parser.add_argument("--fingerprint", required=True, help="exact runtime target fingerprint")
    parser.add_argument("--device", default="dm3q")
    parser.add_argument("--donor", type=Path, required=True)
    parser.add_argument("--donor-contract", type=Path)
    parser.add_argument(
        "--engine-donor",
        type=Path,
        required=True,
        help="modern target containing the shared source-backed BOPE engine",
    )
    parser.add_argument("--donor-module", type=Path)
    parser.add_argument("--donor-btf", type=Path)
    parser.add_argument("--project", type=Path, default=project_root())
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--base-address", type=lambda value: int(value, 0))
    parser.add_argument("--kallsyms-finder", type=Path)
    parser.add_argument("--vmlinux-to-elf", type=Path)
    parser.add_argument("--unpack-bootimg", type=Path)
    parser.add_argument("--llvm-nm", type=Path)
    parser.add_argument("--pahole", type=Path)
    parser.add_argument("--lz4", type=Path)
    parser.add_argument("--ksu-source", type=Path)
    parser.add_argument("--ndk", type=Path)
    parser.add_argument("--skip-kernelsu", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--integrate-app", action="store_true")
    parser.add_argument("--keep-failed", action="store_true")
    return parser


def _write_readmes(target: Path, info: FactoryInfo, kernel_release: str) -> None:
    (target / "README.md").write_text(
        f"# {info.model} {info.post_build} generated target\n\n"
        f"Generated from the Samsung Odin factory package `{info.path.name}` "
        f"with `{info.pre_build}` as the calibration and KernelSU donor.\n\n"
        "Status: **WIP — device validation required before release.**\n"
    )
    (target / "firmware/README.md").write_text(
        f"# {info.post_build} firmware evidence\n\n"
        f"Factory archive: `{info.path}`. AP entry: `{info.ap_entry}`.\n\n"
        f"The streamed `{info.boot_entry}` produced the exact Android boot image "
        f"used to recover `{kernel_release}`. The boot image, decompressed kernel, "
        "and symbolized ELF are regenerable and ignored; kallsyms and BTF remain "
        "tracked port evidence.\n"
    )
    engine_readme = target / "brazilian-open-payload-engine/README.md"
    engine_readme.write_text(
        engine_readme.read_text().replace("tools/bope-from-ota", "tools/bope-from-factory")
    )


def _enforce_exact_runtime_identity(engine: Path) -> None:
    source = engine / "src/00_orchestrator.c"
    text = source.read_text()
    old = """  return uname(&info) == 0 &&
         strcmp(info.release, TARGET_KERNEL_RELEASE) == 0 &&
         property_equals(\"ro.product.model\", TARGET_MODEL) &&
         property_equals(\"ro.product.device\", TARGET_DEVICE) &&
         property_equals(\"ro.build.version.incremental\", TARGET_BUILD);
"""
    new = """  return uname(&info) == 0 &&
         strcmp(info.release, TARGET_KERNEL_RELEASE) == 0 &&
         strcmp(info.version, TARGET_KERNEL_VERSION) == 0 &&
         property_equals(\"ro.product.model\", TARGET_MODEL) &&
         property_equals(\"ro.product.device\", TARGET_DEVICE) &&
         property_equals(\"ro.build.version.incremental\", TARGET_BUILD) &&
         property_equals(\"ro.build.fingerprint\", TARGET_FINGERPRINT);
"""
    if new in text:
        return
    if old not in text:
        raise BopeError(
            "shared BOPE engine has an unknown runtime identity-check shape"
        )
    source.write_text(text.replace(old, new, 1))


def _write_manifest(
    target: Path,
    *,
    final_target: Path,
    project: Path,
    info: FactoryInfo,
    donor: Path,
    donor_contract: Path,
    engine_donor: Path,
    kernel: object,
    contract: object,
    verification: dict[str, object],
    kernelsu: dict[str, object] | None,
    build_artifacts: dict[str, Path],
    profile: dict[str, object] | None,
    compressed_boot: Path,
) -> Path:
    recorded_verification = dict(verification)
    recorded_verification["header"] = relative_or_absolute(contract.header, target)
    document = {
        "schema": 1,
        "status": "GENERATED_WIP_DEVICE_VALIDATION_REQUIRED",
        "target": {
            "slug": info.slug,
            "directory": relative_or_absolute(final_target, project),
            "model": info.model,
            "device": info.device,
            "build": info.post_build,
            "fingerprint": info.post_fingerprint,
            "android_version": info.android_version,
            "kernel_release": kernel.identity.release,
            "kernel_version_info": kernel.identity.version_info,
        },
        "source": {
            "donor_target": relative_or_absolute(donor, project),
            "donor_contract": file_record(donor_contract, project),
            "engine_donor": relative_or_absolute(engine_donor, project),
            "build": info.pre_build,
            "fingerprint": info.pre_fingerprint,
        },
        "factory": {
            "archive": file_record(info.path),
            "ap_entry": info.ap_entry,
            "boot_entry": info.boot_entry,
            "compressed_boot": file_record(compressed_boot, target),
        },
        "kernel": {
            "boot": file_record(kernel.boot, target),
            "kernel": file_record(kernel.kernel, target),
            "kallsyms": file_record(kernel.kallsyms, target),
            "elf": file_record(kernel.elf, target),
            "btf": file_record(kernel.btf, target),
            "base_address": f"0x{kernel.base_address:016x}",
        },
        "contract": {
            "derived_values": contract.derived,
            "changed_values": contract.changed,
            "inherited_calibration_values": contract.inherited,
            "verification": recorded_verification,
        },
        "kernelsu": kernelsu,
        "artifacts": {
            name: file_record(path, target)
            for name, path in build_artifacts.items()
        },
        "app_profile": profile,
    }
    path = target / "port-manifest.json"
    atomic_text(path, json.dumps(document, indent=2, sort_keys=True) + "\n")
    return path


def generate(args: argparse.Namespace) -> Path:
    project = args.project.expanduser().resolve()
    if not (project / "targets").is_dir():
        raise BopeError(f"not a Root My Galaxy checkout: {project}")
    donor = args.donor.expanduser().resolve()
    engine_donor = args.engine_donor.expanduser().resolve()
    donor_contract = _donor_contract(donor, args.donor_contract)
    donor_build = _string_macro(donor_contract, "TARGET_BUILD")
    donor_fingerprint = _string_macro(donor_contract, "TARGET_FINGERPRINT")
    donor_release = _string_macro(donor_contract, "TARGET_KERNEL_RELEASE")
    info = inspect_factory(
        args.factory.expanduser().resolve(),
        donor_build=donor_build,
        donor_fingerprint=donor_fingerprint,
        target_fingerprint=args.fingerprint,
        device=args.device,
    )
    output_root = (
        args.output_root.expanduser().resolve()
        if args.output_root
        else project / "targets"
    )
    output_root.mkdir(parents=True, exist_ok=True)
    final_target = output_root / f"{info.slug}-WIP"
    if final_target.exists():
        raise BopeError(
            f"target already exists; refusing to overwrite it: {final_target}"
        )

    llvm_nm = _tool("llvm-nm", args.llvm_nm)
    pahole = _tool("pahole", args.pahole)
    kallsyms_finder = (
        _tool("kallsyms-finder", args.kallsyms_finder)
        if args.kallsyms_finder
        else default_kallsyms_finder()
    )
    vmlinux_to_elf = (
        _tool("vmlinux-to-elf", args.vmlinux_to_elf)
        if args.vmlinux_to_elf
        else default_vmlinux_to_elf()
    )
    unpacker = (
        _tool("unpack_bootimg", args.unpack_bootimg)
        if args.unpack_bootimg
        else None
    )
    base_address = args.base_address
    if base_address is None:
        base_address = parse_numeric_macros(donor_contract).get(
            "TARGET_KIMAGE_TEXT_BASE", DEFAULT_BASE_ADDRESS
        )

    staging = Path(tempfile.mkdtemp(prefix=f".{info.slug}-WIP.", dir=output_root))
    staging.rmdir()
    try:
        note(f"factory: {info.post_build} ({info.device}, Android {info.android_version})")
        note(f"calibration and KernelSU donor: {donor}")
        note(f"shared BOPE engine donor: {engine_donor}")
        create_target_skeleton(engine_donor, staging, info)
        logs = staging / "port-logs"
        firmware = staging / "firmware"
        compressed_boot = firmware / f"boot_{info.slug.upper()}.img.lz4"
        boot = firmware / f"boot_{info.slug.upper()}.img"
        note("streaming boot.img.lz4 from the nested factory AP archive")
        extract_factory_boot(
            info,
            compressed_boot,
            boot,
            lz4=args.lz4,
        )
        ignore = staging / ".gitignore"
        ignore.write_text(ignore.read_text() + f"firmware/{compressed_boot.name}\n")
        kernel = prepare_kernel(
            boot,
            firmware,
            info.slug.upper(),
            base_address=base_address,
            unpacker=unpacker,
            kallsyms_finder=kallsyms_finder,
            vmlinux_to_elf=vmlinux_to_elf,
            pahole=pahole,
            logs_dir=logs,
        )
        note("deriving BOPE firmware addresses and layouts from ELF/BTF")
        contract = derive_contract(
            donor_contract,
            staging / "brazilian-open-payload-engine/src/target.h",
            target_name=info.slug,
            model=info.model,
            device=info.device,
            build=info.post_build,
            fingerprint=info.post_fingerprint,
            kernel_release=kernel.identity.release,
            kernel_version=kernel.identity.version_info,
            elf=kernel.elf,
            btf=kernel.btf,
            llvm_nm=llvm_nm,
            pahole=pahole,
        )
        verification = verify_contract(
            contract.header,
            elf=kernel.elf,
            btf=kernel.btf,
            llvm_nm=llvm_nm,
            pahole=pahole,
        )
        if not verification["passed"]:
            raise BopeError(
                "generated target contract failed verification: "
                + json.dumps(verification["errors"])
            )
        _enforce_exact_runtime_identity(
            staging / "brazilian-open-payload-engine"
        )
        _write_readmes(staging, info, kernel.identity.release)

        build_artifacts: dict[str, Path] = {}
        if not args.skip_build:
            build_artifacts = build_target(staging, ndk=args.ndk, logs=logs)

        kernelsu_report: dict[str, object] | None = None
        ksud: Path | None = None
        if not args.skip_kernelsu:
            kernelsu_report = prepare_kernelsu(
                donor,
                staging,
                slug=info.slug,
                donor_release=donor_release,
                target_release=kernel.identity.release,
                target_kallsyms=kernel.kallsyms,
                target_vmlinux=kernel.elf,
                target_btf=kernel.btf,
                llvm_nm=llvm_nm,
                pahole=pahole,
                ksu_source=args.ksu_source,
                ndk=args.ndk,
                logs_dir=logs,
                donor_module=(
                    args.donor_module.expanduser().resolve()
                    if args.donor_module
                    else None
                ),
                donor_btf=(
                    args.donor_btf.expanduser().resolve()
                    if args.donor_btf
                    else None
                ),
            )
            ksud = staging / str(kernelsu_report["ksud"]["path"])

        profile: dict[str, object] | None = None
        if build_artifacts and ksud:
            profile = app_profile(info, kernel, build_artifacts, ksud)
            atomic_text(
                staging / "app-profile.json",
                json.dumps(profile, indent=2, sort_keys=True) + "\n",
            )
        elif args.integrate_app:
            raise BopeError("--integrate-app requires both target build and KernelSU")

        _write_manifest(
            staging,
            final_target=final_target,
            project=project,
            info=info,
            donor=donor,
            donor_contract=donor_contract,
            engine_donor=engine_donor,
            kernel=kernel,
            contract=contract,
            verification=verification,
            kernelsu=kernelsu_report,
            build_artifacts=build_artifacts,
            profile=profile,
            compressed_boot=compressed_boot,
        )
        staging.rename(final_target)

        if args.integrate_app and profile and ksud:
            remapped = {
                name: final_target / path.relative_to(staging)
                for name, path in build_artifacts.items()
            }
            integrate_app(
                project,
                profile,
                remapped,
                final_target / ksud.relative_to(staging),
            )
        note(f"generated target: {final_target}")
        note("status: WIP; clean-boot device validation is still required")
        return final_target
    except Exception:
        if staging.exists() and not args.keep_failed:
            shutil.rmtree(staging)
        elif staging.exists():
            note(f"failed staging target kept at: {staging}")
        raise


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        generate(args)
    except (BopeError, OSError, ValueError) as error:
        print(f"bope-from-factory: error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
