"""Retarget and audit the KernelSU Next late-load bundle."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct

from elftools.elf.elffile import ELFFile

from .common import executable, note, run, run_logged
from .errors import BopeError


KSU_BTF_STRUCTURES = (
    "cred",
    "task_struct",
    "work_struct",
    "completion",
    "ucounts",
    "user_struct",
    "user_namespace",
    # `struct module` is the one the kernel itself walks at load time
    # (load_module -> apply_relocations writes init/exit, add_usage_links
    # walks target_list). A layout skew here is an immediate panic, and it
    # is invisible to the CRC audit because none of those fields are
    # exported symbols. The DYI3 module shipped with cleanup_module
    # relocated into target_list (0x368 instead of 0x378) for exactly this
    # reason: the build lacked CONFIG_DEBUG_INFO_BTF_MODULES, which inserts
    # btf_data_size/btf_data before target_list.
    "module",
)


def retarget_vermagic(source: Path, destination: Path, old_release: str, new_release: str) -> None:
    old = f"vermagic={old_release}".encode()
    new = f"vermagic={new_release}".encode()
    if len(old) != len(new):
        raise BopeError(
            "KernelSU vermagic releases differ in byte length; rebuild the module "
            "instead of applying an in-place retarget"
        )
    data = source.read_bytes()
    count = data.count(old)
    if count != 1:
        raise BopeError(f"expected one donor vermagic string, found {count}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data.replace(old, new, 1))


def _read_virtual_address(elf: ELFFile, address: int, size: int) -> bytes:
    for segment in elf.iter_segments():
        if segment["p_type"] != "PT_LOAD":
            continue
        start = segment["p_vaddr"]
        end = start + segment["p_filesz"]
        if start <= address and address + size <= end:
            elf.stream.seek(segment["p_offset"] + address - start)
            return elf.stream.read(size)
    raise BopeError(f"vmlinux address 0x{address:x} is not backed by PT_LOAD")


def load_crctab(path: Path) -> dict[str, int]:
    """Read a precomputed kernel symbol CRC table (JSON: name -> "0x…")."""
    raw = json.loads(path.read_text())
    return {name: int(value, 16) for name, value in raw.items()}


def target_crcs(vmlinux: Path, crctab: Path | None = None) -> dict[str, int]:
    """Symbol CRCs for the device kernel, from the compact table when present.

    The table is a few hundred KB and can be committed; the vmlinux it comes
    from is ~50 MB and only ever needed to regenerate it.
    """
    if crctab is not None and crctab.is_file():
        return load_crctab(crctab)
    return target_symvers(vmlinux)


def target_symvers(vmlinux: Path) -> dict[str, int]:
    with vmlinux.open("rb") as stream:
        elf = ELFFile(stream)
        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            raise BopeError("vmlinux has no .symtab")
        symbols = {symbol.name: symbol["st_value"] for symbol in symtab.iter_symbols()}
        ranges = (
            (
                symbols["__start___ksymtab"],
                symbols["__stop___ksymtab"],
                symbols["__start___kcrctab"],
            ),
            (
                symbols["__start___ksymtab_gpl"],
                symbols["__stop___ksymtab_gpl"],
                symbols["__start___kcrctab_gpl"],
            ),
        )
        result: dict[str, int] = {}
        for name, address in symbols.items():
            if not name.startswith("__ksymtab_"):
                continue
            exported = name.removeprefix("__ksymtab_")
            for table_start, table_stop, crc_start in ranges:
                if table_start <= address < table_stop:
                    offset = address - table_start
                    if offset % 12:
                        raise BopeError(f"unaligned kernel_symbol entry: {exported}")
                    crc_address = crc_start + (offset // 12) * 4
                    result[exported] = struct.unpack(
                        "<I", _read_virtual_address(elf, crc_address, 4)
                    )[0]
                    break
        return result


def _undefined_symbols(module: Path, llvm_nm: Path) -> set[str]:
    symbols: set[str] = set()
    for line in run([llvm_nm, "-u", module]).splitlines():
        parts = line.split()
        if parts:
            symbols.add(parts[-1])
    return symbols


def _kallsyms(path: Path) -> set[str]:
    result: set[str] = set()
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 3:
            result.add(parts[2])
    return result


def _module_versions(module: Path, modprobe: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in run([modprobe, "--dump-modversions", module]).splitlines():
        parts = line.split()
        if len(parts) >= 2:
            result[parts[1]] = int(parts[0], 16)
    return result


def _btf_layout(path: Path, structure: str, pahole: Path, flavor: str = "btf") -> str:
    return run([pahole, "-F", flavor, "-C", structure, path])


_PAHOLE_MEMBER = re.compile(
    r"^\s*(?P<decl>.*?)\s*;\s*/\*\s*(?P<offset>\d+)\s+(?P<size>\d+)\s*\*/\s*$"
)
_PAHOLE_BLOCK_OPEN = re.compile(r"^\s*(?:struct|union)\s*(?P<name>[A-Za-z_]\w*)?\s*\{\s*$")
_PAHOLE_BLOCK_CLOSE = re.compile(
    r"^\s*\}\s*(?P<member>(?!__attribute__)[A-Za-z_]\w*)?\s*"
    r"(?:__attribute__\(\(.*\)\))?\s*;\s*"
    r"(?:/\*\s*(?P<offset>\d+)\s+(?P<size>\d+)\s*\*/)?\s*$"
)
_PAHOLE_SIZE = re.compile(r"/\* size:\s*(\d+)")


def _strip_attributes(declaration: str) -> str:
    """Drop __attribute__((...)) so DWARF and BTF declarations agree."""
    result: list[str] = []
    index = 0
    while index < len(declaration):
        if declaration.startswith("__attribute__", index):
            cursor = declaration.find("((", index)
            if cursor == -1:
                break
            depth = 0
            while cursor < len(declaration):
                if declaration[cursor] == "(":
                    depth += 1
                elif declaration[cursor] == ")":
                    depth -= 1
                    if depth == 0:
                        cursor += 1
                        break
                cursor += 1
            index = cursor
            continue
        result.append(declaration[index])
        index += 1
    return "".join(result).strip()


def _member_name(declaration: str) -> str | None:
    declaration = _strip_attributes(declaration)
    if not declaration or declaration.startswith(("/*", "}")):
        return None
    function_pointer = re.search(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)", declaration)
    if function_pointer:
        return function_pointer.group(1)
    array = re.search(r"([A-Za-z_]\w*)\s*\[", declaration)
    if array:
        return array.group(1)
    names = re.findall(r"[A-Za-z_]\w*", declaration)
    return names[-1] if names else None


def _pahole_layout(text: str) -> tuple[int | None, dict[str, tuple[int, int]]]:
    """Parse a pahole struct dump into (byte_size, {member: (offset, size)}).

    DWARF and BTF dumps of the same layout differ cosmetically: attribute
    annotations are printed on one side only, and pahole's DWARF mode repeats
    every anonymous union as an extra brace block whose members carry
    union-relative offsets (the real ones carry absolute offsets). Comparing
    raw text therefore both misses real skew and invents fake skew, so the
    comparison is numeric and the artifact blocks are dropped.
    """
    byte_size: int | None = None
    # One dict per open brace block; index 0 is the struct itself.
    stack: list[dict[str, tuple[int, int]]] = [{}]
    for line in text.splitlines():
        size = _PAHOLE_SIZE.search(line)
        if size:
            byte_size = int(size.group(1))
            continue
        opening = _PAHOLE_BLOCK_OPEN.match(line)
        if opening:
            stack.append({})
            continue
        closing = _PAHOLE_BLOCK_CLOSE.match(line)
        if closing:
            if len(stack) == 1:
                continue
            members = stack.pop()
            parent = stack[-1]
            if closing.group("member"):
                # An inline definition closing as a member: its offset is real
                # and its members are absolute, so fold them into the parent.
                parent.update(members)
                if closing.group("offset") is not None:
                    parent[closing.group("member")] = (
                        int(closing.group("offset")),
                        int(closing.group("size")),
                    )
                continue
            if closing.group("offset") is None and len(stack) > 1:
                # Pahole's DWARF mode repeats anonymous unions as brace blocks
                # without an absolute offset, and those members are relative to
                # the union. Dropping the block keeps only the real offsets.
                # The struct's own closing brace is the exception: after this
                # pop it is the last frame on the stack.
                continue
            parent.update(members)
            continue
        member = _PAHOLE_MEMBER.match(line)
        if member:
            name = _member_name(member.group("decl"))
            if name:
                stack[-1][name] = (int(member.group("offset")), int(member.group("size")))
    return byte_size, stack[0]


def _layout_mismatch(build: str, target: str) -> dict[str, object] | None:
    """Compare two pahole dumps; None when the layouts agree.

    Fatal conditions: differing struct size, a shared member whose offset or
    size moved, or a member the kernel has that the build side does not know
    about. Members present only on the build side are reported as
    informational: pahole renders anonymous unions from DWARF differently
    than from BTF, which produces phantom extras that carry no skew.
    """
    build_size, build_members = _pahole_layout(build)
    target_size, target_members = _pahole_layout(target)
    if not build_members or not target_members:
        # A dump that parses to nothing is a parser failure, not agreement.
        # Silently passing here is what let the struct module skew through.
        raise BopeError(
            "pahole layout parse produced no members "
            f"(build={len(build_members)}, target={len(target_members)})"
        )
    fields = {
        name: {
            "build": list(build_members[name]),
            "target": list(target_members[name]),
        }
        for name in sorted(target_members.keys() & build_members.keys())
        if build_members[name] != target_members[name]
    }
    missing_in_build = sorted(target_members.keys() - build_members.keys())
    extra_in_build = sorted(build_members.keys() - target_members.keys())
    if fields or missing_in_build or build_size != target_size:
        return {
            "build_size": build_size,
            "target_size": target_size,
            "fields": fields,
            "missing_in_build": missing_in_build,
            "extra_in_build": extra_in_build,
        }
    return None


def _this_module_relocations(module: Path) -> dict[str, int]:
    """Offsets the kernel writes init/cleanup_module into during load_module."""
    offsets: dict[str, int] = {}
    with module.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = elf.get_section_by_name(".symtab")
        if symbols is None:
            raise BopeError(f"{module} has no .symtab; cannot audit relocations")
        names = [symbol.name for symbol in symbols.iter_symbols()]
        for section in elf.iter_sections():
            if not (section.name.startswith(".rela") and section.name.endswith("this_module")):
                continue
            for relocation in section.iter_relocations():
                name = names[relocation["r_info_sym"]]
                if name in ("init_module", "cleanup_module"):
                    offsets[name] = relocation["r_offset"]
    return offsets


def this_module_relocation_mismatches(
    module: Path, target_btf: Path, pahole: Path
) -> dict[str, object]:
    """init/cleanup_module must land on the target's struct module init/exit."""
    offsets = _this_module_relocations(module)
    _, members = _pahole_layout(_btf_layout(target_btf, "module", pahole))
    expected = {"init_module": "init", "cleanup_module": "exit"}
    mismatches: dict[str, object] = {}
    for symbol, member in expected.items():
        if member not in members:
            mismatches[symbol] = {"reason": f"target struct module has no {member}"}
            continue
        want = members[member][0]
        got = offsets.get(symbol)
        if got != want:
            mismatches[symbol] = {
                "relocation_offset": got,
                "target_member": member,
                "target_offset": want,
            }
    return mismatches


def _has_dwarf(module: Path) -> bool:
    with module.open("rb") as stream:
        elf = ELFFile(stream)
        return elf.get_section_by_name(".debug_info") is not None


def audit_module(
    module: Path,
    *,
    target_release: str,
    kallsyms: Path,
    vmlinux: Path,
    build_module: Path,
    target_btf: Path,
    llvm_nm: Path,
    pahole: Path,
    modinfo: Path,
    modprobe: Path,
    require_layout_check: bool = False,
    crctab: Path | None = None,
) -> dict[str, object]:
    undefined = _undefined_symbols(module, llvm_nm)
    missing_symbols = sorted(undefined - _kallsyms(kallsyms))
    versions = _module_versions(module, modprobe)
    target_versions = target_crcs(vmlinux, crctab)
    crc_mismatches = {
        name: {
            "module": f"0x{versions[name]:08x}",
            "target": f"0x{target_versions[name]:08x}",
        }
        for name in sorted(versions.keys() & target_versions.keys())
        if versions[name] != target_versions[name]
    }
    missing_crc_targets = sorted(versions.keys() - target_versions.keys())
    # Layout comparison is DWARF (what the module was compiled against) vs BTF
    # (what the device kernel actually has). Both sides must describe the same
    # offsets; the CRC audit cannot see non-exported struct fields at all.
    # A stripped module has no DWARF, so the build-side layouts come from the
    # unstripped object of the same build when one is supplied.
    layout_source: Path | None = None
    for candidate in (module, build_module):
        if candidate is not None and candidate.is_file() and _has_dwarf(candidate):
            layout_source = candidate
            break
    btf_mismatches: dict[str, object] = {}
    layout_check = "unavailable: no DWARF in " + " or ".join(
        str(candidate) for candidate in (module, build_module) if candidate is not None
    )
    if layout_source is not None:
        layout_check = f"dwarf:{layout_source}"
        for structure in KSU_BTF_STRUCTURES:
            build = _btf_layout(layout_source, structure, pahole, "dwarf")
            target = _btf_layout(target_btf, structure, pahole, "btf")
            mismatch = _layout_mismatch(build, target)
            if mismatch:
                btf_mismatches[structure] = mismatch
    relocation_mismatches = this_module_relocation_mismatches(module, target_btf, pahole)
    vermagic = run([modinfo, "-F", "vermagic", module]).strip()
    passed = (
        not missing_symbols
        and not crc_mismatches
        and not missing_crc_targets
        and not btf_mismatches
        and not relocation_mismatches
        and vermagic.startswith(target_release + " ")
        and (layout_source is not None or not require_layout_check)
    )
    return {
        "passed": passed,
        "module": str(module),
        "sha256": hashlib.sha256(module.read_bytes()).hexdigest(),
        "vermagic": vermagic,
        "undefined_symbols": len(undefined),
        "missing_symbols": missing_symbols,
        "module_version_entries": len(versions),
        "matched_symbol_crcs": len(versions.keys() & target_versions.keys()),
        "crc_mismatches": crc_mismatches,
        "missing_crc_targets": missing_crc_targets,
        "layout_check": layout_check,
        "build_layout_source": str(build_module),
        "btf_structures": list(KSU_BTF_STRUCTURES),
        "btf_mismatches": btf_mismatches,
        "relocation_mismatches": relocation_mismatches,
    }


def _find_donor_module(donor: Path) -> Path:
    candidates = sorted((donor / "kernelsu-next/out").glob("**/*kernelsu.ko"))
    if not candidates:
        raise BopeError(f"donor KernelSU module was not found under {donor}")
    return candidates[-1]


def _find_donor_btf(donor: Path) -> Path:
    candidates = sorted((donor / "firmware").glob("vmlinux_*.btf"))
    candidates.extend(
        sorted((donor / "reference/kernel/btf").glob("vmlinux*.btf"))
    )
    if len(candidates) != 1:
        raise BopeError(
            f"expected one donor BTF blob, found {len(candidates)} under {donor}"
        )
    return candidates[0]


def _find_ksu_source(explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit.expanduser())
    configured = os.environ.get("BOPE_KSU_NEXT_SRC") or os.environ.get("KSU_NEXT_SRC")
    if configured:
        candidates.append(Path(configured).expanduser())
    candidates.extend(
        [
            Path.home() / "Projects/github/KernelSU-Next-v3.4.0-zzhl",
            Path.home() / "Projects/github/KernelSU-Next-v3.4.0",
        ]
    )
    for candidate in candidates:
        if (candidate / "userspace/ksud/Cargo.toml").is_file():
            return candidate.resolve()
    raise BopeError("KernelSU Next source was not found; pass --ksu-source")


def _find_ndk(explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(explicit.expanduser())
    if os.environ.get("ANDROID_NDK_HOME"):
        candidates.append(Path(os.environ["ANDROID_NDK_HOME"]).expanduser())
    candidates.extend(
        [
            Path.home() / "Android/Sdk/ndk/28.2.13676358",
            Path("/opt/android-sdk/ndk/28.2.13676358"),
        ]
    )
    for candidate in candidates:
        if (candidate / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang").is_file():
            return candidate.resolve()
    raise BopeError("Android NDK was not found; set ANDROID_NDK_HOME")


def build_ksud(
    module: Path,
    destination: Path,
    *,
    ksu_source: Path | None,
    ndk: Path | None,
    log: Path,
) -> Path:
    source = _find_ksu_source(ksu_source)
    ndk_root = _find_ndk(ndk)
    cargo = executable("cargo")
    linker = ndk_root / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
    llvm_root = ndk_root / "toolchains/llvm/prebuilt/linux-x86_64"
    strip = llvm_root / "bin/llvm-strip"
    embedded = source / "userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko"
    embedded.parent.mkdir(parents=True, exist_ok=True)
    previous = embedded.read_bytes() if embedded.is_file() else None
    try:
        shutil.copyfile(module, embedded)
        os.utime(embedded, None)
        environment = os.environ.copy()
        environment["CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER"] = str(linker)
        environment["LIBCLANG_PATH"] = str(llvm_root / "lib")
        run_logged(
            [cargo, "build", "--locked", "--release", "--target", "aarch64-linux-android"],
            log,
            cwd=source / "userspace/ksud",
            env=environment,
        )
        built = source / "userspace/ksud/target/aarch64-linux-android/release/ksud"
        if not built.is_file():
            raise BopeError(f"KernelSU build completed without ksud: {built}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        run([strip, "--strip-all", "-o", destination, built])
        destination.chmod(0o755)
    finally:
        if previous is None:
            embedded.unlink(missing_ok=True)
        else:
            embedded.write_bytes(previous)
    return destination


def prepare_kernelsu(
    donor: Path,
    target: Path,
    *,
    slug: str,
    donor_release: str,
    target_release: str,
    target_kallsyms: Path,
    target_vmlinux: Path,
    target_btf: Path,
    llvm_nm: Path,
    pahole: Path,
    ksu_source: Path | None,
    ndk: Path | None,
    logs_dir: Path,
    donor_module: Path | None = None,
    donor_btf: Path | None = None,
) -> dict[str, object]:
    donor_module = donor_module or _find_donor_module(donor)
    donor_btf = donor_btf or _find_donor_btf(donor)
    if not donor_module.is_file():
        raise BopeError(f"donor KernelSU module does not exist: {donor_module}")
    if not donor_btf.is_file():
        raise BopeError(f"donor BTF blob does not exist: {donor_btf}")
    version_match = re.search(r"v(\d+\.\d+\.\d+)", str(donor_module))
    version = version_match.group(1) if version_match else "3.4.0"
    output = target / f"kernelsu-next/out/kernelsu-next-{slug}-v{version}"
    module = output / "android13-5.15_kernelsu.ko"
    ksud = output / f"ksud-next-v{version}"
    note(f"retargeting KernelSU Next v{version} module vermagic")
    retarget_vermagic(donor_module, module, donor_release, target_release)
    report = audit_module(
        module,
        target_release=target_release,
        kallsyms=target_kallsyms,
        vmlinux=target_vmlinux,
        # Retargeting copies the donor module byte-for-byte (only the vermagic
        # string changes), so the donor's own DWARF is the build-side layout.
        build_module=donor_module,
        target_btf=target_btf,
        llvm_nm=llvm_nm,
        pahole=pahole,
        modinfo=executable("modinfo"),
        modprobe=executable("modprobe"),
    )
    if not report["passed"]:
        raise BopeError(
            "KernelSU compatibility audit failed: " + json.dumps(report, sort_keys=True)
        )
    note("building ksud with the retargeted module embedded")
    build_ksud(module, ksud, ksu_source=ksu_source, ndk=ndk, log=logs_dir / "ksud-build.log")
    report["version"] = version
    report["module"] = str(module.relative_to(target))
    report["ksud"] = {
        "path": str(ksud.relative_to(target)),
        "sha256": hashlib.sha256(ksud.read_bytes()).hexdigest(),
    }
    (output / "compatibility.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    (output / "SHA256SUMS").write_text(
        f"{hashlib.sha256(module.read_bytes()).hexdigest()}  {module.name}\n"
        f"{hashlib.sha256(ksud.read_bytes()).hexdigest()}  {ksud.name}\n"
    )
    return report
