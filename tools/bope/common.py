"""Common subprocess, hashing, and filesystem helpers."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Iterable

from .errors import BopeError


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def note(message: str) -> None:
    print(f"[BOPE] {message}", flush=True)


def digest(path: Path, algorithm: str = "sha256") -> str:
    hasher = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def require_file(path: Path, label: str | None = None) -> Path:
    if not path.is_file():
        raise BopeError(f"missing {label or 'file'}: {path}")
    return path


def executable(
    name: str,
    *,
    explicit: Path | None = None,
    fallbacks: Iterable[Path] = (),
) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit.expanduser())
    located = shutil.which(name)
    if located:
        candidates.append(Path(located))
    candidates.extend(path.expanduser() for path in fallbacks)
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            # Frontends such as modprobe/modinfo are symlinks to kmod and use
            # argv[0] to select their operation. Keep the discovered basename.
            return candidate.absolute()
    raise BopeError(
        f"required executable '{name}' was not found; pass its explicit path"
    )


def run(
    command: Iterable[str | os.PathLike[str]],
    *,
    cwd: Path | None = None,
    capture: bool = True,
    env: dict[str, str] | None = None,
) -> str:
    rendered = [str(part) for part in command]
    try:
        process = subprocess.run(
            rendered,
            cwd=cwd,
            env=env,
            check=True,
            text=True,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.PIPE if capture else None,
        )
    except FileNotFoundError as error:
        raise BopeError(f"command not found: {rendered[0]}") from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or "").strip()
        suffix = f"\n{detail}" if detail else ""
        raise BopeError(f"command failed ({error.returncode}): {' '.join(rendered)}{suffix}") from error
    return process.stdout or ""


def run_logged(
    command: Iterable[str | os.PathLike[str]],
    log: Path,
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> None:
    rendered = [str(part) for part in command]
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as stream:
        process = subprocess.run(
            rendered,
            cwd=cwd,
            env=env,
            text=True,
            stdout=stream,
            stderr=subprocess.STDOUT,
        )
    if process.returncode:
        tail = "\n".join(log.read_text(errors="replace").splitlines()[-30:])
        raise BopeError(
            f"command failed ({process.returncode}): {' '.join(rendered)}\n{tail}"
        )


def atomic_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(text)
    temporary.replace(path)


def relative_or_absolute(path: Path, base: Path) -> str:
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path.resolve())


def fail(message: str) -> "None":
    print(f"bope-from-ota: error: {message}", file=sys.stderr)
    raise SystemExit(1)
