#!/usr/bin/env python3
"""Analyze Root My Galaxy app history together with AFZH3 panic evidence."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
from typing import Any


PATTERNS = {
    "selected_cpu": re.compile(r"\[groom\] cpu selected=(\d+) capacity=(\d+) max_freq_khz=(\d+)"),
    "critical_cpus": re.compile(r"\[groom\] bulk cpu=(\d+); critical create/free/reclaim cpu=(\d+)"),
    "factory_ms": re.compile(r"exec factory ready objects=(\d+) cpu=(\d+) elapsed=(\d+)ms"),
    "proc_spray_ms": re.compile(r"phase=proc-spray-done t=\+(\d+)ms dt=(\d+)ms"),
    "ksnitch_setup_ms": re.compile(r"phase=ksnitch-setup t=\+\d+ms dt=(\d+)ms"),
    "collisions_ms": re.compile(r"phase=find-collisions-done t=\+\d+ms dt=(\d+)ms"),
    "bruteforce_ms": re.compile(r"phase=bruteforce-done t=\+\d+ms dt=(\d+)ms"),
    "reclaim_ms": re.compile(r"phase=drain-reclaim-done t=\+\d+ms dt=(\d+)ms"),
    "object_index": re.compile(r"object_index=(\d+)"),
    "pipe_ms": re.compile(r"\[pipe_rw\] ready[^\n]*total_ms=(\d+)"),
    "root_seconds": re.compile(r"Root acquired in ([0-9.]+) seconds"),
    "gate_final": re.compile(r"gate=\d+/\d+ phase=baseline temp=([0-9.]+)C[^\n]*uptime=(\d+)s"),
    "attempt": re.compile(r"exploit completed attempt=(\d+)/(\d+)"),
}

LEGACY_GATE_FINAL = PATTERNS["gate_final"]
COMPACT_GATE = re.compile(
    r"\[launcher\] gate=\d+/\d+ temp=([0-9.]+)C mem=\d+MB"
)
COMPACT_UPTIME = re.compile(r"\[launcher\] uptime=(\d+)s")


def one_or_many(match: re.Match[str] | None) -> Any:
    if not match:
        return None
    groups = match.groups()
    return groups[0] if len(groups) == 1 else list(groups)


def final_gate_metrics(log: str) -> list[str] | None:
    legacy = LEGACY_GATE_FINAL.search(log)
    if legacy:
        return list(legacy.groups())
    samples = list(COMPACT_GATE.finditer(log))
    if not samples:
        return None
    latest = samples[-1]
    next_gate = log.find("\n[launcher] gate=", latest.end())
    window = log[latest.end():next_gate if next_gate >= 0 else None]
    uptime = COMPACT_UPTIME.search(window)
    return [latest.group(1), uptime.group(1)] if uptime else None


def parse_history(path: pathlib.Path, extract_logs: bool) -> dict[str, Any] | None:
    raw = path.read_bytes()
    if not raw:
        return {
            "path": str(path), "classification": "CORRUPT_EMPTY",
            "bytes": 0, "nonzero_bytes": 0,
        }
    try:
        value = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError):
        return {
            "path": str(path), "classification": "CORRUPT_ZERO" if not any(raw) else "CORRUPT_JSON",
            "bytes": len(raw), "nonzero_bytes": sum(byte != 0 for byte in raw),
        }
    if not isinstance(value, dict) or "id" not in value or "log" not in value:
        return None

    raw_log = str(value.get("log", ""))
    log = re.sub(r"\n {2}", " ", raw_log)
    started = value.get("startedAtMillis")
    completed = value.get("completedAtMillis")
    metrics = {name: one_or_many(pattern.search(log)) for name, pattern in PATTERNS.items()}
    metrics["gate_final"] = final_gate_metrics(log)
    wall_seconds = None
    if isinstance(started, int) and isinstance(completed, int):
        wall_seconds = round((completed - started) / 1000, 3)
    succeeded = value.get("result") == "Succeeded"
    temporary_root = "stage=temporary-root-ready" in log
    ksu = "KernelSU control verified" in log
    classification = "SUCCEEDED" if succeeded and temporary_root and ksu else str(value.get("result", "UNKNOWN")).upper()
    log_lines = [line.strip() for line in raw_log.splitlines() if line.strip()]
    last_log_line = log_lines[-1] if log_lines else None
    error_lines = [
        line for line in log_lines
        if re.search(
            r"(?:\[-\]|\b(?:error|erro|falha|failed|rejected|panic|corrupt|timed out|expirou)\b|\btimeout\b(?!\s*=))",
            line,
            re.I,
        )
    ]
    if classification == "FAILED" and last_log_line and re.search(r"\[launcher\] gate=", last_log_line):
        classification = "FAILED_HISTORY_INTERRUPTED_AT_GATE"
    result = {
        "path": str(path),
        "classification": classification,
        "id": value.get("id"),
        "started_at_millis": started,
        "completed_at_millis": completed,
        "wall_seconds": wall_seconds,
        "profile_id": value.get("profileId"),
        "used_shizuku": value.get("usedShizuku", False),
        "last_log_line": last_log_line,
        "error_lines": error_lines,
        "metrics": metrics,
        "extended_reads": log.count("extended read plan"),
        "checks": {
            "aar_aaw_verified": "[aar_aaw] verify ok" in log,
            "fops_restored": bool(re.search(
                r"restore ashmem_misc\.fops .*(?:confirmed|ok)=1", log
            )),
            "pipe_ready": "[pipe_rw] ready" in log,
            "temporary_root": temporary_root,
            "kernelsu_verified": ksu,
            "critical_cpu_invariant_logged": metrics["critical_cpus"] is not None,
        },
    }
    if extract_logs:
        extracted = path.with_suffix("").with_suffix(".log")
        extracted.write_text(
            raw_log + ("\n" if raw_log and not raw_log.endswith("\n") else "")
        )
        result["extracted_log"] = str(extracted)
    return result


def parse_panic_window(path: pathlib.Path, panic_window: str) -> dict[str, Any]:
    causal_markers = (
        "Unable to handle kernel paging request",
        "list_del corruption",
        "list_add corruption",
        "Internal error:",
    )
    starts = [panic_window.rfind(marker) for marker in causal_markers]
    starts = [position for position in starts if position >= 0]
    if starts:
        marker_start = min(starts)
        line_start = panic_window.rfind("\n", 0, marker_start) + 1
        panic_window = panic_window[line_start:]
    text = panic_window

    def first(pattern: str) -> str | None:
        match = re.search(pattern, text)
        return match.group(1) if match else None

    pc = first(r"\bpc\s*:\s*(\S+)")
    lr = first(r"\blr\s*:\s*(\S+)")
    fault = first(r"Unable to handle kernel paging request at virtual address\s+([0-9a-fA-F]+)")
    cpu_info = re.search(r"CPU:\s*(\d+)\s+PID:\s*(\d+)\s+Comm:\s*(\S+)", text)
    owner = first(r"\bx0\s*:\s*([0-9a-fA-F]+)")
    uptime = first(
        r"^\[\s*([0-9.]+)\].*Unable to handle kernel paging request"
    )
    fops_owner = bool(pc and pc.startswith("try_module_get") and lr and lr.startswith("misc_open"))
    workqueue_list = bool(
        pc and pc.startswith("__list_del_entry_valid")
        and "try_to_grab_pending" in text
        and "cancel_work_sync" in text
    )
    return {
        "path": str(path),
        "fault_address": fault,
        "pc": pc,
        "lr": lr,
        "cpu": int(cpu_info.group(1)) if cpu_info else None,
        "pid": int(cpu_info.group(2)) if cpu_info else None,
        "comm": cpu_info.group(3) if cpu_info else None,
        "uptime_seconds": float(uptime) if uptime else None,
        "x0": owner,
        "signature": (
            "MISC_FOPS_OWNER" if fops_owner else
            "WORKQUEUE_LIST_CORRUPTION" if workqueue_list else
            "KERNEL_PANIC"
        ),
        "stage": (
            "first-misc-open-before-aar-proof" if fops_owner else
            "cancel-work-after-temporary-root" if workqueue_list else
            None
        ),
    }


def parse_panics(path: pathlib.Path) -> list[dict[str, Any]]:
    text = path.read_text(errors="replace")
    panic_ends = list(re.finditer(r"Kernel panic[^\n]*", text))
    if not panic_ends:
        if "Unable to handle kernel paging request" not in text:
            return []
        return [parse_panic_window(path, text)]

    results: list[dict[str, Any]] = []
    previous_end = 0
    for panic_end in panic_ends:
        results.append(
            parse_panic_window(path, text[previous_end:panic_end.end()])
        )
        previous_end = panic_end.end()
    return results


def markdown(report: dict[str, Any]) -> str:
    lines = ["# Análise das execuções do app", "", "| Arquivo | Resultado | Total | Factory | Proc spray | Reclaim | Pipe | Root |", "|---|---|---:|---:|---:|---:|---:|---:|"]
    for run in report["runs"]:
        metrics = run.get("metrics", {})
        def metric(name: str, suffix: str = "") -> str:
            value = metrics.get(name)
            if isinstance(value, list):
                value = value[-1]
            return f"{value}{suffix}" if value is not None else "—"
        wall = f"{run['wall_seconds']:.3f}s" if run.get("wall_seconds") is not None else "—"
        lines.append(
            f"| `{pathlib.Path(run['path']).name}` | {run['classification']} | {wall} | "
            f"{metric('factory_ms', 'ms')} | {metric('proc_spray_ms', 'ms')} | "
            f"{metric('reclaim_ms', 'ms')} | {metric('pipe_ms', 'ms')} | "
            f"{metric('root_seconds', 's')} |"
        )
    lines += ["", f"Execuções válidas: **{report['summary']['valid_runs']}**; sucessos: **{report['summary']['successes']}**; históricos corrompidos: **{report['summary']['corrupt_runs']}**.", ""]
    failures = [run for run in report["runs"] if run["classification"].startswith("FAILED")]
    if failures:
        lines += ["## Falhas", ""]
        for run in failures:
            detail = run.get("error_lines", [])[-1:] or [run.get("last_log_line") or "sem log"]
            lines.append(f"- `{pathlib.Path(run['path']).name}`: `{run['classification']}`; última evidência: `{detail[0]}`.")
        lines.append("")
    if report["panics"]:
        lines += ["## Panics", ""]
        for panic in report["panics"]:
            uptime = panic.get("uptime_seconds")
            uptime_text = f", uptime `{uptime:.3f}s`" if uptime is not None else ""
            lines.append(
                f"- `{pathlib.Path(panic['path']).name}`: `{panic['signature']}`, "
                f"CPU `{panic['cpu']}`, PID `{panic['pid']}`{uptime_text}, "
                f"PC `{panic['pc']}`, LR `{panic['lr']}`, fault `{panic['fault_address']}`."
            )
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path)
    parser.add_argument("--json", dest="json_output", type=pathlib.Path)
    parser.add_argument("--markdown", dest="markdown_output", type=pathlib.Path)
    parser.add_argument("--extract-logs", action="store_true")
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error(f"diretório ausente: {args.directory}")

    candidates = sorted({*args.directory.rglob("*.json"), *args.directory.rglob("*.json.corrupt")})
    runs = [result for path in candidates if (result := parse_history(path, args.extract_logs))]
    index_order: dict[str, int] = {}
    index_path = args.directory / "history-index.tsv"
    if index_path.is_file():
        for line in index_path.read_text(errors="replace").splitlines()[1:]:
            fields = line.split("\t")
            if len(fields) >= 2 and fields[0].isdigit():
                index_order[fields[1]] = int(fields[0])
    def run_order(item: dict[str, Any]) -> tuple[int, int, str]:
        name = pathlib.Path(item["path"]).name
        if name in index_order:
            return (0, index_order[name], name)
        numbered = re.search(r"run[-_]?(\d+)", name, re.I)
        if numbered:
            return (1, int(numbered.group(1)), name)
        return (2, -(item.get("started_at_millis") or 0), name)
    runs.sort(key=run_order)
    panic_paths = sorted({path for path in args.directory.rglob("*") if path.is_file() and "kmsg" in path.name.lower()})
    parsed_panics = [
        panic
        for path in panic_paths
        for panic in parse_panics(path)
    ]
    panics = []
    seen_panics: set[tuple[Any, ...]] = set()
    for panic in parsed_panics:
        identity = tuple(panic.get(key) for key in ("fault_address", "pc", "lr", "cpu", "pid", "comm"))
        if identity not in seen_panics:
            seen_panics.add(identity)
            panics.append(panic)
    report = {
        "directory": str(args.directory.resolve()),
        "runs": runs,
        "panics": panics,
        "summary": {
            "runs": len(runs),
            "valid_runs": sum("CORRUPT" not in run["classification"] for run in runs),
            "successes": sum(run["classification"] == "SUCCEEDED" for run in runs),
            "corrupt_runs": sum("CORRUPT" in run["classification"] for run in runs),
            "panics": len(panics),
        },
    }
    json_text = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json_text)
    if args.markdown_output:
        args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
        args.markdown_output.write_text(markdown(report))
    if not args.json_output and not args.markdown_output:
        print(json_text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
