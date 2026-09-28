#!/usr/bin/env python3
"""Synchronize and verify AFZH3 engine artifacts in the Android app."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import zipfile

PAYLOAD_ID = "dm3q-S918BXXSAFZH3-ksunext"
PAYLOAD_ASSET = "cve-2026-43499-app-afzh3.so"
FACTORY_ASSET = "mm-exec-factory-afzh3"
LAUNCHER_ASSET = "stability-launcher-afzh3"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_info(path: pathlib.Path) -> dict[str, object]:
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data), "sha256": sha256(data)}


def require_elf(path: pathlib.Path) -> None:
    if not path.is_file() or path.read_bytes()[:4] != b"\x7fELF":
        raise ValueError(f"ELF inválido ou ausente: {path}")


def copy_if_changed(source: pathlib.Path, destination: pathlib.Path) -> None:
    source_data = source.read_bytes()
    if not destination.is_file() or destination.read_bytes() != source_data:
        destination.write_bytes(source_data)
    destination.chmod(0o755)


def project_paths() -> dict[str, pathlib.Path]:
    engine = pathlib.Path(__file__).resolve().parents[1]
    project = pathlib.Path(__file__).resolve().parents[4]
    assets = project / "app/src/main/assets"
    return {
        "engine": engine,
        "project": project,
        "assets": assets,
        "manifest": assets / "targets-v3.json",
        "payload": engine / "build/payload.so",
        "factory": engine / "build/mm-exec-factory",
        "launcher": project / "stability-launcher/build/stability-launcher",
        "apk": project / "app/build/outputs/apk/debug/app-debug.apk",
    }


def target_profile(document: dict[str, object]) -> dict[str, object]:
    for profile in document.get("payloads", []):
        if profile.get("payloadId") == PAYLOAD_ID:
            return profile
    raise ValueError(f"perfil ausente no manifesto: {PAYLOAD_ID}")


def replace_size(text: str, asset: str, size: int) -> str:
    pattern = re.compile(
        rf'("url"\s*:\s*"asset://{re.escape(asset)}"\s*,\s*"size"\s*:\s*)\d+'
    )
    updated, count = pattern.subn(rf"\g<1>{size}", text, count=1)
    if count != 1:
        raise ValueError(f"size não encontrado para asset://{asset}")
    return updated


def ensure_factory_entry(text: str) -> str:
    document = json.loads(text)
    if "mmFactory" in target_profile(document):
        return text
    pattern = re.compile(
        rf'(?P<prefix>"payloadId"\s*:\s*"{re.escape(PAYLOAD_ID)}")'
        r'(?P<body>.*?)'
        r'(?P<block>"launcher"\s*:\s*\{\s*'
        r'"url"\s*:\s*"asset://stability-launcher-afzh3"\s*,\s*'
        r'"size"\s*:\s*\d+\s*\})',
        re.DOTALL,
    )
    replacement = (
        r'\g<prefix>\g<body>\g<block>,\n'
        '      "mmFactory": {\n'
        '        "url": "asset://mm-exec-factory-afzh3",\n'
        '        "size": 0\n'
        '      }'
    )
    updated, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise ValueError("não foi possível inserir mmFactory no perfil AFZH3")
    return updated


def sync(args: argparse.Namespace) -> dict[str, object]:
    require_elf(args.payload)
    require_elf(args.factory)
    require_elf(args.launcher)
    args.assets.mkdir(parents=True, exist_ok=True)
    destinations = {
        "payload": args.assets / PAYLOAD_ASSET,
        "factory": args.assets / FACTORY_ASSET,
        "launcher": args.assets / LAUNCHER_ASSET,
    }
    copy_if_changed(args.payload, destinations["payload"])
    copy_if_changed(args.factory, destinations["factory"])
    copy_if_changed(args.launcher, destinations["launcher"])

    original_text = args.manifest.read_text()
    text = ensure_factory_entry(original_text)
    text = replace_size(text, PAYLOAD_ASSET, destinations["payload"].stat().st_size)
    text = replace_size(text, FACTORY_ASSET, destinations["factory"].stat().st_size)
    text = replace_size(text, LAUNCHER_ASSET, destinations["launcher"].stat().st_size)
    json.loads(text)
    if text != original_text:
        args.manifest.write_text(text)
    return {
        "operation": "sync",
        "payload": file_info(destinations["payload"]),
        "factory": file_info(destinations["factory"]),
        "launcher": file_info(destinations["launcher"]),
        "manifest": str(args.manifest.resolve()),
    }


def verify(args: argparse.Namespace) -> tuple[dict[str, object], bool]:
    checks: dict[str, bool] = {}
    failures: list[str] = []
    local_paths = {
        PAYLOAD_ASSET: args.assets / PAYLOAD_ASSET,
        FACTORY_ASSET: args.assets / FACTORY_ASSET,
        LAUNCHER_ASSET: args.assets / LAUNCHER_ASSET,
    }
    for name, path in local_paths.items():
        checks[f"local_{name}_exists"] = path.is_file()
    if not args.apk.is_file():
        failures.append(f"APK ausente: {args.apk}")
        report = {"operation": "verify", "checks": checks, "failures": failures}
        return report, False

    apk_info = file_info(args.apk)
    embedded: dict[str, dict[str, object]] = {}
    try:
        with zipfile.ZipFile(args.apk) as archive:
            manifest_data = archive.read("assets/targets-v3.json")
            document = json.loads(manifest_data)
            profile = target_profile(document)
            for name, path in local_paths.items():
                data = archive.read(f"assets/{name}")
                embedded[name] = {"bytes": len(data), "sha256": sha256(data)}
                if path.is_file():
                    checks[f"apk_{name}_matches_local"] = sha256(data) == sha256(path.read_bytes())
            checks["manifest_payload_size"] = profile["exploit"]["size"] == embedded[PAYLOAD_ASSET]["bytes"]
            checks["manifest_factory_size"] = profile["mmFactory"]["size"] == embedded[FACTORY_ASSET]["bytes"]
            checks["manifest_launcher_size"] = profile["launcher"]["size"] == embedded[LAUNCHER_ASSET]["bytes"]
    except (KeyError, OSError, ValueError, zipfile.BadZipFile) as error:
        failures.append(str(error))

    installed_info = None
    if args.installed_apk:
        if args.installed_apk.is_file():
            installed_info = file_info(args.installed_apk)
            checks["installed_apk_matches_build"] = installed_info["sha256"] == apk_info["sha256"]
        else:
            failures.append(f"APK instalado ausente: {args.installed_apk}")

    failures.extend(name for name, passed in checks.items() if not passed)
    report = {
        "operation": "verify",
        "apk": apk_info,
        "installed_apk": installed_info,
        "embedded": embedded,
        "checks": checks,
        "failures": failures,
        "passed": not failures,
    }
    return report, not failures


def render(report: dict[str, object], output: pathlib.Path | None) -> None:
    text = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if output:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text)
        print(output)
    else:
        print(text, end="")


def main() -> int:
    defaults = project_paths()
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    sync_parser = subparsers.add_parser("sync", help="copia engine para assets e atualiza sizes")
    sync_parser.add_argument("--payload", type=pathlib.Path, default=defaults["payload"])
    sync_parser.add_argument("--factory", type=pathlib.Path, default=defaults["factory"])
    sync_parser.add_argument("--launcher", type=pathlib.Path, default=defaults["launcher"])
    sync_parser.add_argument("--assets", type=pathlib.Path, default=defaults["assets"])
    sync_parser.add_argument("--manifest", type=pathlib.Path, default=defaults["manifest"])
    sync_parser.add_argument("--output", type=pathlib.Path)

    verify_parser = subparsers.add_parser("verify", help="confere assets, manifesto e APK")
    verify_parser.add_argument("--apk", type=pathlib.Path, default=defaults["apk"])
    verify_parser.add_argument("--assets", type=pathlib.Path, default=defaults["assets"])
    verify_parser.add_argument("--installed-apk", type=pathlib.Path)
    verify_parser.add_argument("--output", type=pathlib.Path)

    args = parser.parse_args()
    try:
        if args.command == "sync":
            report = sync(args)
            passed = True
        else:
            report, passed = verify(args)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"erro: {error}", file=sys.stderr)
        return 1
    render(report, args.output)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
