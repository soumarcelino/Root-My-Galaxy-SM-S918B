#!/usr/bin/env python3
"""Verify ZZI8 source constants against the bundled ELF, kallsyms and BTF."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


SYMBOLS = {
    "TARGET_WORKER_THREAD_OFF": ("worker_thread", 0),
    "TARGET_INIT_TASK_OFF": ("init_task", 0),
    "TARGET_ROOT_TASK_GROUP_OFF": ("root_task_group", 0),
    "TARGET_ASHMEM_MISC_FOPS_OFF": ("ashmem_misc", 0x10),
    "TARGET_ASHMEM_FOPS_OFF": ("ashmem_fops", 0),
    "TARGET_ASHMEM_IOCTL_OFF": ("ashmem_ioctl", 0),
    "TARGET_ASHMEM_COMPAT_IOCTL_OFF": ("compat_ashmem_ioctl", 0),
    "TARGET_ASHMEM_MMAP_OFF": ("ashmem_mmap", 0),
    "TARGET_ASHMEM_OPEN_OFF": ("ashmem_open", 0),
    "TARGET_ASHMEM_RELEASE_OFF": ("ashmem_release", 0),
    "TARGET_ASHMEM_SHOW_FDINFO_OFF": ("ashmem_show_fdinfo", 0),
    "TARGET_CONFIGFS_READ_ITER_OFF": ("configfs_read_iter", 0),
    "TARGET_CONFIGFS_BIN_WRITE_ITER_OFF": ("configfs_bin_write_iter", 0),
    "TARGET_GENERIC_FILE_SPLICE_READ_OFF": ("generic_file_splice_read", 0),
    "TARGET_MEMSTART_ADDR_OFF": ("memstart_addr", 0),
    "TARGET_KIMAGE_VOFFSET_OFF": ("kimage_voffset", 0),
    "TARGET_KMALLOC_CACHES_OFF": ("kmalloc_caches", 0),
    "TARGET_ANON_PIPE_BUF_OPS_OFF": ("anon_pipe_buf_ops", 0),
    "TARGET_CALL_USERMODEHELPER_EXEC_WORK_OFF":
        ("call_usermodehelper_exec_work", 0),
    "TARGET_DO_SAK_WORK_OFF": ("do_SAK_work", 0),
    "TARGET_DO_SAK_OFF": ("do_SAK", 0),
    "TARGET_SELINUX_STATE_ENFORCING_OFF": ("selinux_state", 0),
}

BTF_FIELDS = {
    "TARGET_PAGE_COMPOUND_HEAD_OFF": ("page", "compound_head"),
    "TARGET_PAGE_SLAB_CACHE_OFF": ("page", "slab_cache"),
    "TARGET_KMEM_CACHE_SIZE_OFF": ("kmem_cache", "size"),
    "TARGET_KMEM_CACHE_OBJECT_SIZE_OFF": ("kmem_cache", "object_size"),
    "TARGET_KMEM_CACHE_INUSE_OFF": ("kmem_cache", "inuse"),
    "TARGET_KMEM_CACHE_ALIGN_OFF": ("kmem_cache", "align"),
    "TARGET_KMEM_CACHE_USEROFFSET_OFF": ("kmem_cache", "useroffset"),
    "TARGET_KMEM_CACHE_USERSIZE_OFF": ("kmem_cache", "usersize"),
    "TARGET_TASK_TASKS_OFF": ("task_struct", "tasks"),
    "TARGET_TASK_PID_OFF": ("task_struct", "pid"),
    "TARGET_TASK_COMM_OFF": ("task_struct", "comm"),
    "TARGET_TASK_FILES_OFF": ("task_struct", "files"),
    "TARGET_FILES_FDT_OFF": ("files_struct", "fdt"),
    "TARGET_FDTABLE_MAX_FDS_OFF": ("fdtable", "max_fds"),
    "TARGET_FDTABLE_FD_OFF": ("fdtable", "fd"),
    "TARGET_FILE_PRIVATE_DATA_OFF": ("file", "private_data"),
    "TARGET_PIPE_HEAD_OFF": ("pipe_inode_info", "head"),
    "TARGET_PIPE_TAIL_OFF": ("pipe_inode_info", "tail"),
    "TARGET_PIPE_RING_SIZE_OFF": ("pipe_inode_info", "ring_size"),
    "TARGET_PIPE_BUFS_OFF": ("pipe_inode_info", "bufs"),
    "TARGET_PIPE_BUFFER_PAGE_OFF": ("pipe_buffer", "page"),
    "TARGET_PIPE_BUFFER_OPS_OFF": ("pipe_buffer", "ops"),
    "TARGET_TTY_FILE_TTY_OFF": ("tty_file_private", "tty"),
    "TARGET_TTY_FILE_FILE_OFF": ("tty_file_private", "file"),
    "TARGET_TTY_MAGIC_OFF": ("tty_struct", "magic"),
    "TARGET_TTY_OPS_OFF": ("tty_struct", "ops"),
    "TARGET_TTY_INDEX_OFF": ("tty_struct", "index"),
    "TARGET_TTY_SAK_WORK_OFF": ("tty_struct", "SAK_work"),
    "TARGET_TTY_PORT_OFF": ("tty_struct", "port"),
    "TARGET_TTY_OPS_FLUSH_BUFFER_OFF": ("tty_operations", "flush_buffer"),
    "TARGET_WORK_DATA_OFF": ("work_struct", "data"),
    "TARGET_WORK_ENTRY_OFF": ("work_struct", "entry"),
    "TARGET_WORK_FUNC_OFF": ("work_struct", "func"),
}

BTF_SIZES = {
    "TARGET_STRUCT_PAGE_SIZE": ("page", 64),
    "TARGET_KMEM_CACHE_DESC_SIZE": ("kmem_cache", 264),
    "TARGET_TASK_CACHE_SIZE": ("task_struct", 4608),
    "TARGET_FILES_CACHE_SIZE": ("files_struct", 704),
    "TARGET_TTY_OPS_SIZE": ("tty_operations", 280),
}


def parse_macros(path: pathlib.Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for name, raw in re.findall(
        r"^#define\s+(TARGET_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|[0-9]+)[uUlL]*\s*$",
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
                        default=root / "src/target.h")
    parser.add_argument("--elf", type=pathlib.Path,
                        default=firmware / "vmlinux_ZZI8.elf")
    parser.add_argument("--btf", type=pathlib.Path,
                        default=firmware / "vmlinux_ZZI8.btf")
    args = parser.parse_args()

    macros = parse_macros(args.header)
    symbols = parse_symbols(args.elf)
    errors: list[str] = []
    base = macros.get("TARGET_KIMAGE_TEXT_BASE")
    if base is None:
        print("FAIL missing TARGET_KIMAGE_TEXT_BASE")
        return 1

    for macro, (symbol, delta) in SYMBOLS.items():
        expected = symbols.get(symbol)
        actual = macros.get(macro)
        if expected is None:
            errors.append(f"missing ELF symbol {symbol}")
            continue
        expected = expected - base + delta
        if actual != expected:
            errors.append(
                f"{macro}: header={actual!r} ELF=0x{expected:x} ({symbol}+0x{delta:x})"
            )

    worker = macros.get("TARGET_WORKER_THREAD_OFF")
    caller = macros.get("TARGET_WORKER_CALLER_OFF")
    if worker is None or caller != worker + 0x78:
        errors.append("TARGET_WORKER_CALLER_OFF must equal worker_thread + 0x78")

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

    if macros.get("TARGET_PIPE_BUFFER_STRIDE") != 40:
        errors.append("TARGET_PIPE_BUFFER_STRIDE must be 40")

    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    print(f"PASS {len(SYMBOLS)} ELF symbols, {len(BTF_FIELDS)} BTF fields, "
          f"{len(BTF_SIZES)} BTF sizes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
