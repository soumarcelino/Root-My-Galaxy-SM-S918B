#!/usr/bin/env python3
"""Synchronize and verify the validated ZZHL artifacts in the Android app."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile


PROFILE_ID = "dm3q-S918BXXUAZZHL-ksunext"
ARTIFACTS = {
    "exploit": "cve-2026-43499-app-zzhl.so",
    "kernelsu": "ksud-next-v3.4.0-zzhl",
    "helper": "cve-2026-43499-root-zzhl",
    "launcher": "stability-launcher-zzhl",
    "mmFactory": "mm-exec-factory-zzhl",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def paths() -> dict[str, Path]:
    engine = Path(__file__).resolve().parents[1]
    target = engine.parent
    project = Path(__file__).resolve().parents[4]
    return {
        "project": project,
        "assets": project / "app/src/main/assets",
        "manifest": project / "app/src/main/assets/targets-v3.json",
        "apk": project / "app/build/outputs/apk/debug/app-debug.apk",
        "exploit": engine / "build/payload.so",
        "kernelsu": target
        / "kernelsu-next/out/kernelsu-next-zzhl-v3.4.0/ksud-next-v3.4.0",
        "helper": target / "helper/build/cve-2026-43499-root",
        "launcher": engine / "build/stability-launcher-zzhl",
        "mmFactory": engine / "build/mm-exec-factory",
    }


def profile(document: dict[str, object]) -> dict[str, object]:
    for item in document.get("payloads", []):
        if item.get("payloadId") == PROFILE_ID:
            return item
    raise ValueError(f"perfil ausente: {PROFILE_ID}")


def require_elf(path: Path) -> None:
    if not path.is_file() or path.read_bytes()[:4] != b"\x7fELF":
        raise ValueError(f"ELF inválido ou ausente: {path}")


def sync(args: argparse.Namespace) -> dict[str, object]:
    document = json.loads(args.manifest.read_text())
    selected = profile(document)
    report: dict[str, object] = {"operation": "sync", "profile": PROFILE_ID}
    for field, asset in ARTIFACTS.items():
        source = getattr(args, field)
        require_elf(source)
        destination = args.assets / asset
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        destination.chmod(0o755)
        data = destination.read_bytes()
        selected[field] = {"url": f"asset://{asset}", "size": len(data)}
        report[field] = {"bytes": len(data), "sha256": digest(data)}
    args.manifest.write_text(json.dumps(document, indent=2) + "\n")
    return report


def verify(args: argparse.Namespace) -> tuple[dict[str, object], bool]:
    failures: list[str] = []
    embedded: dict[str, object] = {}
    if not args.apk.is_file():
        return {"operation": "verify", "failures": [f"APK ausente: {args.apk}"]}, False
    with zipfile.ZipFile(args.apk) as archive:
        document = json.loads(archive.read("assets/targets-v3.json"))
        selected = profile(document)
        for field, asset in ARTIFACTS.items():
            local = args.assets / asset
            apk_data = archive.read(f"assets/{asset}")
            local_data = local.read_bytes()
            info = selected.get(field, {})
            passed = (
                apk_data == local_data
                and info.get("url") == f"asset://{asset}"
                and info.get("size") == len(apk_data)
            )
            embedded[field] = {
                "bytes": len(apk_data),
                "sha256": digest(apk_data),
                "passed": passed,
            }
            if not passed:
                failures.append(field)
    report = {
        "operation": "verify",
        "profile": PROFILE_ID,
        "apk_sha256": digest(args.apk.read_bytes()),
        "embedded": embedded,
        "failures": failures,
        "passed": not failures,
    }
    return report, not failures


def main() -> int:
    default = paths()
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    sync_parser = subparsers.add_parser("sync")
    for field in ARTIFACTS:
        sync_parser.add_argument(f"--{field}", type=Path, default=default[field])
    sync_parser.add_argument("--assets", type=Path, default=default["assets"])
    sync_parser.add_argument("--manifest", type=Path, default=default["manifest"])
    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("--assets", type=Path, default=default["assets"])
    verify_parser.add_argument("--apk", type=Path, default=default["apk"])
    args = parser.parse_args()
    try:
        if args.command == "sync":
            report, passed = sync(args), True
        else:
            report, passed = verify(args)
    except (KeyError, OSError, ValueError, json.JSONDecodeError, zipfile.BadZipFile) as error:
        print(f"erro: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
