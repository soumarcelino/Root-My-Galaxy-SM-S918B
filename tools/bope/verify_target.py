#!/usr/bin/env python3
"""Verify a generated BOPE target contract against its kernel ELF and BTF."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .common import executable
from .contract import verify_contract


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--btf", required=True, type=Path)
    parser.add_argument("--llvm-nm", type=Path)
    parser.add_argument("--pahole", type=Path)
    args = parser.parse_args(argv)
    report = verify_contract(
        args.header,
        elf=args.elf,
        btf=args.btf,
        llvm_nm=executable("llvm-nm", explicit=args.llvm_nm),
        pahole=executable("pahole", explicit=args.pahole),
    )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
