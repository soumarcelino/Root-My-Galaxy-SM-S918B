#!/usr/bin/env python3
"""Verify the critical allocation/free/reclaim CPU invariant."""

from __future__ import annotations

import argparse
import json
import pathlib
import re


def after(text: str, needle: str, start: int = 0) -> int:
    position = text.find(needle, start)
    if position < 0:
        raise ValueError(f"marcador ausente: {needle}")
    return position


def main() -> int:
    repo = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=pathlib.Path,
        default=repo / "src/05_mm_slab_grooming.c",
    )
    parser.add_argument("--artifact", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    failures: list[str] = []
    checks: dict[str, bool] = {}
    try:
        text = args.source.read_text()
        checks["no_hardcoded_critical_cpu"] = "OSS_CRITICAL_CPU" not in text
        setup = after(text, "g_ks_verify = kernelsnitch_setup")
        creation_pin = after(text, "pin_and_validate_groom_cpu()", setup)
        critical_factory = after(text, "fill_critical_with_exec_factory(", creation_pin)
        pcp_send = after(text, "pcp_sent = sendmsg(pcp_sv[0]", critical_factory)
        reclaim_pin = after(text, "pin_and_validate_groom_cpu()", pcp_send)
        cage_prepare = after(
            text, "prepare_close_range_batch(cage_sources", reclaim_pin
        )
        cage_close = after(text, "close_range_batch(cage_first_fd", cage_prepare)
        seed_prepare = after(
            text, "prepare_close_range_batch(seed_sources", cage_close
        )
        seed_close = after(text, "close_range_batch(seed_first_fd", seed_prepare)
        final_prepare = after(
            text, "prepare_close_range_batch(critical_sources", seed_close
        )
        batch_close = after(
            text, "close_range_batch(critical_first_fd", final_prepare
        )
        reclaim_send = after(text, "sent = sendmsg(reclaim_sv[0]", batch_close)
        critical_failure = after(
            text, '"[groom] critical close_range failed first=', batch_close
        )
        failure_end = "    goto cleanup;\n  }\n"
        critical_gap_start = after(text, failure_end, critical_failure) + len(
            failure_end
        )
        critical_gap = text[critical_gap_start:reclaim_send]
        critical_gap_calls = {
            name
            for name in re.findall(r"\b([A-Za-z_]\w*)\s*\(", critical_gap)
            if name not in {"for", "if", "while", "switch", "sizeof"}
        }
        exact_gate = after(text, "exact_mm_reclaim(", reclaim_send)
        checks["critical_creation_order"] = (
            setup < creation_pin < critical_factory < pcp_send
        )
        checks["critical_reclaim_order"] = (
            pcp_send
            < reclaim_pin
            < cage_prepare
            < cage_close
            < seed_prepare
            < seed_close
            < final_prepare
            < batch_close
            < reclaim_send
            < exact_gate
        )
        checks["immediate_first_reclaim_send"] = (
            not critical_gap_calls and "errno =" not in critical_gap
        )
        checks["bulk_factory_uses_fast_cpu"] = "pin_groom_cpu()" in text
        checks["bulk_spray_uses_fast_cpu"] = "clone_child(g_groom_cpu)" in text
        checks["critical_factory_single_pid"] = (
            'factory_session_next(&session, "critical-pre"' in text
            and 'factory_session_next(&session, "critical-post"' in text
        )
        checks["no_yield_in_reclaim"] = "sched_yield()" not in text
        checks["exact_slab_gate"] = (
            "released_refs" in text
            and "object_drop == OSS_ORDER3_SIZE / OSS_MM_STRUCT_SZ" in text
            and "active_slab_drop == 1 && slab_drop == 1" in text
            and "active_drop == released_refs" not in text
        )
        cage_body = text[cage_prepare:seed_prepare]
        checks["known_target_released_last"] = (
            "critical_sources[] = {&memfd_leak}" in text
            and "&memfd_leak" not in cage_body
            and seed_close < final_prepare < batch_close
        )
        checks["partial_cache_flush"] = (
            "prepare_slab_count" in text
            and "spray_seed_drains" in text
            and "target cage refs=%zu seed=%zu final_release=%zu" in text
        )
        checks["dynamic_cpu_revalidation"] = (
            '#include "00_cpu_discovery.h"' in text
            and "pin_and_validate_groom_cpu()" in text
            and text.count("pin_and_validate_groom_cpu()") >= 5
        )
        checks["diagnostic_marker_present"] = (
            "critical create/free/reclaim cpu=%d" in text
        )
    except (OSError, ValueError) as error:
        failures.append(str(error))

    if args.artifact:
        if not args.artifact.is_file():
            failures.append(f"artefato ausente: {args.artifact}")
        else:
            data = args.artifact.read_bytes()
            checks["artifact_has_cpu_marker"] = (
                b"critical create/free/reclaim cpu=%d" in data
            )
            checks["artifact_has_exact_gate"] = b"exact reclaim active_drop=" in data
            checks["artifact_has_close_range"] = (
                b"target cage refs=%zu seed=%zu final_release=%zu" in data
            )

    failures.extend(name for name, passed in checks.items() if not passed)
    report = {
        "source": str(args.source.resolve()),
        "artifact": str(args.artifact.resolve()) if args.artifact else None,
        "checks": checks,
        "failures": failures,
        "passed": not failures,
    }
    rendered = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered)
    else:
        print(rendered, end="")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
