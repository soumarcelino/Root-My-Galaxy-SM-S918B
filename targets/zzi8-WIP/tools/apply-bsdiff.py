#!/usr/bin/env python3
"""Apply an OTA BSDIFF40 patch and optionally verify the output."""

from __future__ import annotations

import argparse
import bz2
import hashlib
from pathlib import Path


def decode_offset(raw: bytes) -> int:
    value = int.from_bytes(raw, "little")
    negative = value >> 63
    value &= (1 << 63) - 1
    return -value if negative else value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("patch", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sha1")
    args = parser.parse_args()

    patch = args.patch.read_bytes()
    if patch[:8] != b"BSDIFF40":
        raise ValueError("patch is not BSDIFF40")
    control_size = decode_offset(patch[8:16])
    diff_size = decode_offset(patch[16:24])
    output_size = decode_offset(patch[24:32])
    control = bz2.decompress(patch[32 : 32 + control_size])
    diff = bz2.decompress(patch[32 + control_size : 32 + control_size + diff_size])
    extra = bz2.decompress(patch[32 + control_size + diff_size :])
    source = args.source.read_bytes()
    output = bytearray(output_size)
    old_at = new_at = control_at = diff_at = extra_at = 0
    while new_at < output_size:
        add = decode_offset(control[control_at : control_at + 8])
        copy = decode_offset(control[control_at + 8 : control_at + 16])
        seek = decode_offset(control[control_at + 16 : control_at + 24])
        control_at += 24
        if add < 0 or copy < 0 or new_at + add + copy > output_size:
            raise ValueError("invalid control tuple")
        for index in range(add):
            source_index = old_at + index
            old_byte = source[source_index] if 0 <= source_index < len(source) else 0
            output[new_at + index] = (diff[diff_at + index] + old_byte) & 0xFF
        new_at += add
        old_at += add
        diff_at += add
        output[new_at : new_at + copy] = extra[extra_at : extra_at + copy]
        new_at += copy
        old_at += seek
        extra_at += copy

    digest = hashlib.sha1(output).hexdigest()
    if args.sha1 and digest != args.sha1:
        raise ValueError(f"SHA-1 mismatch: {digest} != {args.sha1}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"{digest}  {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
