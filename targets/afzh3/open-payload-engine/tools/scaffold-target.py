#!/usr/bin/env python3
"""Create an explicit, incomplete target-profile scaffold for a new port."""

from __future__ import annotations

import argparse
import json
import pathlib


CHECK_NAMES = [
    ("ashmem_misc_fops", "src/00_orchestrator.c"),
    ("real_ashmem_fops", "src/00_orchestrator.c"),
    ("init_task", "src/00_orchestrator.c"),
    ("selinux_enforcing", "src/10_workqueue_umh_root.c"),
    ("system_unbound_wq", "src/10_workqueue_umh_root.c"),
    ("call_usermodehelper_exec_work", "src/10_workqueue_umh_root.c"),
    ("kmalloc_caches", "src/09_pipe_buffer_rw.c"),
    ("anon_pipe_buf_ops", "src/09_pipe_buffer_rw.c"),
    ("skb_send_size", "src/09_pipe_buffer_rw.c"),
    ("pipe_count", "src/09_pipe_buffer_rw.c"),
    ("pipe_slots", "src/09_pipe_buffer_rw.c"),
    ("fake_fops_offset", "src/00_orchestrator.c"),
    ("fake_work_offset", "src/10_workqueue_umh_root.c"),
    ("umh_data_offset", "src/10_workqueue_umh_root.c"),
    ("pipe_proof_offset", "src/09_pipe_buffer_rw.c"),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--kernel", default="TODO")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    if args.output.exists() and not args.force:
        parser.error(f"arquivo já existe: {args.output}; use --force")
    profile = {
        "schema": 1,
        "name": args.name,
        "device": args.device,
        "model": args.model,
        "build": args.build,
        "kernel": args.kernel,
        "validated_payload_sha256": None,
        "status": "INCOMPLETE_DO_NOT_EXECUTE",
        "checks": [
            {"name": name, "file": file, "literal": "TODO", "minimum": 1}
            for name, file in CHECK_NAMES
        ],
        "porting_gates": {
            "closed_binary_analyzed": False,
            "btf_and_kallsyms_compared": False,
            "read_only_device_checks_passed": False,
            "two_clean_reboots_passed": False,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(profile, indent=2, ensure_ascii=False) + "\n")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
