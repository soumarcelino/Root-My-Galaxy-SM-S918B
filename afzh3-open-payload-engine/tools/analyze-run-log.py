#!/usr/bin/env python3
"""Parse payload/runner logs into stable JSON or Markdown summaries."""

from __future__ import annotations

import argparse
import json
import pathlib
import re

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
PATTERNS = {
    "kaslr": re.compile(r"\[kaslr\] source=(\S+) base=([0-9a-f]+).*p0_offset=([0-9a-f]+)"),
    "groom": re.compile(r"\[groom\] mm leaked=([0-9a-f]+) aligned_base=([0-9a-f]+)"),
    "pipe": re.compile(r"\[pipe_rw\] ready attempt=(\d+)/(\d+).*pipe=(\d+)"),
    "umh": re.compile(r"\[root_umh\] result wake=(\d+) complete=(\d+) socket=(\d+)"),
    "attempt": re.compile(r"exploit completed attempt=(\d+)/(\d+)"),
    "ksu": re.compile(r"KernelSU control verified version=(\d+) flags=(\S+) uapi=(\d+) features=(\S+)"),
    "uid": re.compile(r"uid=(\d+)\(root\).*context=(\S+)"),
}
PIPE_READY = re.compile(
    r"\[pipe_rw\] ready attempt=(\d+)/(\d+).*pipe=(\d+).*"
    r"prepare_ms=(\d+) establish_ms=(\d+) total_ms=(\d+)"
)
PIPE_FAILURE = re.compile(
    r"\[pipe_rw\] (setup miss|terminal failure) attempt=(\d+)/(\d+) "
    r"reason=(\S+) stage=(\S+) errno=(-?\d+) elapsed_ms=(\d+)"
)
PIPE_TELEMETRY = re.compile(
    r"\[pipe_rw\] telemetry stage_ms (?P<stages>.*?) total=(?P<total>\d+)ms"
)


def analyze(path: pathlib.Path) -> dict[str, object]:
    text = ANSI.sub("", path.read_text(errors="replace"))
    lines = text.splitlines()
    result: dict[str, object] = {
        "path": str(path),
        "stages": re.findall(r"stage=([a-z0-9-]+)", text),
        "errors": [],
    }
    for name, pattern in PATTERNS.items():
        match = pattern.search(text)
        if match:
            result[name] = match.groups()

    pipe_attempts = []
    pipe_telemetry = []
    for line in lines:
        telemetry = PIPE_TELEMETRY.search(line)
        if telemetry:
            stages = {
                name: int(value)
                for name, value in re.findall(r"([a-z-]+)=(\d+)ms", telemetry["stages"])
            }
            pipe_telemetry.append({
                "stage_ms": stages,
                "total_ms": int(telemetry["total"]),
            })
        match = PIPE_READY.search(line)
        if match:
            attempt, limit, pipe, prepare, establish, total = map(int, match.groups())
            pipe_attempts.append({
                "attempt": attempt, "limit": limit, "outcome": "ready",
                "pipe": pipe, "prepare_ms": prepare,
                "establish_ms": establish, "total_ms": total,
            })
            continue
        match = PIPE_FAILURE.search(line)
        if match:
            kind, attempt, limit, reason, stage, error_no, elapsed = match.groups()
            pipe_attempts.append({
                "attempt": int(attempt), "limit": int(limit),
                "outcome": kind.replace(" ", "_"), "reason": reason,
                "stage": stage, "errno": int(error_no),
                "elapsed_ms": int(elapsed),
            })
    result["pipe_attempts"] = pipe_attempts
    result["pipe_telemetry"] = pipe_telemetry
    result["mutation"] = {
        "kernel_pending": "stage=kernel-mutation-pending" in text,
        "workqueue_pending": ("stage=workqueue-mutation-pending" in text or
                              "[root_umh] queued" in text),
    }

    error_words = ("failed", "falhou", "panic", "corruption", "HARDENED_USERCOPY", "timeout")
    result["errors"] = [line for line in lines if any(word.lower() in line.lower() for word in error_words)][:50]
    temporary = "stage=temporary-root-ready" in text
    ksu = "ksu" in result
    uid0 = "uid" in result and result["uid"][0] == "0"
    runner_root = "[+] Root confirmado" in text
    if uid0 or runner_root:
        classification = "PASS_ROOT"
    elif temporary and ksu:
        classification = "PARTIAL_KSU_NO_SU_PROOF"
    elif temporary:
        classification = "PARTIAL_TEMP_ROOT"
    elif "[aar_aaw] verify ok" in text:
        classification = "PARTIAL_KERNEL_RW"
    elif "stage=kernel-location-ready" in text:
        classification = "PARTIAL_KASLR"
    else:
        classification = "FAIL_EARLY_OR_INCONCLUSIVE"
    result["classification"] = classification
    result["checks"] = {
        "aar_aaw_verified": "[aar_aaw] verify ok" in text,
        "global_fops_restored": bool(re.search(r"restore ashmem_misc\.fops .*ok=1", text)),
        "pipe_ready": "pipe" in result,
        "temporary_root_ready": temporary,
        "kernelsu_verified": ksu,
        "uid0_proved": uid0 or runner_root,
    }
    return result


def markdown(results: list[dict[str, object]]) -> str:
    out = ["# Relatório de execução", ""]
    for result in results:
        out += [f"## `{result['path']}`", "", f"Resultado: **{result['classification']}**", ""]
        checks = result["checks"]
        out += ["| Check | Estado |", "|---|---:|"]
        for name, value in checks.items():
            out.append(f"| `{name}` | {'PASS' if value else 'FAIL'} |")
        out.append("")
        if result["errors"]:
            out += ["Erros/sinais:", ""] + [f"- `{line}`" for line in result["errors"]] + [""]
    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=pathlib.Path)
    parser.add_argument("--format", choices=("json", "markdown"), default="json")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    for path in args.logs:
        if not path.is_file():
            parser.error(f"log ausente: {path}")
    results = [analyze(path) for path in args.logs]
    text = json.dumps(results, indent=2, ensure_ascii=False) + "\n" if args.format == "json" else markdown(results)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
