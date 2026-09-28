#!/usr/bin/env python3
"""Audit source literals against a versioned target profile."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=pathlib.Path)
    parser.add_argument("--repo", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--artifact", type=pathlib.Path)
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()
    if not args.profile.is_file():
        parser.error(f"perfil ausente: {args.profile}")
    profile = json.loads(args.profile.read_text())
    results = []
    passed = True
    for check in profile.get("checks", []):
        path = args.repo / check["file"]
        literal = check["literal"]
        minimum = int(check.get("minimum", 1))
        count = path.read_text(errors="replace").count(literal) if path.is_file() else 0
        ok = count >= minimum
        passed &= ok
        results.append({**check, "count": count, "pass": ok})

    artifact = None
    expected_hash = profile.get("validated_payload_sha256")
    if args.artifact:
        if not args.artifact.is_file():
            parser.error(f"artefato ausente: {args.artifact}")
        actual = hashlib.sha256(args.artifact.read_bytes()).hexdigest()
        artifact = {"path": str(args.artifact), "expected": expected_hash, "actual": actual,
                    "pass": not expected_hash or actual == expected_hash}
        passed &= artifact["pass"]

    report = {"profile": profile.get("name"), "pass": passed, "checks": results, "artifact": artifact}
    if args.as_json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        for item in results:
            state = "PASS" if item["pass"] else "FAIL"
            print(f"{state:4} {item['name']}: {item['file']} literal={item['literal']!r} count={item['count']}")
        if artifact:
            print(f"{'PASS' if artifact['pass'] else 'FAIL':4} payload_sha256: {artifact['actual']}")
        print("PASS profile" if passed else "FAIL profile")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
