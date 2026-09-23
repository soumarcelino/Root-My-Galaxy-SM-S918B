#!/usr/bin/env python3
"""Summarize a long clean-boot campaign and rank stability profiles."""

from __future__ import annotations

import argparse
import json
import pathlib
import statistics


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("campaign", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    records = []
    for path in sorted(args.campaign.glob("boot-*/record.json")):
        try:
            records.append(json.loads(path.read_text()))
        except (OSError, ValueError):
            continue

    profiles: dict[str, list[dict]] = {}
    for record in records:
        profiles.setdefault(record["profile"], []).append(record)

    ranked = []
    for name, items in profiles.items():
        durations = [item["duration_seconds"] for item in items]
        passed = sum(bool(item["pass"]) for item in items)
        ranked.append({
            "profile": name,
            "boots": len(items),
            "passes": passed,
            "pass_rate": passed / len(items),
            "median_seconds": statistics.median(durations),
            "mean_seconds": statistics.fmean(durations),
        })
    ranked.sort(key=lambda item: (-item["pass_rate"], item["median_seconds"], item["profile"]))

    output = {
        "completed_boots": len(records),
        "passes": sum(bool(item["pass"]) for item in records),
        "failures": sum(not bool(item["pass"]) for item in records),
        "profiles": ranked,
        "recommended_profile": ranked[0]["profile"] if ranked else "standard",
    }
    args.output.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
    print(json.dumps(output, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
