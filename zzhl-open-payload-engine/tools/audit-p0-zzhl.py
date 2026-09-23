#!/usr/bin/env python3
"""Reject a P0 build when imported ZZHL target values are not audited."""
import argparse
import re
import sys
from pathlib import Path


EXPECTED = {
    "INIT_TASK_OFF": "0x02c05380ULL",
    "PREPARE_KERNEL_CRED_OFF": "0x0011e694ULL",
    "COMMIT_CREDS_OFF": "0x001203d0ULL",
    "OVERRIDE_CREDS_OFF": "0x0011f4a8ULL",
    "ROOT_TASK_GROUP_OFF": "0x02cb9ac0ULL",
    "KMALLOC_CACHES_OFF": "0x02067330ULL",
    "ANON_PIPE_BUF_OPS_OFF": "0x01e81e60ULL",
    "SYSTEM_UNBOUND_WQ_OFF": "0x02a90808ULL",
    "CALL_USERMODEHELPER_EXEC_WORK_OFF": "0x00104888ULL",
    "ASHMEM_FOPS_OFF": "0x02010238ULL",
    "ASHMEM_MISC_FOPS_OFF": "0x02bfd1f8ULL",
    "ASHMEM_IOCTL_OFF": "0x01151344ULL",
    "ASHMEM_COMPAT_IOCTL_OFF": "0x011519a0ULL",
    "ASHMEM_MMAP_OFF": "0x011519f8ULL",
    "ASHMEM_OPEN_OFF": "0x01151cd8ULL",
    "ASHMEM_RELEASE_OFF": "0x01151d70ULL",
    "ASHMEM_SHOW_FDINFO_OFF": "0x01151e8cULL",
    "CONFIGFS_READ_ITER_OFF": "0x005d89c0ULL",
    "CONFIGFS_BIN_WRITE_ITER_OFF": "0x005d93e8ULL",
    "COPY_SPLICE_READ_OFF": "0x00528dccULL",
    "NOOP_LLSEEK_OFF": "0x004bc658ULL",
    "SELINUX_ENFORCING_OFF": "0x02d8e600ULL",
    "ROOT_UMH_PATH": '"/data/local/tmp/oss-clone-zzhl/root-helper"',
}


def macros(path: Path):
    return dict(re.findall(r"^#define\s+(\w+)\s+(\S+)", path.read_text(), re.M))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", type=Path, required=True)
    ap.add_argument("--wrapper", type=Path, required=True)
    args = ap.parse_args()
    values = macros(args.upstream)
    wrapper = macros(args.wrapper)
    failed = False
    for name, expected in EXPECTED.items():
        actual = wrapper.get(name, values.get(name))
        if actual != expected:
            print(f"FAIL {name}: got={actual!r} want={expected}")
            failed = True
        else:
            print(f"PASS {name}={actual}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
