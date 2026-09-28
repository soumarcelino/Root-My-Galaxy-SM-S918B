#!/usr/bin/env python3
"""Retarget an equal-length kernel module vermagic without changing ELF layout."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--from-release", required=True)
    parser.add_argument("--to-release", required=True)
    args = parser.parse_args()

    source = f"vermagic={args.from_release}".encode()
    target = f"vermagic={args.to_release}".encode()
    if len(source) != len(target):
        parser.error("release strings must have equal byte length")

    data = args.input.read_bytes()
    occurrences = data.count(source)
    if occurrences != 1:
        parser.error(f"expected one vermagic occurrence, found {occurrences}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data.replace(source, target, 1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
