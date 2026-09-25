#!/usr/bin/env python3
"""Run local repository checks without touching a device."""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys


def run(label: str, command: list[str], cwd: pathlib.Path) -> bool:
    proc = subprocess.run(command, cwd=cwd, text=True)
    print(f"{'PASS' if proc.returncode == 0 else 'FAIL'} {label}")
    return proc.returncode == 0


def check_links(repo: pathlib.Path) -> bool:
    pattern = re.compile(r"\[[^]]+\]\(([^)]+)\)")
    bad = []
    for path in [repo / "README.md", *sorted((repo / "docs").glob("*.md"))]:
        for target in pattern.findall(path.read_text(errors="replace")):
            if not target or target.startswith(("http://", "https://", "#", "/")):
                continue
            resolved = path.parent / target.split("#", 1)[0]
            if not resolved.exists():
                bad.append((path, target))
    for path, target in bad:
        print(f"BROKEN {path.relative_to(repo)} -> {target}")
    print(f"{'PASS' if not bad else 'FAIL'} markdown-links")
    return not bad


def check_secrets(repo: pathlib.Path) -> bool:
    pattern = re.compile(
        r"(gh[pousr]_[A-Za-z0-9]{20,}|github[_-]?token\s*[:=]|api[_-]?key\s*[:=]|"
        r"BEGIN (?:RSA|OPENSSH|EC) PRIVATE KEY)", re.I
    )
    bad = []
    for path in repo.rglob("*"):
        if not path.is_file() or ".git" in path.parts or path.stat().st_size > 2_000_000:
            continue
        try:
            text = path.read_text()
        except UnicodeDecodeError:
            continue
        if pattern.search(text):
            bad.append(path)
    for path in bad:
        print(f"SECRET_PATTERN {path.relative_to(repo)}")
    print(f"{'PASS' if not bad else 'FAIL'} secret-patterns")
    return not bad


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true", help="também executa make -B -j2 all so")
    parser.add_argument("--profile", type=pathlib.Path, default=pathlib.Path("tools/profiles/afzh3.json"))
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[1]
    ok = check_links(repo) & check_secrets(repo)

    for script in sorted((repo / "tools").glob("*.sh")):
        ok &= run(f"bash-n:{script.name}", ["bash", "-n", str(script)], repo)
    for script in sorted((repo / "tools").glob("*.py")):
        ok &= run(f"py-compile:{script.name}", [sys.executable, "-m", "py_compile", str(script)], repo)
    ok &= run("target-profile", [sys.executable, str(repo / "tools/audit-profile.py"),
                                  str(repo / args.profile)], repo)
    if shutil.which("shellcheck"):
        ok &= run("shellcheck", ["shellcheck", *map(str, sorted((repo / "tools").glob("*.sh")))], repo)
    else:
        print("SKIP shellcheck (não instalado)")
    if args.build:
        ok &= run("build", ["make", "-B", "-j2", "all", "so"], repo)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
