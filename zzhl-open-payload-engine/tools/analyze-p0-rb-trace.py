#!/usr/bin/env python3
"""Require complete external RB geometry from tracefs kprobe events."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def fields(line: str) -> dict[str, int]:
    return {key: int(value, 16) for key, value in
            re.findall(r"\b(node|root|parent|right|left|task|blocked)=([0-9a-fA-F]+)", line)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    lines = args.trace.read_text(errors="replace").splitlines()
    erase = [fields(line) for line in lines if "p0rb_erase:" in line]
    adjust = [fields(line) for line in lines if "p0rb_adjust:" in line]
    if not erase:
        raise SystemExit("FAIL no rb_erase probe event")
    latest = erase[-1]
    required = {"node", "root", "parent", "right", "left"}
    if required - latest.keys():
        raise SystemExit("FAIL incomplete rb_erase event")
    linked_adjust = [event for event in adjust
                     if event.get("blocked", 0) and
                     event["blocked"] + 0x18 == latest["node"]]
    result = {
        "adjust_events": adjust,
        "rb_erase": latest,
        "linked_adjust_events": linked_adjust,
        "complete_geometry": bool(linked_adjust),
        "release_condition": False,
    }
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    if not result["complete_geometry"]:
        raise SystemExit("FAIL rb_erase node is not linked to captured pi_blocked_on")
    print("PASS complete external RB geometry; scheduler trigger remains blocked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
