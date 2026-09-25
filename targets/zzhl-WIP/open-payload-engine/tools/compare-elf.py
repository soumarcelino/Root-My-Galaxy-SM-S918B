#!/usr/bin/env python3
"""Compare two ELF payloads using hashes, headers, sections, dependencies and strings."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True, errors="replace", stderr=subprocess.DEVNULL)


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_header(readelf: str, path: pathlib.Path) -> dict[str, str]:
    text = run(readelf, "-hW", str(path))
    result = {}
    for line in text.splitlines():
        if ":" in line:
            key, value = (part.strip() for part in line.split(":", 1))
            if key in {"Class", "Data", "Type", "Machine", "Entry point address"}:
                result[key] = value
    return result


def parse_sections(readelf: str, path: pathlib.Path) -> dict[str, int]:
    text = run(readelf, "-SW", str(path))
    result: dict[str, int] = {}
    for line in text.splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)", line)
        if match:
            result[match.group(1)] = int(match.group(2), 16)
    return result


def needed(readelf: str, path: pathlib.Path) -> list[str]:
    text = run(readelf, "-dW", str(path))
    return sorted(set(re.findall(r"Shared library: \[(.*?)\]", text)))


def imports(readelf: str, path: pathlib.Path) -> list[str]:
    text = run(readelf, "-Ws", str(path))
    names = set()
    for line in text.splitlines():
        if " UND " in line:
            name = line.split()[-1].split("@")[0]
            names.add(name)
    return sorted(names)


def strings(path: pathlib.Path, minimum: int) -> set[str]:
    tool = shutil.which("strings")
    if not tool:
        return set()
    return set(run(tool, "-a", "-n", str(minimum), str(path)).splitlines())


def describe(readelf: str, path: pathlib.Path, minimum: int) -> dict[str, object]:
    return {
        "path": str(path.resolve()),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
        "header": parse_header(readelf, path),
        "sections": parse_sections(readelf, path),
        "needed": needed(readelf, path),
        "imports": imports(readelf, path),
        "strings": strings(path, minimum),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--minimum-string", type=int, default=8)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    for path in (args.reference, args.candidate):
        if not path.is_file():
            parser.error(f"arquivo ausente: {path}")
    readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
    if not readelf:
        parser.error("readelf/llvm-readelf ausente")
    left = describe(readelf, args.reference, args.minimum_string)
    right = describe(readelf, args.candidate, args.minimum_string)
    left_strings = left.pop("strings")
    right_strings = right.pop("strings")
    left_sections = left["sections"]
    right_sections = right["sections"]
    section_diff = {
        name: {"reference": left_sections.get(name), "candidate": right_sections.get(name)}
        for name in sorted(set(left_sections) | set(right_sections))
        if left_sections.get(name) != right_sections.get(name)
    }
    report = {
        "reference": left,
        "candidate": right,
        "comparison": {
            "same_hash": left["sha256"] == right["sha256"],
            "header_equal": left["header"] == right["header"],
            "section_size_differences": section_diff,
            "imports_only_reference": sorted(set(left["imports"]) - set(right["imports"])),
            "imports_only_candidate": sorted(set(right["imports"]) - set(left["imports"])),
            "strings_common_count": len(left_strings & right_strings),
            "strings_only_reference_sample": sorted(left_strings - right_strings)[:100],
            "strings_only_candidate_sample": sorted(right_strings - left_strings)[:100],
        },
    }
    text = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
        print(args.output)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
