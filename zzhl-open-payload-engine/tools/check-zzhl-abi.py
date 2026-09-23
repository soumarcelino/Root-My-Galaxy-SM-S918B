#!/usr/bin/env python3
"""Verify every BTF field offset consumed by the ZZHL open payload."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


CHECKS: dict[str, tuple[str, ...]] = {
    "file_operations": (
        r"owner;\s*/\*\s+0\s+8\s*\*/",
        r"read_iter.*?/\*\s+32\s+8\s*\*/",
        r"write_iter.*?/\*\s+40\s+8\s*\*/",
        r"unlocked_ioctl.*?/\*\s+80\s+8\s*\*/",
        r"compat_ioctl.*?/\*\s+88\s+8\s*\*/",
        r"\(\*mmap\).*?/\*\s+96\s+8\s*\*/",
        r"\(\*open\).*?/\*\s+112\s+8\s*\*/",
        r"\(\*release\).*?/\*\s+128\s+8\s*\*/",
        r"splice_read.*?/\*\s+200\s+8\s*\*/",
        r"show_fdinfo.*?/\*\s+224\s+8\s*\*/",
    ),
    "miscdevice": (r"fops;\s*/\*\s+16\s+8\s*\*/",),
    "file": (r"private_data;\s*/\*\s+216\s+8\s*\*/",),
    "files_struct": (r"fdt;\s*/\*\s+32\s+8\s*\*/",),
    "fdtable": (r"fd;\s*/\*\s+8\s+8\s*\*/",),
    "pipe_inode_info": (
        r"head;\s*/\*\s+96\s+4\s*\*/",
        r"tail;\s*/\*\s+100\s+4\s*\*/",
        r"ring_size;\s*/\*\s+108\s+4\s*\*/",
        r"bufs;\s*/\*\s+168\s+8\s*\*/",
    ),
    "pipe_buffer": (
        r"page;\s*/\*\s+0\s+8\s*\*/",
        r"ops;\s*/\*\s+16\s+8\s*\*/",
        r"size:\s+40,",
    ),
    "page": (
        r"compound_head;\s*/\*\s+8\s+8\s*\*/",
        r"slab_cache;\s*/\*\s+24\s+8\s*\*/",
        r"size:\s+64,",
    ),
    "task_struct": (
        r"sched_task_group;\s*/\*\s+1024\s+8\s*\*/",
        r"tasks;\s*/\*\s+1232\s+16\s*\*/",
        r"pid;\s*/\*\s+1496\s+4\s*\*/",
        r"comm\[16\];\s*/\*\s+1960\s+16\s*\*/",
        r"files;\s*/\*\s+2008\s+8\s*\*/",
    ),
    "rt_mutex_waiter": (
        r"tree_entry;\s*/\*\s+0\s+24\s*\*/",
        r"pi_tree_entry;\s*/\*\s+24\s+24\s*\*/",
        r"task;\s*/\*\s+48\s+8\s*\*/",
        r"lock;\s*/\*\s+56\s+8\s*\*/",
        r"prio;\s*/\*\s+68\s+4\s*\*/",
    ),
    "workqueue_struct": (r"dfl_pwq;\s*/\*\s+176\s+8\s*\*/",),
    "pool_workqueue": (
        r"pool;\s*/\*\s+0\s+8\s*\*/",
        r"wq;\s*/\*\s+8\s+8\s*\*/",
        r"work_color;\s*/\*\s+16\s+4\s*\*/",
        r"refcnt;\s*/\*\s+24\s+4\s*\*/",
        r"nr_in_flight\[16\];\s*/\*\s+28\s+64\s*\*/",
        r"nr_active;\s*/\*\s+92\s+4\s*\*/",
        r"max_active;\s*/\*\s+96\s+4\s*\*/",
    ),
    "worker_pool": (
        r"worklist;\s*/\*\s+32\s+16\s*\*/",
        r"nr_idle;\s*/\*\s+52\s+4\s*\*/",
    ),
    "work_struct": (
        r"data;\s*/\*\s+0\s+8\s*\*/",
        r"entry;\s*/\*\s+8\s+16\s*\*/",
        r"func;\s*/\*\s+24\s+8\s*\*/",
    ),
    "subprocess_info": (
        r"complete;\s*/\*\s+48\s+8\s*\*/",
        r"path;\s*/\*\s+56\s+8\s*\*/",
        r"argv;\s*/\*\s+64\s+8\s*\*/",
        r"envp;\s*/\*\s+72\s+8\s*\*/",
        r"wait;\s*/\*\s+80\s+4\s*\*/",
        r"size:\s+112,",
    ),
    "selinux_state": (r"enforcing;\s*/\*\s+0\s+1\s*\*/",),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--btf", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if not args.btf.is_file():
        parser.error(f"BTF ausente: {args.btf}")
    failed = 0
    for struct, patterns in CHECKS.items():
        run = subprocess.run(
            ["pahole", "-F", "btf", "-C", struct, str(args.btf)],
            text=True, capture_output=True, check=False,
        )
        text = run.stdout
        missing = [p for p in patterns if not re.search(p, text, re.DOTALL)]
        if run.returncode or missing:
            failed += 1
            print(f"FAIL {struct}: {len(missing)} layout checks missing")
        else:
            print(f"PASS {struct}: {len(patterns)} layout checks")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
