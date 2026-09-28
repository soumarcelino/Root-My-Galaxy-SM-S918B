"""One-command Samsung OTA to generated BOPE target pipeline."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import tempfile

from .common import digest, executable, fail, note, project_root, require_file
from .contract import derive_contract, parse_numeric_macros, verify_contract
from .errors import BopeError
from .kernel import (
    apply_bsdiff,
    default_kallsyms_finder,
    default_vmlinux_to_elf,
    find_source_boot,
    prepare_kernel,
)
from .kernelsu import prepare_kernelsu
from .ota import OtaInfo, extract_patch, inspect_ota
from .target import (
    app_profile,
    build_target,
    create_target_skeleton,
    find_donor,
    integrate_app,
    write_firmware_readme,
    write_port_manifest,
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


def _validated_source_boot(path: Path, info: OtaInfo) -> Path:
    path = require_file(path.expanduser().resolve(), "source boot image")
    size = path.stat().st_size
    sha1 = digest(path, "sha1")
    if size != info.source_size or sha1 != info.source_sha1:
        raise BopeError(
            "source boot does not match OTA metadata: "
            f"size={size}/{info.source_size}, sha1={sha1}/{info.source_sha1}"
        )
    return path


def _discover_source_boot(info: OtaInfo, project: Path, donor: Path) -> Path:
    return find_source_boot(
        info.source_sha1,
        info.source_size,
        [donor / "firmware", project / "targets", info.path.parent, Path.home() / "Downloads"],
    )


def _tool(name: str, explicit: Path | None) -> Path:
    return executable(name, explicit=explicit)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="bope-from-ota",
        description=(
            "Reconstruct a Samsung target boot image from an incremental OTA, "
            "derive BOPE's ELF/BTF contract, audit KernelSU Next, build every "
            "artifact, and emit a new firmware target."
        ),
    )
    parser.add_argument("ota", type=Path, help="Samsung incremental OTA ZIP")
    parser.add_argument("--project", type=Path, default=project_root())
    parser.add_argument(
        "--output-root",
        type=Path,
        help="target parent directory (default: PROJECT/targets)",
    )
    parser.add_argument("--source-boot", type=Path, help="source-build boot.img")
    parser.add_argument("--donor", type=Path, help="source-build BOPE target")
    parser.add_argument("--base-address", type=lambda value: int(value, 0))
    parser.add_argument("--kallsyms-finder", type=Path)
    parser.add_argument("--vmlinux-to-elf", type=Path)
    parser.add_argument("--unpack-bootimg", type=Path)
    parser.add_argument("--llvm-nm", type=Path)
    parser.add_argument("--pahole", type=Path)
    parser.add_argument("--ksu-source", type=Path)
    parser.add_argument("--ndk", type=Path)
    parser.add_argument(
        "--skip-kernelsu",
        action="store_true",
        help="skip KernelSU retarget/audit/build (development only)",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="skip BOPE/helper compilation (development only)",
    )
    parser.add_argument(
        "--integrate-app",
        action="store_true",
        help="copy artifacts and append the generated profile to the Android app",
    )
    parser.add_argument(
        "--keep-failed",
        action="store_true",
        help="keep the hidden staging target when a pipeline gate fails",
    )
    return parser


def generate(args: argparse.Namespace) -> Path:
    project = args.project.expanduser().resolve()
    if not (project / "targets").is_dir():
        raise BopeError(f"not a Root My Galaxy checkout: {project}")
    info = inspect_ota(args.ota.expanduser().resolve())
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

    donor = (
        args.donor.expanduser().resolve()
        if args.donor
        else find_donor(project, info.pre_build)
    )
    donor_header = require_file(
        donor / "brazilian-open-payload-engine/src/target.h", "donor target contract"
    )
    donor_build = _string_macro(donor_header, "TARGET_BUILD")
    if donor_build != info.pre_build:
        raise BopeError(
            f"donor build {donor_build} does not match OTA source {info.pre_build}"
        )
    source_boot = (
        _validated_source_boot(args.source_boot, info)
        if args.source_boot
        else _discover_source_boot(info, project, donor)
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
        base_address = parse_numeric_macros(donor_header).get(
            "TARGET_KIMAGE_TEXT_BASE", DEFAULT_BASE_ADDRESS
        )

    staging = Path(
        tempfile.mkdtemp(prefix=f".{info.slug}-WIP.", dir=output_root)
    )
    staging.rmdir()
    try:
        note(f"OTA: {info.pre_build} -> {info.post_build} ({info.device})")
        note(f"donor: {donor}")
        note(f"source boot: {source_boot}")
        create_target_skeleton(donor, staging, info)
        logs = staging / "port-logs"
        firmware = staging / "firmware"

        patch = extract_patch(info, firmware / Path(info.patch_entry).name)
        boot = firmware / f"boot_{info.slug.upper()}.img"
        note("reconstructing target boot from the OTA BSDIFF40 patch")
        apply_bsdiff(
            source_boot,
            patch,
            boot,
            expected_size=info.target_size,
            expected_sha1=info.target_sha1,
        )
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
            donor_header,
            staging / "brazilian-open-payload-engine/src/target.h",
            target_name=info.slug,
            model=info.model,
            device=info.device,
            build=info.post_build,
            fingerprint=info.post_fingerprint,
            kernel_release=kernel.identity.release,
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
        write_firmware_readme(staging, info, kernel)

        build_artifacts: dict[str, Path] = {}
        if not args.skip_build:
            build_artifacts = build_target(staging, ndk=args.ndk, logs=logs)

        kernelsu_report: dict[str, object] | None = None
        ksud: Path | None = None
        if not args.skip_kernelsu:
            donor_release = _string_macro(donor_header, "TARGET_KERNEL_RELEASE")
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
            )
            ksud = staging / str(kernelsu_report["ksud"]["path"])

        profile: dict[str, object] | None = None
        if build_artifacts and ksud:
            profile = app_profile(info, kernel, build_artifacts, ksud)
            (staging / "app-profile.json").write_text(
                json.dumps(profile, indent=2, sort_keys=True) + "\n"
            )
        elif args.integrate_app:
            raise BopeError("--integrate-app requires both target build and KernelSU")

        write_port_manifest(
            staging,
            final_target=final_target,
            project=project,
            info=info,
            donor=donor,
            source_boot=source_boot,
            kernel=kernel,
            contract=contract,
            verification=verification,
            kernelsu=kernelsu_report,
            build_artifacts=build_artifacts,
            profile=profile,
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
        note("status: WIP; device validation is still required before promotion")
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
        fail(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
