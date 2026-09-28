#!/usr/bin/env python3
"""Reject firmware-specific data outside src/target.h."""

from __future__ import annotations

import pathlib
import re


FIRMWARE_LITERALS = (
    "S918BXXUAZZHL",
    "5.15.197-android13-8-34343818-abS918BXXUAZZHL",
    "0xffffffc008000000",
    "0xffffff8000000000",
    "0xffffff9000000000",
    "0xfffffffe00000000",
    "0xfffffffe40000000",
    "0x02c05380",
    "0x02bfd1f8",
    "0x02010238",
    "0x02d8e600",
)


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    target = root / "src/target.h"
    compat = root / "src/target_zzhl.h"
    failures: list[str] = []

    if not target.is_file():
        print("FAIL missing src/target.h")
        return 1

    paths = [*(root / "src").rglob("*"), *(root / "tests").rglob("*")]
    for path in sorted(paths):
        if path.suffix not in {".c", ".h"} or path in {target, compat}:
            continue
        text = path.read_text(errors="replace")
        relative = path.relative_to(root)
        if "target_zzhl.h" in text:
            failures.append(f"{relative}: includes compatibility target")
        if re.search(r"\bZZHL_[A-Z0-9_]+\b", text):
            failures.append(f"{relative}: uses firmware-prefixed macro")
        if re.search(r"^\s*#\s*define\s+TARGET_[A-Z0-9_]+", text, re.M):
            failures.append(f"{relative}: defines TARGET_* outside target.h")
        lowered = text.lower()
        for literal in FIRMWARE_LITERALS:
            if literal.lower() in lowered:
                failures.append(f"{relative}: duplicates firmware literal {literal}")

    for failure in failures:
        print(f"FAIL {failure}")
    if failures:
        return 1
    print("PASS target boundary: firmware constants are isolated in src/target.h")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
