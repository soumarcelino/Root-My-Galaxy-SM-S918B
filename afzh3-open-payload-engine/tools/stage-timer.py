#!/usr/bin/env python3
"""Passthrough filter: echoes stdin unchanged and appends timing to every
milestone line across the whole run -- launcher start, gate phases, payload
stages, and post-exploit steps. Each timed line gets a per-event emoji, the
delta since the previous timed event, and the cumulative time since the run
started. Gate sample lines also get the elapsed time inside the gate. The
original tokens (notably `stage=NAME` and `phase=NAME`) are never altered, so
downstream greps in validate-two-boots.sh still match. Used to wrap runner
stdout before it is tee'd to the boot log, without touching the device
payload."""
import re
import sys
import time

STAGE_RE = re.compile(r"stage=([\w-]+)")
GATE_SAMPLE_RE = re.compile(r"\[launcher\] gate=(\d+)/(\d+) phase=(\w+)")

STAGE_EMOJI = {
    "preparing-kernel-access": "🔧",
    "locating-kernel": "🔎",
    "kernel-location-ready": "📍",
    "verifying-kernel-access": "🔑",
    "starting-temporary-root": "🚀",
    "kernel-mutation-pending": "⚠️",
    "workqueue-mutation-pending": "⚠️",
    "temporary-root-ready": "✅",
    "exploit-start": "💥",
}
DEFAULT_STAGE_EMOJI = "⏱️"

# Ordered: first matching pattern wins. (regex, label, emoji)
MILESTONES = [
    (re.compile(r"\[launcher\] gate (?:relaxado|conservador):"), "launcher-start", "⚙️"),
    (re.compile(r"\[launcher\] pipe-gate=pass"), "pipe-gate", "🚰"),
    (re.compile(r"\[launcher\] cooldown: exigindo"), "cooldown-start", "❄️"),
    (re.compile(r"\[launcher\] estabilidade máxima confirmada"), "gate-completo", "🟢"),
    (re.compile(r"\[launcher\] execve: carregando payload"), "execve", "📦"),
    (re.compile(r"waiting for boot allocator quiet window"), "quiet-window", "🕐"),
    (re.compile(r"starting exploit"), "exploit-loop", "🎯"),
    (re.compile(r"exploit attempt="), "exploit-attempt", "🔁"),
    (re.compile(r"\[pipe_rw\] ready "), "pipe-rw-ready", "🩹"),
    (re.compile(r"Carregando KernelSU"), "kernelsu-load", "🧩"),
    (re.compile(r"KernelSU control verified"), "kernelsu-ok", "🛡️"),
    (re.compile(r"Aguardando su"), "aguardando-su", "⌛"),
    (re.compile(r"Root confirmado"), "root-ok", "🏁"),
]


def main() -> int:
    run_start = None
    last_ts = None
    last_label = None
    gate_start = None

    def annotate(now, emoji, label):
        nonlocal run_start, last_ts, last_label
        if run_start is None:
            run_start = now
        cumulative = now - run_start
        if last_ts is None:
            suffix = f"  {emoji} ⏱️ início | Σ{cumulative:.1f}s"
        else:
            delta = now - last_ts
            suffix = (f"  {emoji} ⏱️ {delta:.3f}s (desde {last_label})"
                      f" | Σ{cumulative:.1f}s")
        last_ts = now
        last_label = label
        return suffix

    for line in sys.stdin:
        now = time.monotonic()
        stripped = line.rstrip("\n")

        gate_m = GATE_SAMPLE_RE.search(line)
        if gate_m:
            if gate_start is None:
                gate_start = now
            in_gate = now - gate_start
            sys.stdout.write(f"{stripped}  ⏳ +{in_gate:.1f}s no gate\n")
            sys.stdout.flush()
            continue

        stage_m = STAGE_RE.search(line)
        if stage_m:
            stage = stage_m.group(1)
            emoji = STAGE_EMOJI.get(stage, DEFAULT_STAGE_EMOJI)
            sys.stdout.write(stripped + annotate(now, emoji, stage) + "\n")
            sys.stdout.flush()
            continue

        matched = False
        for regex, label, emoji in MILESTONES:
            if regex.search(line):
                sys.stdout.write(stripped + annotate(now, emoji, label) + "\n")
                sys.stdout.flush()
                matched = True
                break
        if matched:
            continue

        sys.stdout.write(line)
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
