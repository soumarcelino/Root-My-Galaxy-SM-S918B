#!/usr/bin/env python3
"""Extract the external rt_mutex waiter selected by rb_erase from a panic."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

PI_TREE_ENTRY_OFF = 0x18
RB_PARENT_COLOR_MASK = ~0x3


def register(text: str, name: str) -> int:
    match = re.search(rf"\b{name}\s*:\s*([0-9a-fA-F]+)", text)
    if not match:
        raise ValueError(f"register {name} missing")
    return int(match.group(1), 16)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("panic", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    text = args.panic.read_text(errors="replace")
    if "rb_erase+0x10" not in text:
        raise SystemExit("FAIL panic is not rb_erase+0x10")

    try:
        pi_tree_entry = register(text, "x0")
        pi_root = register(text, "x1")
        parent_color = register(text, "x8")
    except ValueError as error:
        raise SystemExit(f"FAIL {error}") from error

    waiter = pi_tree_entry - PI_TREE_ENTRY_OFF
    result = {
        "source": str(args.panic),
        "rb_erase_node": f"0x{pi_tree_entry:016x}",
        "rt_mutex_waiter": f"0x{waiter:016x}",
        "pi_tree_entry_offset": f"0x{PI_TREE_ENTRY_OFF:x}",
        "pi_waiters_root": f"0x{pi_root:016x}",
        "pi_parent_color": f"0x{parent_color:016x}",
        "pi_parent_pointer": f"0x{parent_color & RB_PARENT_COLOR_MASK:016x}",
        "rb_right": None,
        "rb_left": None,
        "complete_geometry": False,
        "reason": "panic records parent_color only; capture rb_right/rb_left before trigger",
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered)
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
