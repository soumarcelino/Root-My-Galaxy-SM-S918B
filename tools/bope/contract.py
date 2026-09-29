"""Derive and verify BOPE's firmware contract from ELF and BTF evidence."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

from .common import run
from .errors import BopeError


SYMBOLS: dict[str, tuple[str, int]] = {
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
    "TARGET_CALL_USERMODEHELPER_EXEC_WORK_OFF": ("call_usermodehelper_exec_work", 0),
    "TARGET_DO_SAK_WORK_OFF": ("do_SAK_work", 0),
    "TARGET_DO_SAK_OFF": ("do_SAK", 0),
    "TARGET_SELINUX_STATE_ENFORCING_OFF": ("selinux_state", 0),
}


BTF_FIELDS: dict[str, tuple[str, str]] = {
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
    "TARGET_FOPS_OWNER_OFF": ("file_operations", "owner"),
    "TARGET_FOPS_LLSEEK_OFF": ("file_operations", "llseek"),
    "TARGET_FOPS_READ_OFF": ("file_operations", "read"),
    "TARGET_FOPS_WRITE_OFF": ("file_operations", "write"),
    "TARGET_FOPS_READ_ITER_OFF": ("file_operations", "read_iter"),
    "TARGET_FOPS_WRITE_ITER_OFF": ("file_operations", "write_iter"),
    "TARGET_FOPS_IOCTL_OFF": ("file_operations", "unlocked_ioctl"),
    "TARGET_FOPS_COMPAT_IOCTL_OFF": ("file_operations", "compat_ioctl"),
    "TARGET_FOPS_MMAP_OFF": ("file_operations", "mmap"),
    "TARGET_FOPS_OPEN_OFF": ("file_operations", "open"),
    "TARGET_FOPS_RELEASE_OFF": ("file_operations", "release"),
    "TARGET_FOPS_SPLICE_READ_OFF": ("file_operations", "splice_read"),
    "TARGET_FOPS_SHOW_FDINFO_OFF": ("file_operations", "show_fdinfo"),
    "TARGET_FAKE_TASK_USAGE_OFF": ("task_struct", "usage"),
    "TARGET_FAKE_TASK_PRIO_OFF": ("task_struct", "prio"),
    "TARGET_FAKE_TASK_NORMAL_PRIO_OFF": ("task_struct", "normal_prio"),
    "TARGET_FAKE_TASK_TASK_GROUP_OFF": ("task_struct", "sched_task_group"),
    "TARGET_FAKE_TASK_PI_LOCK_OFF": ("task_struct", "pi_lock"),
    "TARGET_FAKE_TASK_PI_WAITERS_OFF": ("task_struct", "pi_waiters"),
    "TARGET_FAKE_TASK_PI_TOP_TASK_OFF": ("task_struct", "pi_top_task"),
    "TARGET_FAKE_TASK_PI_BLOCKED_ON_OFF": ("task_struct", "pi_blocked_on"),
    "TARGET_RT_WAITER_TREE_PARENT_OFF": ("rt_mutex_waiter", "tree_entry"),
    "TARGET_RT_WAITER_PI_PARENT_OFF": ("rt_mutex_waiter", "pi_tree_entry"),
    "TARGET_RT_WAITER_TASK_OFF": ("rt_mutex_waiter", "task"),
    "TARGET_RT_WAITER_LOCK_OFF": ("rt_mutex_waiter", "lock"),
    "TARGET_RT_WAITER_WAKE_STATE_OFF": ("rt_mutex_waiter", "wake_state"),
    "TARGET_RT_WAITER_PRIO_OFF": ("rt_mutex_waiter", "prio"),
    "TARGET_RT_WAITER_DEADLINE_OFF": ("rt_mutex_waiter", "deadline"),
    "TARGET_RT_WAITER_WW_CTX_OFF": ("rt_mutex_waiter", "ww_ctx"),
    "TARGET_UMH_SUBPROCESS_COMPLETE_OFF": ("subprocess_info", "complete"),
}


BTF_SIZES: dict[str, str] = {
    "TARGET_STRUCT_PAGE_SIZE": "page",
    "TARGET_KMEM_CACHE_DESC_SIZE": "kmem_cache",
    "TARGET_TASK_CACHE_SIZE": "task_struct",
    "TARGET_FILES_CACHE_SIZE": "files_struct",
    "TARGET_TTY_OPS_SIZE": "tty_operations",
    "TARGET_UMH_SUBPROCESS_INFO_SIZE": "subprocess_info",
    "TARGET_UMH_SUBPROCESS_WORK_SIZE": "work_struct",
    "TARGET_UMH_COMPLETION_SIZE": "completion",
}


DERIVED_RELATIONS = {
    "TARGET_WORKER_CALLER_OFF": ("TARGET_WORKER_THREAD_OFF", 0x78),
    "TARGET_RT_WAITER_TREE_RIGHT_OFF": ("TARGET_RT_WAITER_TREE_PARENT_OFF", 0x08),
    "TARGET_RT_WAITER_TREE_LEFT_OFF": ("TARGET_RT_WAITER_TREE_PARENT_OFF", 0x10),
    "TARGET_RT_WAITER_PI_RIGHT_OFF": ("TARGET_RT_WAITER_PI_PARENT_OFF", 0x08),
    "TARGET_RT_WAITER_PI_LEFT_OFF": ("TARGET_RT_WAITER_PI_PARENT_OFF", 0x10),
}


@dataclass(frozen=True)
class ContractResult:
    header: Path
    derived: dict[str, int | str]
    inherited: list[str]
    changed: dict[str, dict[str, int | str]]


def parse_numeric_macros(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for name, raw in re.findall(
        r"^#define\s+(TARGET_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|[0-9]+)[uUlL]*\s*$",
        path.read_text(),
        re.MULTILINE,
    ):
        values[name] = int(raw, 0)
    return values


def all_target_macros(path: Path) -> list[str]:
    return re.findall(r"^#define\s+(TARGET_[A-Z0-9_]+)\b", path.read_text(), re.MULTILINE)


def parse_symbols(elf: Path, llvm_nm: Path) -> dict[str, int]:
    output = run([llvm_nm, "-n", elf])
    result: dict[str, int] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            result.setdefault(parts[2], int(parts[0], 16))
    return result


def btf_layout(btf: Path, structure: str, pahole: Path) -> str:
    return run([pahole, "-F", "btf", "-C", structure, btf])


def field_offset(layout: str, field: str) -> int | None:
    escaped = re.escape(field) + (r"(?:\[[^]]+\])?" if field == "comm" else "")
    patterns = (
        rf"\b{escaped}\s*;[^/]*?/\*\s*(\d+)(?:[: ])",
        rf"\(\*\s*{escaped}\s*\)\([^;]*\)\s*;[^/]*?/\*\s*(\d+)(?:[: ])",
    )
    for pattern in patterns:
        match = re.search(pattern, layout, re.MULTILINE)
        if match:
            return int(match.group(1))
    return None


def structure_size(layout: str) -> int | None:
    match = re.search(r"/\* size:\s*(\d+),", layout)
    return int(match.group(1)) if match else None


def _numeric_style(raw: str, value: int) -> str:
    raw = raw.strip()
    match = re.fullmatch(r"(0x[0-9a-fA-F]+|[0-9]+)([uUlL]*)", raw)
    if not match:
        raise BopeError(f"cannot preserve numeric macro style: {raw!r}")
    number, suffix = match.groups()
    if number.lower().startswith("0x"):
        width = len(number) - 2
        return f"0x{value:0{width}x}{suffix}"
    return f"{value}{suffix}"


def _replace_numeric(text: str, name: str, value: int) -> tuple[str, int | None]:
    pattern = re.compile(
        rf"^(#define[ \t]+{re.escape(name)}[ \t]+)(0x[0-9a-fA-F]+|[0-9]+)([uUlL]*)[ \t]*$",
        re.MULTILINE,
    )
    match = pattern.search(text)
    if not match:
        raise BopeError(f"donor target is missing numeric macro {name}")
    old_raw = match.group(2) + match.group(3)
    old_value = int(match.group(2), 0)
    replacement = match.group(1) + _numeric_style(old_raw, value)
    return text[: match.start()] + replacement + text[match.end() :], old_value


def _replace_string(text: str, name: str, value: str) -> tuple[str, str]:
    pattern = re.compile(
        rf'^(#define[ \t]+{re.escape(name)})([ \t]+\\\n[ \t]*|[ \t]+)"([^"]*)"[ \t]*$',
        re.MULTILINE,
    )
    match = pattern.search(text)
    if not match:
        raise BopeError(f"donor target is missing string macro {name}")
    old = match.group(3)
    replacement = f'{match.group(1)}{match.group(2)}"{value}"'
    return text[: match.start()] + replacement + text[match.end() :], old


def derive_contract(
    donor_header: Path,
    output_header: Path,
    *,
    target_name: str,
    model: str,
    device: str,
    build: str,
    fingerprint: str,
    kernel_release: str,
    kernel_version: str | None = None,
    elf: Path,
    btf: Path,
    llvm_nm: Path,
    pahole: Path,
) -> ContractResult:
    text = donor_header.read_text()
    symbols = parse_symbols(elf, llvm_nm)
    old_numeric = parse_numeric_macros(donor_header)
    base = symbols.get("_text", old_numeric.get("TARGET_KIMAGE_TEXT_BASE"))
    if base is None:
        raise BopeError("kernel _text and donor TARGET_KIMAGE_TEXT_BASE are both missing")

    derived: dict[str, int | str] = {}
    changed: dict[str, dict[str, int | str]] = {}
    identity = {
        "TARGET_NAME": target_name,
        "TARGET_MODEL": model,
        "TARGET_DEVICE": device,
        "TARGET_BUILD": build,
        "TARGET_FINGERPRINT": fingerprint,
        "TARGET_KERNEL_RELEASE": kernel_release,
    }
    if kernel_version is not None:
        identity["TARGET_KERNEL_VERSION"] = kernel_version
    donor_identity: dict[str, str] = {}
    for name, value in identity.items():
        text, old = _replace_string(text, name, value)
        donor_identity[name] = old
        derived[name] = value
        if old != value:
            changed[name] = {"from": old, "to": value}

    # The contract body is copied from the donor. Keep its prose target-neutral
    # by updating source-build/name references after replacing identity macros.
    text = text.replace(donor_identity["TARGET_BUILD"], build)
    donor_name = donor_identity["TARGET_NAME"]
    text = text.replace(donor_name.upper(), target_name.upper())
    text = text.replace(donor_name, target_name)
    text = re.sub(
        r"must not carry [A-Z0-9]+ literals\.",
        "must not carry firmware literals.",
        text,
    )

    values: dict[str, int] = {"TARGET_KIMAGE_TEXT_BASE": base}
    for macro, (symbol, delta) in SYMBOLS.items():
        if symbol not in symbols:
            raise BopeError(f"required kernel symbol is missing: {symbol}")
        values[macro] = symbols[symbol] - base + delta

    layouts: dict[str, str] = {}
    for macro, (structure, field) in BTF_FIELDS.items():
        layouts.setdefault(structure, btf_layout(btf, structure, pahole))
        offset = field_offset(layouts[structure], field)
        if offset is None:
            raise BopeError(f"BTF field is missing: {structure}.{field}")
        values[macro] = offset
    for macro, structure in BTF_SIZES.items():
        layouts.setdefault(structure, btf_layout(btf, structure, pahole))
        size = structure_size(layouts[structure])
        if size is None:
            raise BopeError(f"BTF structure size is missing: {structure}")
        values[macro] = size
    values["TARGET_FAKE_FOPS_POPULATED_SIZE"] = values["TARGET_FOPS_SHOW_FDINFO_OFF"] + 8
    for macro, (parent, delta) in DERIVED_RELATIONS.items():
        values[macro] = values[parent] + delta

    for name, value in values.items():
        text, old = _replace_numeric(text, name, value)
        derived[name] = value
        if old != value:
            changed[name] = {"from": old, "to": value}

    output_header.parent.mkdir(parents=True, exist_ok=True)
    output_header.write_text(text)
    inherited = sorted(set(all_target_macros(output_header)) - set(derived))
    return ContractResult(output_header, derived, inherited, changed)


def verify_contract(
    header: Path,
    *,
    elf: Path,
    btf: Path,
    llvm_nm: Path,
    pahole: Path,
) -> dict[str, object]:
    macros = parse_numeric_macros(header)
    symbols = parse_symbols(elf, llvm_nm)
    base = macros.get("TARGET_KIMAGE_TEXT_BASE")
    errors: list[str] = []
    checks: dict[str, int] = {}
    if base is None:
        errors.append("missing TARGET_KIMAGE_TEXT_BASE")
        base = 0
    for macro, (symbol, delta) in SYMBOLS.items():
        expected = symbols.get(symbol)
        actual = macros.get(macro)
        if expected is None:
            errors.append(f"missing ELF symbol {symbol}")
            continue
        expected = expected - base + delta
        if actual != expected:
            errors.append(f"{macro}: header={actual!r} ELF=0x{expected:x}")
        else:
            checks[macro] = expected

    layouts: dict[str, str] = {}
    for macro, (structure, field) in BTF_FIELDS.items():
        layouts.setdefault(structure, btf_layout(btf, structure, pahole))
        expected = field_offset(layouts[structure], field)
        actual = macros.get(macro)
        if actual != expected:
            errors.append(f"{macro}: header={actual!r} BTF={expected!r}")
        elif expected is not None:
            checks[macro] = expected
    for macro, structure in BTF_SIZES.items():
        layouts.setdefault(structure, btf_layout(btf, structure, pahole))
        expected = structure_size(layouts[structure])
        actual = macros.get(macro)
        if actual != expected:
            errors.append(f"{macro}: header={actual!r} BTF={expected!r}")
        elif expected is not None:
            checks[macro] = expected
    relations = dict(DERIVED_RELATIONS)
    relations["TARGET_FAKE_FOPS_POPULATED_SIZE"] = ("TARGET_FOPS_SHOW_FDINFO_OFF", 8)
    for macro, (parent, delta) in relations.items():
        expected = macros.get(parent, -delta) + delta
        if macros.get(macro) != expected:
            errors.append(f"{macro}: expected {parent}+0x{delta:x}")
        else:
            checks[macro] = expected
    return {
        "passed": not errors,
        "header": str(header),
        "elf_symbols": len(SYMBOLS),
        "btf_fields": len(BTF_FIELDS),
        "btf_sizes": len(BTF_SIZES),
        "derived_relations": len(relations),
        "checks": checks,
        "errors": errors,
    }
