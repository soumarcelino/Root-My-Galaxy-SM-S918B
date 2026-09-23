#!/usr/bin/env python3
"""Create a reproducible JSON manifest for payloads and related assets."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys


def run(*args: str, cwd: pathlib.Path | None = None) -> str | None:
    try:
        return subprocess.check_output(args, cwd=cwd, text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def digest(path: pathlib.Path) -> dict[str, object]:
    data = path.read_bytes()
    return {
        "path": str(path.resolve()),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "mode": oct(path.stat().st_mode & 0o777),
    }


def elf_header(path: pathlib.Path) -> dict[str, str]:
    readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
    if not readelf:
        return {}
    text = run(readelf, "-h", str(path)) or ""
    wanted = {"Class", "Data", "Type", "Machine", "Entry point address"}
    result: dict[str, str] = {}
    for line in text.splitlines():
        if ":" not in line:
            continue
        key, value = (part.strip() for part in line.split(":", 1))
        if key in wanted:
            result[key.lower().replace(" ", "_")] = value
    return result


def git_state(repo: pathlib.Path) -> dict[str, object]:
    commit = run("git", "rev-parse", "HEAD", cwd=repo)
    branch = run("git", "branch", "--show-current", cwd=repo)
    status = run("git", "status", "--porcelain", cwd=repo)
    return {"commit": commit, "branch": branch, "dirty": bool(status)}


def detect_cc(repo: pathlib.Path) -> str | None:
    if os.environ.get("CC"):
        return os.environ["CC"]
    ndk_candidates = [os.environ.get("ANDROID_NDK_HOME")]
    makefile = repo / "Makefile"
    if makefile.is_file():
        match = re.search(r"^ANDROID_NDK_FALLBACK\s*\?=\s*(.+)$",
                          makefile.read_text(), re.M)
        if match:
            ndk_candidates.append(
                match.group(1).strip().replace("$(HOME)", str(pathlib.Path.home()))
            )
    for ndk in filter(None, ndk_candidates):
        candidate = pathlib.Path(ndk) / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
        if candidate.is_file():
            return str(candidate)
    return shutil.which("aarch64-linux-android35-clang")


def parse_asset(value: str) -> tuple[str, pathlib.Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("asset deve usar NOME=CAMINHO")
    name, raw = value.split("=", 1)
    if not name or not raw:
        raise argparse.ArgumentTypeError("asset deve usar NOME=CAMINHO")
    return name, pathlib.Path(raw)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=pathlib.Path)
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--asset", action="append", default=[], type=parse_asset, metavar="NOME=ARQUIVO")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--target-build")
    args = parser.parse_args()

    if not args.artifact.is_file():
        parser.error(f"artefato ausente: {args.artifact}")
    files = {"payload": digest(args.artifact)}
    files["payload"]["elf"] = elf_header(args.artifact)
    for name, path in args.asset:
        if not path.is_file():
            parser.error(f"asset ausente: {path}")
        files[name] = digest(path)

    clang = detect_cc(args.repo.resolve())
    manifest = {
        "schema": 1,
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "target_build": args.target_build,
        "git": git_state(args.repo.resolve()),
        "toolchain": {
            "cc": clang,
            "cc_version": run(clang, "--version") if clang else None,
            "python": sys.version.split()[0],
        },
        "files": files,
    }
    text = json.dumps(manifest, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
        print(args.output)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
