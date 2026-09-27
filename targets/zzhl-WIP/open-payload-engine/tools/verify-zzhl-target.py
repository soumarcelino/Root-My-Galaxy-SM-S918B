#!/usr/bin/env python3
"""Verify ZZHL source constants against the bundled ELF, kallsyms and BTF."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


BASE = 0xFFFFFFC008000000

SYMBOLS = {
    "ZZHL_WORKER_THREAD_OFF": ("worker_thread", 0),
    "ZZHL_INIT_TASK_OFF": ("init_task", 0),
    "ZZHL_ROOT_TASK_GROUP_OFF": ("root_task_group", 0),
    "ZZHL_ASHMEM_MISC_FOPS_OFF": ("ashmem_misc", 0x10),
    "ZZHL_ASHMEM_FOPS_OFF": ("ashmem_fops", 0),
    "ZZHL_ASHMEM_IOCTL_OFF": ("ashmem_ioctl", 0),
    "ZZHL_ASHMEM_COMPAT_IOCTL_OFF": ("compat_ashmem_ioctl", 0),
    "ZZHL_ASHMEM_MMAP_OFF": ("ashmem_mmap", 0),
    "ZZHL_ASHMEM_OPEN_OFF": ("ashmem_open", 0),
    "ZZHL_ASHMEM_RELEASE_OFF": ("ashmem_release", 0),
    "ZZHL_ASHMEM_SHOW_FDINFO_OFF": ("ashmem_show_fdinfo", 0),
    "ZZHL_CONFIGFS_READ_ITER_OFF": ("configfs_read_iter", 0),
    "ZZHL_CONFIGFS_BIN_WRITE_ITER_OFF": ("configfs_bin_write_iter", 0),
    "ZZHL_GENERIC_FILE_SPLICE_READ_OFF": ("generic_file_splice_read", 0),
    "ZZHL_MEMSTART_ADDR_OFF": ("memstart_addr", 0),
    "ZZHL_KIMAGE_VOFFSET_OFF": ("kimage_voffset", 0),
    "ZZHL_KMALLOC_CACHES_OFF": ("kmalloc_caches", 0),
    "ZZHL_ANON_PIPE_BUF_OPS_OFF": ("anon_pipe_buf_ops", 0),
    "ZZHL_CALL_USERMODEHELPER_EXEC_WORK_OFF":
        ("call_usermodehelper_exec_work", 0),
    "ZZHL_DO_SAK_WORK_OFF": ("do_SAK_work", 0),
    "ZZHL_DO_SAK_OFF": ("do_SAK", 0),
    "ZZHL_SELINUX_STATE_ENFORCING_OFF": ("selinux_state", 0),
}

BTF_FIELDS = {
    "ZZHL_PAGE_COMPOUND_HEAD_OFF": ("page", "compound_head"),
    "ZZHL_PAGE_SLAB_CACHE_OFF": ("page", "slab_cache"),
    "ZZHL_KMEM_CACHE_SIZE_OFF": ("kmem_cache", "size"),
    "ZZHL_KMEM_CACHE_OBJECT_SIZE_OFF": ("kmem_cache", "object_size"),
    "ZZHL_KMEM_CACHE_INUSE_OFF": ("kmem_cache", "inuse"),
    "ZZHL_KMEM_CACHE_ALIGN_OFF": ("kmem_cache", "align"),
    "ZZHL_KMEM_CACHE_USEROFFSET_OFF": ("kmem_cache", "useroffset"),
    "ZZHL_KMEM_CACHE_USERSIZE_OFF": ("kmem_cache", "usersize"),
    "ZZHL_TASK_TASKS_OFF": ("task_struct", "tasks"),
    "ZZHL_TASK_PID_OFF": ("task_struct", "pid"),
    "ZZHL_TASK_COMM_OFF": ("task_struct", "comm"),
    "ZZHL_TASK_FILES_OFF": ("task_struct", "files"),
    "ZZHL_FILES_FDT_OFF": ("files_struct", "fdt"),
    "ZZHL_FDTABLE_MAX_FDS_OFF": ("fdtable", "max_fds"),
    "ZZHL_FDTABLE_FD_OFF": ("fdtable", "fd"),
    "ZZHL_FILE_PRIVATE_DATA_OFF": ("file", "private_data"),
    "ZZHL_PIPE_HEAD_OFF": ("pipe_inode_info", "head"),
    "ZZHL_PIPE_TAIL_OFF": ("pipe_inode_info", "tail"),
    "ZZHL_PIPE_RING_SIZE_OFF": ("pipe_inode_info", "ring_size"),
    "ZZHL_PIPE_BUFS_OFF": ("pipe_inode_info", "bufs"),
    "ZZHL_PIPE_BUFFER_PAGE_OFF": ("pipe_buffer", "page"),
    "ZZHL_PIPE_BUFFER_OPS_OFF": ("pipe_buffer", "ops"),
    "ZZHL_TTY_FILE_TTY_OFF": ("tty_file_private", "tty"),
    "ZZHL_TTY_FILE_FILE_OFF": ("tty_file_private", "file"),
    "ZZHL_TTY_MAGIC_OFF": ("tty_struct", "magic"),
    "ZZHL_TTY_OPS_OFF": ("tty_struct", "ops"),
    "ZZHL_TTY_INDEX_OFF": ("tty_struct", "index"),
    "ZZHL_TTY_SAK_WORK_OFF": ("tty_struct", "SAK_work"),
    "ZZHL_TTY_PORT_OFF": ("tty_struct", "port"),
    "ZZHL_TTY_OPS_FLUSH_BUFFER_OFF": ("tty_operations", "flush_buffer"),
    "ZZHL_WORK_DATA_OFF": ("work_struct", "data"),
    "ZZHL_WORK_ENTRY_OFF": ("work_struct", "entry"),
    "ZZHL_WORK_FUNC_OFF": ("work_struct", "func"),
}

BTF_SIZES = {
    "ZZHL_STRUCT_PAGE_SIZE": ("page", 64),
    "ZZHL_KMEM_CACHE_DESC_SIZE": ("kmem_cache", 264),
    "ZZHL_TASK_CACHE_SIZE": ("task_struct", 4608),
    "ZZHL_FILES_CACHE_SIZE": ("files_struct", 704),
    "ZZHL_TTY_OPS_SIZE": ("tty_operations", 280),
}


def parse_macros(path: pathlib.Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for name, raw in re.findall(
        r"^#define\s+(ZZHL_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|[0-9]+)[uUlL]*\s*$",
        path.read_text(), re.M,
    ):
        values[name] = int(raw, 0)
    return values


def parse_symbols(elf: pathlib.Path) -> dict[str, int]:
    proc = subprocess.run(
        ["llvm-nm", "-n", str(elf)], check=True, text=True,
        stdout=subprocess.PIPE,
    )
    result: dict[str, int] = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            result.setdefault(parts[2], int(parts[0], 16))
    return result


def pahole(btf: pathlib.Path, struct: str) -> str:
    return subprocess.run(
        ["pahole", "-F", "btf", "-C", struct, str(btf)], check=True,
        text=True, stdout=subprocess.PIPE,
    ).stdout


def field_offset(text: str, field: str) -> int | None:
    field_re = re.escape(field) + (r"(?:\[[^]]+\])?" if field == "comm" else "")
    patterns = (
        rf"\b{field_re}\s*;[^/]*?/\*\s*(\d+)(?:[: ])",
        rf"\(\*\s*{field_re}\s*\)\([^;]*\)\s*;[^/]*?/\*\s*(\d+)(?:[: ])",
    )
    for raw in patterns:
        match = re.search(raw, text, re.M)
        if match:
            return int(match.group(1))
    return None


def struct_size(text: str) -> int | None:
    match = re.search(r"/\* size:\s*(\d+),", text)
    return int(match.group(1)) if match else None


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    firmware = root.parent / "firmware"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=pathlib.Path,
                        default=root / "src/target_zzhl.h")
    parser.add_argument("--elf", type=pathlib.Path,
                        default=firmware / "vmlinux_ZZHL.elf")
    parser.add_argument("--btf", type=pathlib.Path,
                        default=firmware / "vmlinux_ZZHL.btf")
    args = parser.parse_args()

    macros = parse_macros(args.header)
    symbols = parse_symbols(args.elf)
    errors: list[str] = []

    for macro, (symbol, delta) in SYMBOLS.items():
        expected = symbols.get(symbol)
        actual = macros.get(macro)
        if expected is None:
            errors.append(f"missing ELF symbol {symbol}")
            continue
        expected = expected - BASE + delta
        if actual != expected:
            errors.append(
                f"{macro}: header={actual!r} ELF=0x{expected:x} ({symbol}+0x{delta:x})"
            )

    worker = macros.get("ZZHL_WORKER_THREAD_OFF")
    caller = macros.get("ZZHL_WORKER_CALLER_OFF")
    if worker is None or caller != worker + 0x78:
        errors.append("ZZHL_WORKER_CALLER_OFF must equal worker_thread + 0x78")

    layouts: dict[str, str] = {}
    for macro, (struct, field) in BTF_FIELDS.items():
        layouts.setdefault(struct, pahole(args.btf, struct))
        actual = macros.get(macro)
        expected = field_offset(layouts[struct], field)
        if actual != expected:
            errors.append(
                f"{macro}: header={actual!r} BTF={expected!r} ({struct}.{field})"
            )

    for macro, (struct, expected) in BTF_SIZES.items():
        layouts.setdefault(struct, pahole(args.btf, struct))
        actual = macros.get(macro)
        btf_size = struct_size(layouts[struct])
        if actual != expected or btf_size != expected:
            errors.append(
                f"{macro}: header={actual!r} BTF={btf_size!r} expected={expected}"
            )

    if macros.get("ZZHL_PIPE_BUFFER_STRIDE") != 40:
        errors.append("ZZHL_PIPE_BUFFER_STRIDE must be 40")

    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    print(f"PASS {len(SYMBOLS)} ELF symbols, {len(BTF_FIELDS)} BTF fields, "
          f"{len(BTF_SIZES)} BTF sizes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
