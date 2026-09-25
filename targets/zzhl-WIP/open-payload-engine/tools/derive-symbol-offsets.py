#!/usr/bin/env python3
"""Derive kernel symbol offsets from a saved kallsyms snapshot."""

from __future__ import annotations

import argparse
import json
import pathlib


def parse_int(value: str) -> int:
    return int(value, 0)


def read_symbols(path: pathlib.Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            address = int(parts[0], 16)
        except ValueError:
            continue
        name = parts[2]
        if address and name not in symbols:
            symbols[name] = address
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kallsyms", type=pathlib.Path)
    base = parser.add_mutually_exclusive_group(required=True)
    base.add_argument("--base-symbol")
    base.add_argument("--base-address", type=parse_int)
    parser.add_argument("--symbol", action="append", default=[])
    parser.add_argument("--symbols-file", type=pathlib.Path)
    parser.add_argument("--format", choices=("json", "c"), default="json")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if not args.kallsyms.is_file():
        parser.error(f"kallsyms ausente: {args.kallsyms}")
    names = list(args.symbol)
    if args.symbols_file:
        names += [line.strip() for line in args.symbols_file.read_text().splitlines()
                  if line.strip() and not line.lstrip().startswith("#")]
    if not names:
        parser.error("informe ao menos um --symbol ou --symbols-file")
    symbols = read_symbols(args.kallsyms)
    if not symbols:
        parser.error("snapshot vazio, mascarado ou inválido")
    if args.base_symbol:
        if args.base_symbol not in symbols:
            parser.error(f"símbolo base ausente: {args.base_symbol}")
        base_address = symbols[args.base_symbol]
    else:
        base_address = args.base_address
    result = {}
    missing = []
    for name in names:
        if name not in symbols:
            missing.append(name)
            continue
        result[name] = {"address": f"0x{symbols[name]:016x}",
                        "offset": f"0x{symbols[name] - base_address:08x}"}
    if args.format == "json":
        text = json.dumps({"base": f"0x{base_address:016x}", "symbols": result,
                           "missing": missing}, indent=2, sort_keys=True) + "\n"
    else:
        lines = [f"/* base 0x{base_address:016x} */"]
        for name, data in result.items():
            macro = "KOFF_" + "".join(c if c.isalnum() else "_" for c in name).upper()
            lines.append(f"#define {macro} {data['offset']}ULL")
        for name in missing:
            lines.append(f"/* missing: {name} */")
        text = "\n".join(lines) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end="")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
