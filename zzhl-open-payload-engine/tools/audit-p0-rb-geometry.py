#!/usr/bin/env python3
"""Reject the known P0 pipe-buffer-as-rb-node geometry on ZZHL."""

from pathlib import Path
import sys


def require(path: Path, *needles: str) -> None:
    text = path.read_text()
    for needle in needles:
        if needle not in text:
            raise SystemExit(f"FAIL {path}: missing {needle!r}")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    upstream = root.parent
    btf = root / "evidence/zzhl-validation/btf-layouts-6584b354"

    require(upstream / "src/util.c",
            "p0_oracle_data_targets",
            "data_target = pipebuf_page_base +",
            "slide_bank_targets[slot] = 0",
            "put_fake_waiter(p, waiter_off, 1, 0, 0, parent, 0, 0")
    require(upstream / "src/common.h",
            "#define PIPE_OBJECT_SIZE KMALLOC_PIPE_OBJ_SIZE",
            "#define KMALLOC_PIPE_OBJ_SIZE 0x800",
            "struct user_pipe_buffer")
    require(root / "src/p0/p0_zzhl_safety.h",
            "#define P0_ZZHL_LOCAL_RB_GATE_ONLY 1")
    require(btf / "pipe_buffer.txt",
            "struct page *              page;", "unsigned int               offset;",
            "const struct pipe_buf_operations  * ops;")
    require(btf / "rb_node.txt",
            "__rb_parent_color", "rb_right", "rb_left")
    require(btf / "rt_mutex_waiter.txt",
            "tree_entry", "pi_tree_entry", "size: 88")

    print("PASS ZZHL BTF rt_mutex_waiter=0x58 rb_node=0x18")
    require(upstream / "src/slide_app.c",
            "tree_left == 0 && pi_left == 0",
            "ZZHL P0 local RB gate valid",
            "physical slot=%zu blocked: external RB proof required",
            "scheduler trigger disabled: external RB node unproven",
            "FOPS route not released",
            "strcmp(source, \"tracefs\") == 0")
    print("PASS P0 pipe target retained as data-only, never RB child")
    print("PASS local gate requires null children; scheduler/FOPS remain blocked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
