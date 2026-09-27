#!/usr/bin/env python3
import argparse
import json
import pathlib
import re


REQUIRED = {
    "kernel_access_reached": "stage=verifying-kernel-access",
    "aar_verified": "[aar_aaw] verify ok: corruption landed, R/W primitive live",
    "fops_restored": "restore ashmem_misc.fops",
    "pipe_ready": "[pipe_rw] ready",
}
FORBIDDEN = re.compile(
    r"usercopy: Kernel memory exposure|usercopy_abort|configfs_read_iter|"
    r"Kernel panic|Oops - BUG",
    re.IGNORECASE,
)


def text_evidence(boot: pathlib.Path) -> str:
    selected = []
    for path in boot.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix == ".log" or path.name in {
            "last_kmsg.txt",
            "proc-last-kmsg.txt",
            "dropbox-last-kmsg.txt",
        }:
            selected.append(path.read_text(errors="replace"))
    return "\n".join(selected)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("campaign", type=pathlib.Path)
    parser.add_argument("--expect-boots", type=int, default=3)
    parser.add_argument("--expect-sha256")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    boot_dirs = sorted(
        path for path in args.campaign.glob("boot-[0-9][0-9][0-9]")
        if path.is_dir()
    )
    results = []
    boot_ids = []
    payload_hashes = []
    for boot in boot_dirs:
        record_path = boot / "record.json"
        record = json.loads(record_path.read_text()) if record_path.is_file() else {}
        evidence = text_evidence(boot)
        checks = {name: marker in evidence for name, marker in REQUIRED.items()}
        checks["fops_restore_confirmed"] = bool(
            re.search(r"restore ashmem_misc\.fops.*confirmed=1", evidence)
        )
        forbidden = sorted(set(match.group(0) for match in FORBIDDEN.finditer(evidence)))
        boot_id = record.get("boot_id", "")
        payload_hash = record.get("payload_sha256", "")
        if boot_id:
            boot_ids.append(boot_id)
        if payload_hash:
            payload_hashes.append(payload_hash)
        results.append({
            "boot": boot.name,
            "boot_id": boot_id,
            "checks": checks,
            "forbidden_markers": forbidden,
            "full_root_flow_pass": record.get("pass") is True,
            "pass_usercopy_path": all(checks.values()) and not forbidden,
            "payload_sha256": payload_hash,
        })

    distinct_boot_ids = len(boot_ids) == len(set(boot_ids)) == args.expect_boots
    one_payload = len(set(payload_hashes)) == 1 and len(payload_hashes) == args.expect_boots
    expected_payload = not args.expect_sha256 or (
        one_payload and payload_hashes[0] == args.expect_sha256.lower()
    )
    passed = (
        len(results) == args.expect_boots
        and distinct_boot_ids
        and one_payload
        and expected_payload
        and all(item["pass_usercopy_path"] for item in results)
    )
    report = {
        "boots_expected": args.expect_boots,
        "boots_found": len(results),
        "distinct_boot_ids": distinct_boot_ids,
        "one_payload": one_payload,
        "expected_payload": expected_payload,
        "pass_usercopy_path_all_boots": passed,
        "full_root_flow_passes": sum(item["full_root_flow_pass"] for item in results),
        "boots": results,
    }
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
