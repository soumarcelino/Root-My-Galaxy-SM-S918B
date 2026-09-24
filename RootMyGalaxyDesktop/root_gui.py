#!/usr/bin/env python3
"""Qt desktop frontend for the tested open-source AFZH3 root runner."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from html import escape
from pathlib import Path

from PyQt6.QtCore import QProcess, QTimer
from PyQt6.QtGui import QCloseEvent, QFont, QTextCursor
from PyQt6.QtWidgets import (
    QApplication, QComboBox, QFrame, QGridLayout, QHBoxLayout, QLabel,
    QMainWindow, QMessageBox, QProgressBar, QPushButton, QStyleFactory,
    QTextEdit, QVBoxLayout, QWidget,
)


ANSI_ESCAPE = re.compile(r"(?:\x1b|␛)\[[0-?]*[ -/]*[@-~]")
ROOT_RE = re.compile(r"uid=0(?:\(root\))?")
METRIC_RE = re.compile(r"^metric (.+)$", re.MULTILINE)

# Mesmas fontes do gate do launcher (read_device_metrics em
# tools/validate-two-boots.sh), lidas com built-ins do shell para evitar um
# processo por arquivo/campo: MemAvailable, temperatura de thermal_zone,
# loadavg, tarefas executáveis e PSI some/avg10 de cpu/memory/io. Emite uma
# linha "metric k=v ..." consumida por update_metrics; roda dentro do mesmo
# adb shell do probe de root, sem abrir conexão concorrente.
METRICS_SH = (
    "mem=0; while read key amount unit; do "
    "[ \"$key\" = 'MemAvailable:' ] && { mem=$amount; break; }; "
    "done < /proc/meminfo; "
    "read up _rest < /proc/uptime; "
    "read load _load5 _load15 runfield _rest < /proc/loadavg; "
    "run=${runfield%%/*}; "
    "read _ cpu_field _ < /proc/pressure/cpu; cpu=${cpu_field#avg10=}; "
    "read _ mp_field _ < /proc/pressure/memory; mp=${mp_field#avg10=}; "
    "read _ io_field _ < /proc/pressure/io; io=${io_field#avg10=}; "
    "temp=0; for z in /sys/class/thermal/thermal_zone*/temp; do "
    "read v < \"$z\" 2>/dev/null || continue; "
    "case $v in *[!0-9]*|'')continue;; esac; "
    "[ $v -lt 200000 ]&&[ $v -gt $temp ]&&temp=$v; done; "
    "printf 'metric mem=%s temp=%s load=%s run=%s cpu=%s mp=%s io=%s up=%s\\n' "
    "$mem $temp $load $run $cpu $mp $io $up"
)
# Limiares idênticos ao gate host (validate-two-boots.sh:146-152).
METRIC_MIN_MEM_KB = 1048576
METRIC_MAX_TEMP_MC = 45000
METRIC_MAX_RUNNABLE = 8
METRIC_PSI_MAX = {"cpu": 30.0, "mp": 5.0, "io": 10.0}
# neutral vazio: usa cor de texto da palette (adapta ao tema claro/escuro).
METRIC_COLORS = {"ok": "#16a34a", "warn": "#ef4444", "neutral": ""}
STAGES = (
    ("Preparando a mochila", "Enviando helper e payload testados para o celular.", "📦",
     ("preparando payload",)),
    ("Esperando o momento certo", "Launcher local mede temperatura, memória e pressão.", "🌡️",
     ("[launcher] gate ",)),
    ("Iniciando a jornada", "O payload começou; agora cada mudança é acompanhada pelos logs.", "🚀",
     ("starting exploit",)),
    ("Encontrando o kernel", "Descobrindo onde o kernel está carregado neste boot.", "🧭",
     ("stage=locating-kernel",)),
    ("Testando a passagem", "Confirmando acesso de leitura e escrita antes de continuar.", "🔎",
     ("stage=verifying-kernel-access",)),
    ("Abrindo a porta temporária", "A mutação crítica começou; não interrompa esta etapa.", "🔐",
     ("stage=starting-temporary-root",)),
    ("Construindo a ponte", "Montando o canal seguro usado para acessar a memória física.", "🌉",
     ("[pipe_rw]",)),
    ("Ativando o KernelSU", "Carregando o controle de root para este boot.", "⚙️",
     ("carregando kernelsu",)),
    ("Conferindo a conquista", "Executando su -c id: só uid=0 confirma root de verdade.", "✅",
     ("aguardando su", "root confirmado")),
)


def locate_runner() -> Path:
    override = os.environ.get("ROOT_MY_GALAXY_RUNNER")
    if override:
        return Path(override).expanduser().resolve()
    app_dir = Path(__file__).resolve().parent
    return app_dir.parent / "simple-root" / "simple-root.sh"


class RootWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.runner = locate_runner()
        self.process: QProcess | None = None
        self.verify_process: QProcess | None = None
        self.root_probe_process: QProcess | None = None
        self.reboot_process: QProcess | None = None
        self.reconnect_process: QProcess | None = None
        self.selected_serial = ""
        self.runner_exit_code: int | None = None
        self.mutation_possible = False
        self.reboot_required = False
        self.current_boot_id = ""
        state_home = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state"))
        self.unsafe_boot_path = state_home / "root-my-galaxy" / "unsafe-boot-id"
        self.stage_index = 0
        self._stdout_buffer = ""
        self._stderr_buffer = ""
        self._last_log_time: float | None = None
        self.setWindowTitle("Root My Galaxy · AFZH3 Open Source")
        self.resize(980, 680)
        self.setMinimumSize(780, 520)
        self._build_ui()
        self.log_timer = QTimer(self)
        self.log_timer.setInterval(100)
        self.log_timer.timeout.connect(self.update_log_timer)
        self.log_timer.start()
        self.root_timer = QTimer(self)
        self.root_timer.setInterval(2000)
        self.root_timer.timeout.connect(self.check_root_status)
        self.refresh_devices()
        self.root_timer.start()

    def _build_ui(self) -> None:
        central = QWidget(self)
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        header = QFrame()
        header.setObjectName("header")
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(22, 14, 22, 14)
        titles = QVBoxLayout()
        title = QLabel("Root My Galaxy")
        title.setObjectName("title")
        subtitle = QLabel("Payload aberto AFZH3 · execução e logs em tempo real")
        subtitle.setObjectName("subtitle")
        titles.addWidget(title)
        titles.addWidget(subtitle)
        header_layout.addLayout(titles)
        header_layout.addStretch()
        self.result_badge = QLabel("PRONTO")
        self.result_badge.setObjectName("badge")
        header_layout.addWidget(self.result_badge)
        root.addWidget(header)

        body = QVBoxLayout()
        body.setContentsMargins(22, 16, 22, 18)
        body.setSpacing(12)
        root.addLayout(body, 1)

        device_row = QHBoxLayout()
        self.devices = QComboBox()
        self.devices.setMinimumWidth(430)
        self.devices.currentIndexChanged.connect(self.device_changed)
        self.refresh_button = QPushButton("🔄 Atualizar")
        self.refresh_button.clicked.connect(self.refresh_devices)
        self.reboot_button = QPushButton("♻️ Reiniciar")
        self.reboot_button.clicked.connect(self.reboot_device)
        device_row.addWidget(QLabel("📱 Dispositivo ADB:"))
        device_row.addWidget(self.devices, 1)
        device_row.addWidget(self.refresh_button)
        device_row.addWidget(self.reboot_button)
        body.addLayout(device_row)

        live_state = QFrame()
        live_state.setObjectName("liveState")
        live_layout = QHBoxLayout(live_state)
        live_layout.setContentsMargins(12, 8, 12, 8)
        self.connection_label = QLabel("🔌 ADB · verificando")
        self.boot_label = QLabel("🤖 Sistema · verificando")
        self.root_live_label = QLabel("🔓 Root · verificando")
        live_layout.addWidget(self.connection_label)
        live_layout.addStretch()
        live_layout.addWidget(self.boot_label)
        live_layout.addStretch()
        live_layout.addWidget(self.root_live_label)
        body.addWidget(live_state)

        body.addWidget(self._build_metrics_panel())

        stage_card = QFrame()
        stage_card.setObjectName("stageCard")
        stage_layout = QHBoxLayout(stage_card)
        stage_layout.setContentsMargins(14, 11, 14, 11)
        self.stage_icon = QLabel("🌱")
        self.stage_icon.setObjectName("stageIcon")
        self.stage_icon.setFixedWidth(42)
        stage_text = QVBoxLayout()
        stage_text.setSpacing(2)
        self.stage_label = QLabel("Pronto para começar")
        self.stage_label.setObjectName("stage")
        self.stage_description = QLabel(
            "Escolha o dispositivo e acompanhe cada passo até a prova de root."
        )
        self.stage_description.setObjectName("stageDescription")
        self.stage_description.setWordWrap(True)
        stage_text.addWidget(self.stage_label)
        stage_text.addWidget(self.stage_description)
        stage_layout.addWidget(self.stage_icon)
        stage_layout.addLayout(stage_text, 1)
        body.addWidget(stage_card)
        self.progress = QProgressBar()
        self.progress.setRange(0, len(STAGES))
        self.progress.setValue(0)
        self.progress.setFormat("%v/%m etapas")
        body.addWidget(self.progress)

        self.output = QTextEdit()
        self.output.setReadOnly(True)
        self.output.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
        font = QFont("JetBrainsMono Nerd Font Mono")
        font.setStyleHint(QFont.StyleHint.Monospace)
        font.setPointSizeF(10.5)
        font.setFixedPitch(True)
        self.output.setFont(font)
        body.addWidget(self.output, 1)

        controls = QHBoxLayout()
        self.status_label = QLabel("Selecione dispositivo e execute.")
        controls.addWidget(self.status_label, 1)
        self.stop_button = QPushButton("Parar")
        self.stop_button.setEnabled(False)
        self.stop_button.clicked.connect(self.stop_run)
        self.start_button = QPushButton("Executar payload")
        self.start_button.setDefault(True)
        self.start_button.clicked.connect(self.start_run)
        controls.addWidget(self.stop_button)
        controls.addWidget(self.start_button)
        body.addLayout(controls)

        self.setStyleSheet("""
            QMainWindow, QWidget { font-size: 13px; }
            #header { border-bottom: 1px solid palette(mid); }
            #title { font-size: 24px; font-weight: 650; }
            #subtitle { color: #a1a1aa; }
            #stage { font-size: 16px; font-weight: 600; }
            #stageCard { background: palette(alternate-base);
                         border: 1px solid palette(mid); border-radius: 10px; }
            #stageIcon { font-size: 26px; }
            #stageDescription { color: #a1a1aa; }
            #liveState { border: 1px solid palette(mid); border-radius: 8px; }
            #metricsCard { background: palette(alternate-base);
                           border: 1px solid palette(mid); border-radius: 10px; }
            #metricTitle { font-weight: 700; color: palette(text); font-size: 13px;
                           padding-bottom: 2px; }
            #metricCaption { color: #8b8ea0; font-size: 11px; font-weight: 600; }
            #metricValue { font-size: 17px; font-weight: 800;
                           font-family: "JetBrainsMono Nerd Font Mono", monospace; }
            #badge { border: 1px solid palette(mid); border-radius: 10px;
                     padding: 6px 12px; font-weight: 700; }
            QTextEdit { background: #0d0f18; color: #fffaf3;
                        border: 1px solid #343746; border-radius: 8px;
                        padding: 9px; }
            QPushButton { padding: 7px 15px; }
            QComboBox { padding: 6px 10px; }
        """)

    # Ordem, emoji e rótulos do painel realtime; chave casa com METRICS_SH.
    METRIC_FIELDS = (
        ("mem", "🧠 Memória livre"),
        ("temp", "🌡️ Temperatura"),
        ("run", "🏃 Tarefas exec."),
        ("load", "📈 Carga 1m"),
        ("cpu", "⚡ PSI CPU"),
        ("mp", "💾 PSI memória"),
        ("io", "💽 PSI I/O"),
        ("up", "⏱️ Uptime"),
    )
    # Emoji de estado anexado ao valor.
    STATE_EMOJI = {"ok": "✅", "warn": "⚠️", "neutral": "➖"}

    def _build_metrics_panel(self) -> QFrame:
        card = QFrame()
        card.setObjectName("metricsCard")
        grid = QGridLayout(card)
        grid.setContentsMargins(12, 9, 12, 9)
        grid.setHorizontalSpacing(18)
        grid.setVerticalSpacing(6)
        title = QLabel("📊 Telemetria do dispositivo · tempo real (gate)")
        title.setObjectName("metricTitle")
        grid.addWidget(title, 0, 0, 1, 4)
        self.metric_labels: dict[str, QLabel] = {}
        for index, (key, caption) in enumerate(self.METRIC_FIELDS):
            row = 1 + index // 4
            col = index % 4
            cell = QVBoxLayout()
            cell.setSpacing(1)
            caption_label = QLabel(caption)
            caption_label.setObjectName("metricCaption")
            value_label = QLabel("—")
            value_label.setObjectName("metricValue")
            cell.addWidget(caption_label)
            cell.addWidget(value_label)
            grid.addLayout(cell, row, col)
            self.metric_labels[key] = value_label
        return card

    def _set_metric(self, key: str, text: str, state: str) -> None:
        label = self.metric_labels.get(key)
        if label is None:
            return
        emoji = self.STATE_EMOJI[state]
        suffix = f"  {emoji}" if text != "—" else ""
        label.setText(f"{text}{suffix}")
        color = METRIC_COLORS[state]
        # neutral sem cor fixa: herda texto da palette (legível claro/escuro).
        label.setStyleSheet(f"color: {color};" if color else "")

    def clear_metrics(self) -> None:
        for key in self.metric_labels:
            self._set_metric(key, "—", "neutral")

    def update_metrics(self, raw: str) -> None:
        match = METRIC_RE.search(raw)
        if not match:
            self.clear_metrics()
            return
        values: dict[str, str] = {}
        for token in match.group(1).split():
            if "=" in token:
                name, _, value = token.partition("=")
                values[name] = value

        def as_int(name: str) -> int | None:
            try:
                return int(values.get(name, ""))
            except ValueError:
                return None

        def as_float(name: str) -> float | None:
            try:
                return float(values.get(name, ""))
            except ValueError:
                return None

        mem_kb = as_int("mem")
        if mem_kb is None:
            self._set_metric("mem", "—", "neutral")
        else:
            self._set_metric(
                "mem", f"{mem_kb / 1048576:.2f} GiB",
                "ok" if mem_kb >= METRIC_MIN_MEM_KB else "warn",
            )

        temp_mc = as_int("temp")
        if temp_mc is None:
            self._set_metric("temp", "—", "neutral")
        elif temp_mc == 0:
            self._set_metric("temp", "n/d", "neutral")
        else:
            self._set_metric(
                "temp", f"{temp_mc / 1000:.1f}°C",
                "ok" if temp_mc <= METRIC_MAX_TEMP_MC else "warn",
            )

        runnable = as_int("run")
        if runnable is None:
            self._set_metric("run", "—", "neutral")
        else:
            self._set_metric(
                "run", str(runnable),
                "ok" if runnable <= METRIC_MAX_RUNNABLE else "warn",
            )

        load = as_float("load")
        self._set_metric("load", f"{load:.2f}" if load is not None else "—", "neutral")

        for key in ("cpu", "mp", "io"):
            value = as_float(key)
            if value is None:
                self._set_metric(key, "—", "neutral")
            else:
                self._set_metric(
                    key, f"{value:.1f}",
                    "ok" if value <= METRIC_PSI_MAX[key] else "warn",
                )

        up = as_float("up")
        if up is None:
            self._set_metric("up", "—", "neutral")
        else:
            self._set_metric("up", f"{int(up) // 60}m{int(up) % 60:02d}s", "neutral")

    def refresh_devices(self) -> None:
        selected = str(self.devices.currentData() or "")
        self.devices.blockSignals(True)
        self.devices.clear()
        if not shutil.which("adb"):
            self.devices.addItem("adb não encontrado")
            self.devices.blockSignals(False)
            self.start_button.setEnabled(False)
            self.reboot_button.setEnabled(False)
            self.set_result("ERRO", "#dc2626", "Instale Android platform-tools.")
            return
        try:
            result = subprocess.run(
                ["adb", "devices", "-l"], text=True, capture_output=True,
                timeout=5, check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            self.devices.addItem(f"Falha ao consultar adb: {exc}")
            self.devices.blockSignals(False)
            self.start_button.setEnabled(False)
            return

        online = []
        for line in result.stdout.splitlines()[1:]:
            fields = line.split()
            if len(fields) < 2 or fields[1] != "device":
                continue
            serial = fields[0]
            model = next(
                (item.removeprefix("model:") for item in fields if item.startswith("model:")),
                "Android",
            )
            online.append((serial, model))
            self.devices.addItem(f"{model} · {serial}", serial)
        if not online:
            self.devices.addItem("Nenhum dispositivo autorizado")
        elif selected:
            index = self.devices.findData(selected)
            if index >= 0:
                self.devices.setCurrentIndex(index)
        self.devices.blockSignals(False)
        self.device_changed()

    def device_changed(self) -> None:
        serial = self.devices.currentData()
        self.selected_serial = str(serial or "")
        idle = self.process is None and self.verify_process is None
        self.start_button.setEnabled(False)
        self.reboot_button.setEnabled(bool(serial) and idle)
        if serial:
            self.connection_label.setText("🔌 ADB · conectado")
            self.boot_label.setText("🤖 Sistema · verificando")
            self.root_live_label.setText("🔓 Root · verificando")
            self.status_label.setStyleSheet("")
            self.status_label.setText(f"Verificando estado de {serial}…")
            QTimer.singleShot(0, self.check_root_status)
        else:
            self.connection_label.setText("🔌 ADB · desconectado")
            self.boot_label.setText("🤖 Sistema · indisponível")
            self.root_live_label.setText("🔓 Root · desconhecido")
            self.reboot_button.setEnabled(False)
            self.clear_metrics()

    def check_root_status(self) -> None:
        if (
            not self.selected_serial
            or self.root_probe_process is not None
            or self.process is not None
            or self.verify_process is not None
            or self.reboot_process is not None
            or self.reconnect_process is not None
        ):
            return
        self.root_probe_process = QProcess(self)
        self.root_probe_process.setProcessChannelMode(
            QProcess.ProcessChannelMode.SeparateChannels
        )
        self.root_probe_process.finished.connect(self.root_probe_finished)
        self.root_probe_process.errorOccurred.connect(self.root_probe_error)
        script = (
            "printf 'boot=%s\\n' \"$(getprop sys.boot_completed)\"; "
            "read boot_id < /proc/sys/kernel/random/boot_id; "
            "printf 'boot_id=%s\\n' \"$boot_id\"; "
            + METRICS_SH + "; "
            "/system/bin/su -c id 2>/dev/null || true"
        )
        self.root_probe_process.start(
            "adb", ["-s", self.selected_serial, "shell", script]
        )

    def root_probe_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        if not self.root_probe_process:
            return
        stdout = bytes(self.root_probe_process.readAllStandardOutput()).decode(errors="replace")
        self.root_probe_process.deleteLater()
        self.root_probe_process = None
        connected = exit_code == 0 and "boot=" in stdout
        if connected:
            self.update_metrics(stdout)
        else:
            self.clear_metrics()
        booted = "boot=1" in stdout
        rooted = bool(ROOT_RE.search(stdout))
        boot_id_match = re.search(r"^boot_id=([0-9a-f-]+)$", stdout, re.MULTILINE)
        boot_id = boot_id_match.group(1) if boot_id_match else ""
        try:
            unsafe_boot_id = self.unsafe_boot_path.read_text().strip()
        except OSError:
            unsafe_boot_id = ""
        if boot_id and unsafe_boot_id:
            if boot_id == unsafe_boot_id:
                self.reboot_required = True
                self.mutation_possible = True
            else:
                self.clear_unsafe_boot()
        if boot_id and self.current_boot_id and boot_id != self.current_boot_id:
            self.reboot_required = False
            self.mutation_possible = False
        if boot_id:
            self.current_boot_id = boot_id
        self.connection_label.setText("🔌 ADB · conectado" if connected else "🔌 ADB · desconectado")
        self.boot_label.setText("🤖 Sistema · pronto" if booted else "🤖 Sistema · iniciando")
        self.root_live_label.setText("🔓 Root · ativo" if rooted else "🔓 Root · inativo")
        if rooted:
            self.reboot_required = False
            self.clear_unsafe_boot()
            self.set_result("ROOT ATIVO", "#16a34a", "Monitor: uid=0 confirmado.")
            self.start_button.setEnabled(False)
        elif self.reboot_required:
            self.set_result(
                "REBOOT NECESSÁRIO", "#dc2626",
                "Kernel alterado; reinicie antes de outra tentativa."
            )
            self.start_button.setEnabled(False)
        elif booted:
            self.set_result("SEM ROOT", "#dc2626", "Sistema pronto para executar o payload.")
            self.start_button.setEnabled(True)
        else:
            self.set_result("INICIANDO", "#d97706", "Aguardando Android concluir o boot…")
            self.start_button.setEnabled(False)
        self.reboot_button.setEnabled(connected)

    def root_probe_error(self, _error: QProcess.ProcessError) -> None:
        if self.root_probe_process:
            self.root_probe_process.deleteLater()
            self.root_probe_process = None
        self.connection_label.setText("🔌 ADB · erro")
        self.root_live_label.setText("🔓 Root · desconhecido")
        self.clear_metrics()

    def mark_unsafe_boot(self) -> None:
        if not self.current_boot_id:
            return
        try:
            self.unsafe_boot_path.parent.mkdir(parents=True, exist_ok=True)
            self.unsafe_boot_path.write_text(self.current_boot_id + "\n")
        except OSError as exc:
            self.append_log(f"[GUI] Não foi possível persistir estado crítico: {exc}", True)

    def clear_unsafe_boot(self) -> None:
        try:
            self.unsafe_boot_path.unlink(missing_ok=True)
        except OSError:
            pass

    def start_run(self) -> None:
        if not self.selected_serial:
            QMessageBox.warning(self, "ADB", "Selecione um dispositivo autorizado.")
            return
        if not self.runner.is_file() or not os.access(self.runner, os.X_OK):
            self.set_result("ERRO", "#dc2626", f"Runner ausente: {self.runner}")
            return
        if self.reboot_required:
            QMessageBox.warning(
                self, "Reboot necessário",
                "Uma tentativa alterou o kernel neste boot. Reinicie o celular "
                "antes de executar novamente.",
            )
            return

        self.output.clear()
        self._last_log_time = None
        self.progress.setValue(0)
        self.stage_index = 0
        self.runner_exit_code = None
        self.mutation_possible = False
        self._stdout_buffer = ""
        self._stderr_buffer = ""
        self.stage_icon.setText("🎬")
        self.stage_label.setText("Preparando a execução")
        self.stage_description.setText(
            "Validando runner, payload e conexão ADB antes do primeiro passo."
        )
        self.set_result("EXECUTANDO", "#d97706", "Validando runner e dispositivo…")
        self.append_log(f"[GUI] Runner: {self.runner}")
        payload = self.runner.parent / "assets" / "payload.so"
        if payload.is_file():
            digest = hashlib.sha256(payload.read_bytes()).hexdigest()
            self.append_log(f"[GUI] Payload SHA-256: {digest}")
        launcher = self.runner.parent / "assets" / "stability-launcher"
        if launcher.is_file():
            digest = hashlib.sha256(launcher.read_bytes()).hexdigest()
            self.append_log(f"[GUI] Launcher SHA-256: {digest}")
        self.append_log("[GUI] Tela não será apagada. Aplicativos não serão encerrados.")

        # The device-side C launcher owns the stability gate. Stop the live
        # root poll first so no competing adb shell perturbs its samples.
        self.root_timer.stop()
        if self.root_probe_process:
            probe = self.root_probe_process
            self.root_probe_process = None
            probe.blockSignals(True)
            probe.kill()
            probe.waitForFinished(1000)
            probe.deleteLater()
        self.start_button.setEnabled(False)
        self.refresh_button.setEnabled(False)
        self.reboot_button.setEnabled(False)
        self.stop_button.setEnabled(True)
        self.start_runner()

    def start_runner(self) -> None:
        self.set_result("AGUARDANDO", "#d97706", "Launcher aguardará estabilidade máxima…")

        self.process = QProcess(self)
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self.process.readyReadStandardOutput.connect(self.read_stdout)
        self.process.readyReadStandardError.connect(self.read_stderr)
        self.process.finished.connect(self.runner_finished)
        self.process.errorOccurred.connect(self.runner_error)
        self.process.start("setsid", [str(self.runner), self.selected_serial])
        self.process.closeWriteChannel()

    def reboot_device(self) -> None:
        if not self.selected_serial or self.process is not None:
            return
        answer = QMessageBox.question(
            self, "Reiniciar dispositivo",
            "Reiniciar o dispositivo agora? O root ativo será perdido até nova execução.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Cancel,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        self.append_log(f"[GUI] Reiniciando {self.selected_serial}…")
        self.set_result("REINICIANDO", "#d97706", "Comando adb reboot em andamento…")
        self.stage_icon.setText("🔄")
        self.stage_label.setText("Reiniciando o celular")
        self.stage_description.setText(
            "Aguardando o Android voltar e o monitor confirmar o novo estado."
        )
        self.connection_label.setText("🔌 ADB · reiniciando")
        self.boot_label.setText("🤖 Sistema · reiniciando")
        self.root_live_label.setText("🔓 Root · será removido")
        self.start_button.setEnabled(False)
        self.refresh_button.setEnabled(False)
        self.reboot_button.setEnabled(False)
        self.reboot_process = QProcess(self)
        self.reboot_process.finished.connect(self.reboot_command_finished)
        self.reboot_process.errorOccurred.connect(self.reboot_error)
        self.reboot_process.start("adb", ["-s", self.selected_serial, "reboot"])

    def reboot_command_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        if self.reboot_process:
            self.reboot_process.deleteLater()
            self.reboot_process = None
        if exit_code != 0:
            self.set_result("ERRO", "#dc2626", f"adb reboot falhou: código {exit_code}.")
            self.refresh_button.setEnabled(True)
            self.reboot_button.setEnabled(True)
            return
        self.append_log("[GUI] Reboot aceito; aguardando ADB reconectar…")
        self.connection_label.setText("🔌 ADB · aguardando")
        self.boot_label.setText("🤖 Sistema · iniciando")
        self.root_live_label.setText("🔓 Root · inativo")
        self.reconnect_process = QProcess(self)
        self.reconnect_process.finished.connect(self.reconnect_finished)
        self.reconnect_process.errorOccurred.connect(self.reconnect_error)
        self.reconnect_process.start(
            "adb", ["-s", self.selected_serial, "wait-for-device"]
        )

    def reboot_error(self, _error: QProcess.ProcessError) -> None:
        message = self.reboot_process.errorString() if self.reboot_process else "erro desconhecido"
        if self.reboot_process:
            self.reboot_process.deleteLater()
            self.reboot_process = None
        self.set_result("ERRO", "#dc2626", f"Falha ao iniciar adb reboot: {message}")
        self.refresh_button.setEnabled(True)
        self.reboot_button.setEnabled(bool(self.selected_serial))

    def reconnect_finished(self, _exit_code: int, _status: QProcess.ExitStatus) -> None:
        if self.reconnect_process:
            self.reconnect_process.deleteLater()
            self.reconnect_process = None
        self.append_log("[GUI] ADB reconectado; aguardando Android finalizar boot.")
        self.connection_label.setText("🔌 ADB · conectado")
        self.refresh_button.setEnabled(True)
        QTimer.singleShot(1500, self.check_root_status)

    def reconnect_error(self, _error: QProcess.ProcessError) -> None:
        message = self.reconnect_process.errorString() if self.reconnect_process else "erro desconhecido"
        if self.reconnect_process:
            self.reconnect_process.deleteLater()
            self.reconnect_process = None
        self.set_result("DESCONECTADO", "#dc2626", f"ADB não reconectou: {message}")
        self.refresh_button.setEnabled(True)

    def read_stdout(self) -> None:
        if self.process:
            self._stdout_buffer = self.consume_bytes(
                self._stdout_buffer, bytes(self.process.readAllStandardOutput()), False
            )

    def read_stderr(self) -> None:
        if self.process:
            self._stderr_buffer = self.consume_bytes(
                self._stderr_buffer, bytes(self.process.readAllStandardError()), True
            )

    def consume_bytes(self, pending: str, raw: bytes, error: bool) -> str:
        text = pending + raw.decode(errors="replace").replace("\r", "\n")
        lines = text.split("\n")
        for line in lines[:-1]:
            if line:
                self.append_log(line, error)
        return lines[-1]

    def flush_buffers(self) -> None:
        for line, error in ((self._stdout_buffer, False), (self._stderr_buffer, True)):
            if line:
                self.append_log(line, error)
        self._stdout_buffer = ""
        self._stderr_buffer = ""

    def append_log(self, line: str, error: bool = False) -> None:
        clean = ANSI_ESCAPE.sub("", line).strip("\r")
        if not clean:
            return
        now = time.monotonic()
        self.update_log_timer(now)
        self._last_log_time = now
        self.update_stage(clean)
        lowered = clean.lower()
        adb_progress = "file pushed" in lowered or "file pulled" in lowered
        semantic_error = (
            (error and not adb_progress)
            or "falhou" in lowered or "erro" in lowered
            or "ausente" in lowered or "inválido" in lowered
        )
        color = "#ef4444" if semantic_error else (
            "#22c55e" if "[+]" in clean or ROOT_RE.search(clean) else "#fffaf3"
        )
        self.output.append(f'<span style="color:{color}">[00:00:00.000] {escape(clean)}</span>')
        bar = self.output.verticalScrollBar()
        bar.setValue(bar.maximum())

    def update_log_timer(self, now: float | None = None) -> None:
        if self._last_log_time is None:
            return
        block = self.output.document().lastBlock()
        match = re.match(r"\[\d+:\d{2}:\d{2}\.\d{3}\]", block.text())
        if not match:
            return
        elapsed_ms = int(((now if now is not None else time.monotonic()) - self._last_log_time) * 1000)
        hours, remainder = divmod(elapsed_ms, 3_600_000)
        minutes, remainder = divmod(remainder, 60_000)
        seconds, milliseconds = divmod(remainder, 1000)
        timestamp = f"[{hours:02d}:{minutes:02d}:{seconds:02d}.{milliseconds:03d}]"
        if match.group() == timestamp:
            return
        cursor = QTextCursor(block)
        cursor.movePosition(
            QTextCursor.MoveOperation.Right,
            QTextCursor.MoveMode.KeepAnchor,
            len(match.group()),
        )
        cursor.insertText(timestamp)

    def update_stage(self, line: str) -> None:
        lowered = line.lower()
        if lowered.startswith("[launcher] gate="):
            match = re.search(
                r"gate=(\d+/\d+).*temp=([^ ]+) mem=([^ ]+) runnable=(\d+).*"
                r"psi=([^ ]+)", line
            )
            if match:
                self.stage_description.setText(
                    f"Estável {match.group(1)} · {match.group(2)} · "
                    f"{match.group(3)} livres · {match.group(4)} tarefas · "
                    f"PSI {match.group(5)}"
                )
        elif "[launcher] estabilidade confirmada" in lowered:
            self.stage_description.setText(
                "Métricas, slab e capacidade de pipes foram aprovados."
            )
        elif "[launcher] pipe-gate=pass" in lowered:
            self.stage_description.setText(
                "Capacidade de 480 pipes aprovada; confirmando estabilidade final."
            )
        if "stage=kernel-mutation-pending" in lowered:
            self.mutation_possible = True
            self.reboot_required = True
            self.mark_unsafe_boot()
            self.stop_button.setEnabled(False)
            self.status_label.setText(
                "Mutação kernel possível; aguarde conclusão ou reboot automático."
            )
        for index, (label, description, icon, markers) in enumerate(STAGES, start=1):
            if index > self.stage_index and any(marker in lowered for marker in markers):
                self.stage_index = index
                self.progress.setValue(index)
                self.stage_label.setText(label)
                self.stage_description.setText(description)
                self.stage_icon.setText(icon)

    def runner_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        self.read_stdout()
        self.read_stderr()
        self.flush_buffers()
        self.runner_exit_code = exit_code
        self.append_log(f"[GUI] Runner finalizou com código {exit_code}.", exit_code != 0)
        if self.process:
            self.process.deleteLater()
            self.process = None
        self.stop_button.setEnabled(False)
        self.start_verification()

    def runner_error(self, _error: QProcess.ProcessError) -> None:
        if self.process:
            self.append_log(f"[GUI] Falha ao iniciar runner: {self.process.errorString()}", True)
            if self.process.error() == QProcess.ProcessError.FailedToStart:
                self.process.deleteLater()
                self.process = None
                self.set_result("NÃO EXECUTADO", "#dc2626", "Runner não iniciou.")
                self.stop_button.setEnabled(False)
                self.refresh_button.setEnabled(True)
                self.reboot_button.setEnabled(bool(self.selected_serial))
                self.start_button.setEnabled(
                    bool(self.selected_serial) and not self.reboot_required
                )
                self.root_timer.start()

    def start_verification(self) -> None:
        self.stage_index = len(STAGES)
        self.progress.setValue(len(STAGES))
        self.stage_label.setText("Conferindo a conquista")
        self.stage_description.setText(
            "Executando uma prova independente: adb shell su -c id."
        )
        self.stage_icon.setText("✅")
        self.append_log("[GUI] Prova final: adb shell su -c id")
        self.verify_process = QProcess(self)
        self.verify_process.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self.verify_process.finished.connect(self.verification_finished)
        self.verify_process.errorOccurred.connect(self.verification_error)
        self.verify_process.start(
            "adb", ["-s", self.selected_serial, "shell", "/system/bin/su", "-c", "id"]
        )

    def verification_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        if not self.verify_process:
            return
        stdout = bytes(self.verify_process.readAllStandardOutput()).decode(errors="replace").strip()
        stderr = bytes(self.verify_process.readAllStandardError()).decode(errors="replace").strip()
        if stdout:
            self.append_log(stdout)
        if stderr:
            self.append_log(stderr, True)
        rooted = exit_code == 0 and bool(ROOT_RE.search(stdout))
        self.verify_process.deleteLater()
        self.verify_process = None
        self.root_timer.start()
        self.refresh_button.setEnabled(True)
        self.reboot_button.setEnabled(True)
        self.stop_button.setEnabled(False)
        if rooted:
            self.reboot_required = False
            self.clear_unsafe_boot()
            self.set_result("ROOT ATIVO", "#16a34a", "Sucesso: uid=0 confirmado.")
            self.stage_label.setText("Root conquistado")
            self.stage_description.setText(
                "Tudo certo: o celular respondeu como uid=0 neste boot."
            )
            self.stage_icon.setText("🎉")
            self.start_button.setEnabled(False)
        else:
            runner_note = f" Runner={self.runner_exit_code}." if self.runner_exit_code is not None else ""
            if self.reboot_required:
                self.set_result(
                    "REBOOT NECESSÁRIO", "#dc2626",
                    f"Kernel alterado; não tente novamente neste boot.{runner_note}"
                )
                self.stage_label.setText("Hora de recomeçar com segurança")
                self.stage_description.setText(
                    "Reinicie o celular antes de uma nova tentativa; o botão executar está bloqueado."
                )
                self.stage_icon.setText("🔄")
                self.start_button.setEnabled(False)
            else:
                self.set_result("SEM ROOT", "#dc2626", f"Falha: uid=0 não confirmado.{runner_note}")
                self.stage_label.setText("A jornada não terminou")
                self.stage_description.setText(
                    "O celular não confirmou uid=0. Consulte os logs para localizar a etapa que falhou."
                )
                self.stage_icon.setText("🧩")
                self.start_button.setEnabled(bool(self.selected_serial))
        QTimer.singleShot(500, self.check_root_status)

    def verification_error(self, _error: QProcess.ProcessError) -> None:
        message = self.verify_process.errorString() if self.verify_process else "erro desconhecido"
        self.append_log(f"[GUI] Falha na verificação final: {message}", True)
        if self.verify_process:
            self.verify_process.deleteLater()
            self.verify_process = None
        self.set_result("SEM PROVA", "#dc2626", "Não foi possível executar su -c id.")
        self.refresh_button.setEnabled(True)
        self.reboot_button.setEnabled(bool(self.selected_serial))
        self.start_button.setEnabled(bool(self.selected_serial) and not self.reboot_required)
        self.root_timer.start()
        QTimer.singleShot(500, self.check_root_status)

    def set_result(self, badge: str, color: str, status: str) -> None:
        self.result_badge.setText(badge)
        self.result_badge.setStyleSheet(
            f"color: {color}; border: 1px solid {color}; border-radius: 10px; "
            "padding: 6px 12px; font-weight: 700;"
        )
        self.status_label.setText(status)
        self.status_label.setStyleSheet(f"color: {color}; font-weight: 600;")

    def stop_run(self) -> None:
        if not self.process or self.process.state() == QProcess.ProcessState.NotRunning:
            return
        if self.mutation_possible:
            QMessageBox.warning(
                self, "Execução crítica",
                "O kernel pode já ter sido alterado. Interrupção bloqueada; "
                "aguarde o runner terminar.",
            )
            return
        self.append_log("[GUI] Interrompendo grupo do runner…", True)
        pid = int(self.process.processId())
        try:
            os.killpg(pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            self.process.terminate()
        QTimer.singleShot(2000, self.force_stop)

    def force_stop(self) -> None:
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            pid = int(self.process.processId())
            try:
                os.killpg(pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                self.process.kill()

    def closeEvent(self, event: QCloseEvent) -> None:
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            if self.mutation_possible:
                QMessageBox.warning(
                    self, "Execução crítica",
                    "Janela não pode ser fechada após possível mutação kernel. "
                    "Aguarde o runner terminar.",
                )
                event.ignore()
                return
            self.stop_run()
            self.process.waitForFinished(2500)
        if self.verify_process:
            self.verify_process.kill()
            self.verify_process.waitForFinished(1000)
        for process in (
            self.root_probe_process, self.reboot_process, self.reconnect_process,
        ):
            if process:
                process.kill()
                process.waitForFinished(1000)
        event.accept()


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("Root My Galaxy AFZH3")
    if "Breeze" in QStyleFactory.keys():
        app.setStyle(QStyleFactory.create("Breeze"))
    window = RootWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
