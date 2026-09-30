#!/usr/bin/env python3
"""Turn verified boot/ELF/BTF evidence into BOPE's runtime contract."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from bope.contract import BTF_SIZES, DERIVED_RELATIONS, parse_numeric_macros  # noqa: E402

EVIDENCE = Path("/tmp/universal")
KEYS = ROOT / "targets/universal-WIP/brazilian-open-payload-engine/src/runtime_keys.def"
DONOR = ROOT / "targets/zzi8-WIP/brazilian-open-payload-engine/src/target.h"
OUTPUT = EVIDENCE / "target.contract"


def main() -> None:
    report = json.loads((EVIDENCE / "bope-contract-evidence.json").read_text())
    identity = report["identity"]
    if report["missing_derivations"]:
        raise SystemExit(f"missing derivations: {report['missing_derivations']}")

    donor = parse_numeric_macros(DONOR)
    values = {key: int(value, 0) for key, value in report["derived_target_macros"].items()}
    for macro in BTF_SIZES:
        if values[macro] != donor[macro]:
            raise SystemExit(f"compiled ZZI8 structure size differs: {macro}")
    slabs = {}
    for line in (EVIDENCE / "slabinfo.txt").read_text().splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[0] in {"mm_struct", "skbuff_head_cache", "kmalloc-2k", "kmalloc-4k"}:
            slabs[parts[0]] = tuple(map(int, parts[3:6]))
    expected_slabs = {
        "mm_struct": (donor["TARGET_MM_STRUCT_SIZE"], 32, 8),
        "skbuff_head_cache": (256, 32, 2),
        "kmalloc-2k": (donor["TARGET_PIPE_OBJECT_SIZE"], 16, 8),
        "kmalloc-4k": (4096, 8, 8),
    }
    for name, expected in expected_slabs.items():
        if slabs.get(name) != expected:
            raise SystemExit(f"incompatible slab geometry for {name}: {slabs.get(name)} != {expected}")
    for macro, (parent, delta) in DERIVED_RELATIONS.items():
        values[macro] = values[parent] + delta

    inherited_geometry = (
        "TARGET_KERNEL_IMAGE_SPAN", "TARGET_LINEAR_MAP_BASE", "TARGET_LINEAR_MAP_END",
        "TARGET_VMEMMAP_START", "TARGET_VMEMMAP_END",
        "TARGET_TRACE_LOOSE_RECORD_SIZE", "TARGET_TRACE_TYPE_LEN_MASK",
    )
    values.update({macro: donor[macro] for macro in inherited_geometry})

    trace = (EVIDENCE / "sched_blocked_reason.format").read_text()
    event_id = re.search(r"^ID:\s*(\d+)$", trace, re.MULTILINE)
    caller = re.search(r"field:void\* caller;\s*offset:(\d+);", trace)
    if not event_id or not caller:
        raise SystemExit("tracepoint format lacks ID or caller offset")
    values["TARGET_TRACE_SCHED_BLOCKED_REASON_ID"] = int(event_id.group(1))
    values["TARGET_TRACE_CALLER_OFF"] = int(caller.group(1))

    keys = re.findall(r"^RMG_RUNTIME_KEY\((TARGET_[A-Z0-9_]+)\)$", KEYS.read_text(), re.MULTILINE)
    if len(set(keys)) != len(keys) or set(keys) != set(values) - set(BTF_SIZES):
        missing = set(keys) - set(values)
        extra = set(values) - set(keys) - set(BTF_SIZES)
        raise SystemExit(f"runtime field mismatch: missing={sorted(missing)} extra={sorted(extra)}")

    fields = {
        "schema": "1",
        "model": identity["ro.product.model"],
        "device": identity["ro.product.device"],
        "build": identity["ro.build.version.incremental"],
        "fingerprint": identity["ro.build.fingerprint"],
        "kernel_release": identity["kernel_release"],
        "kernel_version": identity["kernel_version"],
        "boot_id": identity["boot_id"],
    }
    if fields["model"] != "SM-S918B" or fields["device"] != "dm3q":
        raise SystemExit("universal WIP is limited to the SM-S918B / dm3q family")
    if not fields["fingerprint"].endswith(fields["build"] + ":user/release-keys"):
        raise SystemExit("build and fingerprint disagree")
    lines = [f"{key}={value}" for key, value in fields.items()]
    lines.extend(f"{key}={values[key]:#x}" for key in keys)
    OUTPUT.write_text("\n".join(lines) + "\n")
    print(f"Wrote {len(keys)} runtime values to {OUTPUT}")


if __name__ == "__main__":
    main()
