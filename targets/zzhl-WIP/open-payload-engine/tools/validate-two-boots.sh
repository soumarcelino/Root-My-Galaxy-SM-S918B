#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
  cat <<'EOF'
Uso: validate-two-boots.sh [opções]

Por padrão mostra o plano. --execute realiza dois reboots limpos e uma
execução do runner por boot, com retry apenas se comprovadamente pré-mutação.

Opções obrigatórias com --execute:
  --serial SERIAL           Serial ADB explícito.
  --expect-build BUILD      Build incremental exato.
  --expect-sha256 HASH      SHA-256 esperado do assets/ksu-payload.

Outras opções:
  --runner ARQUIVO          Obrigatório no clone ZZHL; deve usar payload ZZHL.
  --evidence-dir DIR        Padrão: evidence/campaigns/<UTC>.
  --timeout SEGUNDOS        Timeout por runner; padrão 600.
  --pre-mutation-retries N  Novas execuções somente antes de mutação; padrão 1.
  --quiet-seconds N         Janela quieta após boot/entre retries; padrão 120.
  --skip-host-quiet         Delega estabilidade exclusivamente ao launcher.
  --max-temp-mc N           Temperatura máxima para executar; padrão 45000.
  --min-mem-kb N            MemAvailable mínima; padrão 1048576.
  --boots N                 Quantidade de boots: 1 ou 2; padrão 2.
  --execute                 Autoriza reboots e execução do payload.
  -h, --help                Mostra esta ajuda.
EOF
}

serial=
expect_build=
expect_sha256=
runner=
evidence_dir=
runner_timeout=600
pre_mutation_retries=1
quiet_seconds=120
max_temp_mc=45000
min_mem_kb=1048576
boot_count=2
skip_host_quiet=0
execute=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --expect-build) expect_build="${2:?build ausente}"; shift 2 ;;
    --expect-sha256) expect_sha256="${2:?hash ausente}"; shift 2 ;;
    --runner) runner="${2:?runner ausente}"; shift 2 ;;
    --evidence-dir) evidence_dir="${2:?diretório ausente}"; shift 2 ;;
    --timeout) runner_timeout="${2:?timeout ausente}"; shift 2 ;;
    --pre-mutation-retries) pre_mutation_retries="${2:?retries ausente}"; shift 2 ;;
    --quiet-seconds) quiet_seconds="${2:?janela quieta ausente}"; shift 2 ;;
    --skip-host-quiet) skip_host_quiet=1; shift ;;
    --max-temp-mc) max_temp_mc="${2:?temperatura ausente}"; shift 2 ;;
    --min-mem-kb) min_mem_kb="${2:?memória ausente}"; shift 2 ;;
    --boots) boot_count="${2:?quantidade ausente}"; shift 2 ;;
    --execute) execute=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done

need_cmd "${ADB:-adb}"
need_cmd python3
need_cmd sha256sum
need_cmd timeout
[[ -n "$runner" ]] || die "--runner ZZHL é obrigatório; runner AFZH3 não é compatível"
[[ -f "$runner" ]] || die "runner ausente: $runner"
runner="$(cd -- "$(dirname -- "$runner")" && pwd)/$(basename -- "$runner")"
payload="$(dirname -- "$runner")/assets/ksu-payload"
[[ -f "$payload" ]] || die "payload não encontrado ao lado do runner: $payload"
payload_sha256="$(sha256sum "$payload" | awk '{print $1}')"

if (( ! execute )); then
  cat <<EOF
PLANO (nenhuma alteração executada)
  serial:          ${serial:-<obrigatório com --execute>}
  build esperado:  ${expect_build:-<obrigatório com --execute>}
  runner:          $runner
  payload:         $payload
  payload sha256:  $payload_sha256
  ações:           reboot limpo -> preflight/quiet -> execução -> prova uid0,
                   repetido em segundo reboot independente
  retries seguros:  $pre_mutation_retries (somente com prova pré-mutação)

Para executar, informe --serial, --expect-build, --expect-sha256 e --execute.
EOF
  exit 0
fi

[[ -n "$serial" ]] || die "--serial é obrigatório com --execute"
[[ -n "$expect_build" ]] || die "--expect-build é obrigatório com --execute"
[[ "$expect_sha256" =~ ^[0-9a-fA-F]{64}$ ]] || die "--expect-sha256 inválido"
[[ "${expect_sha256,,}" == "$payload_sha256" ]] ||
  die "payload divergente: obtido=$payload_sha256 esperado=${expect_sha256,,}"
[[ "$runner_timeout" =~ ^[0-9]+$ ]] && (( runner_timeout >= 60 )) ||
  die "--timeout deve ser inteiro >= 60"
[[ "$pre_mutation_retries" =~ ^[0-9]+$ ]] && (( pre_mutation_retries <= 3 )) ||
  die "--pre-mutation-retries deve estar entre 0 e 3"
[[ "$quiet_seconds" =~ ^[0-9]+$ ]] || die "--quiet-seconds deve ser inteiro >= 0"
[[ "$max_temp_mc" =~ ^[0-9]+$ ]] && (( max_temp_mc >= 30000 )) ||
  die "--max-temp-mc inválido"
[[ "$min_mem_kb" =~ ^[0-9]+$ ]] || die "--min-mem-kb inválido"
[[ "$boot_count" == 1 || "$boot_count" == 2 ]] || die "--boots deve ser 1 ou 2"
require_device "$serial"

campaign="${evidence_dir:-$repo_dir/evidence/campaigns/$(timestamp_utc)}"
mkdir -p "$campaign"
seen_file="$campaign/seen-boot-ids.txt"
touch "$seen_file"

read_device_metrics() {
  adb_shell "$serial" 'mem=$(awk '\''/^MemAvailable:/ {print $2; exit}'\'' /proc/meminfo); load=$(awk '\''{print $1}'\'' /proc/loadavg); run=$(awk '\''{split($4,a,"/"); print a[1]}'\'' /proc/loadavg); cpu=$(awk '\''/^some / {for(i=1;i<=NF;i++) if($i ~ /^avg10=/){sub("avg10=", "", $i); print $i; exit}}'\'' /proc/pressure/cpu); mp=$(awk '\''/^some / {for(i=1;i<=NF;i++) if($i ~ /^avg10=/){sub("avg10=", "", $i); print $i; exit}}'\'' /proc/pressure/memory); io=$(awk '\''/^some / {for(i=1;i<=NF;i++) if($i ~ /^avg10=/){sub("avg10=", "", $i); print $i; exit}}'\'' /proc/pressure/io); temp=0; for z in /sys/class/thermal/thermal_zone*/temp; do v=$(cat "$z" 2>/dev/null || true); case "$v" in *[!0-9]*|'\'''\'') continue;; esac; [ "$v" -lt 200000 ] && [ "$v" -gt "$temp" ] && temp=$v; done; printf "%s %s %s %s %s %s %s\n" "$mem" "$temp" "$load" "$run" "$cpu" "$mp" "$io"' 2>/dev/null | strip_cr
}

write_device_telemetry() {
  local output="$1" label="$2" mem_kb="$3" max_temp="$4" load1="$5"
  local runnable="$6" cpu_psi="$7" memory_psi="$8" io_psi="$9"
  python3 - "$output" "$label" "$mem_kb" "$max_temp" "$load1" \
    "$runnable" "$cpu_psi" "$memory_psi" "$io_psi" <<'PY'
import json, pathlib, sys, time
pathlib.Path(sys.argv[1]).write_text(json.dumps({
    "label": sys.argv[2], "unix_time": int(time.time()),
    "mem_available_kb": int(sys.argv[3]), "max_temperature_mc": int(sys.argv[4]),
    "load_1m": float(sys.argv[5]), "runnable_tasks": int(sys.argv[6]),
    "cpu_pressure_avg10": float(sys.argv[7]),
    "memory_pressure_avg10": float(sys.argv[8]), "io_pressure_avg10": float(sys.argv[9]),
}, indent=2, sort_keys=True) + "\n")
PY
}

wait_for_quiet_device() {
  local label="$1" output="$2"
  local deadline=$((SECONDS + quiet_seconds)) stable=0 metrics
  local mem_kb max_temp load1 runnable cpu_psi memory_psi io_psi
  note "$label: aguardando 6s estáveis; tela pode permanecer ligada"
  while :; do
    require_device "$serial"
    metrics="$(read_device_metrics || true)"
    read -r mem_kb max_temp load1 runnable cpu_psi memory_psi io_psi <<<"$metrics"
    if [[ "$mem_kb" =~ ^[0-9]+$ && "$max_temp" =~ ^[0-9]+$ &&
          "$runnable" =~ ^[0-9]+$ ]] &&
       (( mem_kb >= min_mem_kb )) &&
       (( max_temp == 0 || max_temp <= max_temp_mc )) &&
       (( runnable <= 8 )) &&
       awk -v c="${cpu_psi:-999}" \
           -v m="${memory_psi:-999}" -v i="${io_psi:-999}" \
           'BEGIN {exit !(c <= 30.0 && m <= 5.0 && i <= 10.0)}'; then
      ((stable += 1))
    else
      stable=0
    fi
    if (( stable >= 3 )); then
      write_device_telemetry "$output" "$label" "$mem_kb" "$max_temp" \
        "$load1" "$runnable" "$cpu_psi" "$memory_psi" "$io_psi"
      note "$label: janela estável load=$load1 runnable=$runnable psi=$cpu_psi/$memory_psi/$io_psi"
      return 0
    fi
    (( SECONDS < deadline )) || die "$label: sem janela estável em ${quiet_seconds}s"
    sleep 2
  done
}

recover_after_mutation() {
  local number="$1" attempt="$2" previous_boot_id="$3" i completed=0 boot_id=
  note "Boot $number tentativa $attempt: mutação possível; reboot completo obrigatório"
  "$script_dir/collect-forensics.sh" --serial "$serial" \
    --output "$campaign/boot${number}-attempt${attempt}-forensics" || true
  "${ADB:-adb}" -s "$serial" reboot || return 1
  "${ADB:-adb}" -s "$serial" wait-for-device || return 1
  require_device "$serial"
  for ((i=0; i<180; i++)); do
    completed="$(adb_shell "$serial" getprop sys.boot_completed 2>/dev/null | strip_cr || true)"
    [[ "$completed" == 1 ]] && break
    sleep 1
  done
  [[ "$completed" == 1 ]] || return 1
  boot_id="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
  [[ -n "$boot_id" && "$boot_id" != "$previous_boot_id" ]] || return 1
  python3 - "$campaign/boot${number}-attempt${attempt}-recovery.json" "$boot_id" <<'PY'
import json, pathlib, sys
pathlib.Path(sys.argv[1]).write_text(json.dumps({
    "boot_completed": True, "recovery_boot_id": sys.argv[2],
}, indent=2, sort_keys=True) + "\n")
PY
}

wait_for_clean_boot() {
  local number="$1"
  local preflight="$campaign/boot${number}-preflight.json"
  note "Boot $number: reiniciando $serial"
  "${ADB:-adb}" -s "$serial" reboot || die "Boot $number: reboot ADB falhou"
  "${ADB:-adb}" -s "$serial" wait-for-device ||
    die "Boot $number: device não retornou ao ADB"
  require_device "$serial"
  local i completed
  for ((i=0; i<180; i++)); do
    completed="$(adb_shell "$serial" getprop sys.boot_completed 2>/dev/null | strip_cr || true)"
    [[ "$completed" == 1 ]] && break
    sleep 1
  done
  [[ "$completed" == 1 ]] || die "Boot $number não concluiu em 180s"
  local boot_id
  boot_id="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
  grep -Fxq "$boot_id" "$seen_file" && die "boot_id reutilizado: $boot_id"
  printf '%s\n' "$boot_id" >> "$seen_file"
  "$script_dir/preflight-device.sh" --serial "$serial" --expect-build "$expect_build" \
    --require-clean --output "$preflight"
  if (( skip_host_quiet )); then
    note "Boot $number: gate host ignorado; launcher fará validação completa"
  else
    wait_for_quiet_device "boot${number}-pre-run" "$campaign/boot${number}-telemetry.json"
  fi
  printf '%s\n' "$boot_id"
}

run_one_boot() {
  local number="$1" boot_before="$2"
  local log
  local identity="$campaign/boot${number}-identity.txt"
  local analysis="$campaign/boot${number}-analysis.json"
  local runner_rc=1 attempt=0 mutation_pending=0 explicit_pre_mutation=0 recovered=0
  while (( attempt <= pre_mutation_retries )); do
    ((attempt += 1))
    log="$campaign/boot${number}-attempt${attempt}-runner.log"
    note "Boot $number: runner tentativa $attempt/$((pre_mutation_retries + 1))"
    set +e
    "$runner" "$serial" </dev/null > >("$script_dir/stage-timer.py" | tee "$log" >&2) 2>&1 &
    local runner_pid=$! runner_deadline=$((SECONDS + runner_timeout))
    while kill -0 "$runner_pid" 2>/dev/null; do
      if grep -Eq 'stage=(kernel-mutation-pending|verifying-kernel-access|starting-temporary-root)' "$log" 2>/dev/null; then
        mutation_pending=1
      fi
      if (( SECONDS >= runner_deadline )); then
        if (( mutation_pending )); then
          recover_after_mutation "$number" "$attempt" "$boot_before" ||
            die "reboot pós-mutação falhou ou não produziu novo boot_id"
          recovered=1
          for _ in {1..30}; do
            kill -0 "$runner_pid" 2>/dev/null || break
            sleep 1
          done
          kill "$runner_pid" 2>/dev/null || true
        else
          kill "$runner_pid" 2>/dev/null || true
        fi
        break
      fi
      sleep 1
    done
    wait "$runner_pid"
    runner_rc=$?
    set -e
    "$script_dir/analyze-run-log.py" "$log" --output \
      "$campaign/boot${number}-attempt${attempt}-analysis.json"
    if grep -Eq 'stage=(kernel-mutation-pending|workqueue-mutation-pending|verifying-kernel-access|starting-temporary-root)' "$log"; then
      mutation_pending=1
    fi
    (( runner_rc == 0 )) && break
    explicit_pre_mutation=0
    if (( ! mutation_pending )) && grep -Eq 'stage=(exploit-start|kernel-location-ready)' "$log" &&
       ! grep -Eq '\[aar_aaw\] verify ok|\[pipe_rw\] ready|temporary-root-ready' "$log"; then
      explicit_pre_mutation=1
    fi
    if (( mutation_pending || ! explicit_pre_mutation )); then
      recover_after_mutation "$number" "$attempt" "$boot_before" ||
        die "reboot pós-mutação falhou ou não produziu novo boot_id"
      recovered=1
      break
    fi
    (( attempt <= pre_mutation_retries )) || break
    if (( ! skip_host_quiet )); then
      wait_for_quiet_device "boot${number}-retry${attempt}" \
        "$campaign/boot${number}-attempt${attempt}-retry-telemetry.json"
    fi
  done
  cp -- "$campaign/boot${number}-attempt${attempt}-analysis.json" "$analysis"

  local id_rc=1 id_deadline=$((SECONDS + 30))
  set +e
  while :; do
    adb_shell "$serial" "/system/bin/su -c 'id'" > "$identity" 2>&1
    id_rc=$?
    if (( id_rc == 0 )) && grep -q 'uid=0(root)' "$identity"; then
      break
    fi
    (( SECONDS >= id_deadline )) && break
    sleep 1
  done
  set -e
  local boot_after
  boot_after="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id 2>/dev/null | strip_cr || true)"
  local pass=0
  if (( runner_rc == 0 && id_rc == 0 )) && grep -q 'uid=0(root)' "$identity" &&
     [[ "$boot_before" == "$boot_after" ]]; then
    pass=1
  fi
  python3 - "$campaign/boot${number}-result.json" "$number" "$boot_before" "$boot_after" \
    "$runner_rc" "$id_rc" "$pass" "$payload_sha256" "$attempt" "$mutation_pending" <<'PY'
import json, pathlib, sys
out = pathlib.Path(sys.argv[1])
data = {
    "boot_number": int(sys.argv[2]), "boot_id_before": sys.argv[3],
    "boot_id_after": sys.argv[4], "runner_exit": int(sys.argv[5]),
    "id_exit": int(sys.argv[6]), "pass": sys.argv[7] == "1",
    "payload_sha256": sys.argv[8],
    "runner_attempts": int(sys.argv[9]),
    "mutation_pending": sys.argv[10] == "1",
}
out.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
PY
  if (( pass )); then
    ok "Boot $number PASS"
  else
    if (( mutation_pending && ! recovered )); then
      recover_after_mutation "$number" "$attempt" "$boot_before" ||
        die "reboot pós-mutação falhou ou não produziu novo boot_id"
    elif (( ! recovered )); then
      note "Boot $number FAIL; coletando forense"
      "$script_dir/collect-forensics.sh" --serial "$serial" \
        --output "$campaign/boot${number}-forensics" || true
    fi
  fi
  printf '%s\n' "$pass"
}

boot1="$(wait_for_clean_boot 1)"
pass1="$(run_one_boot 1 "$boot1")"
[[ "$pass1" == 1 ]] ||
  die "Boot 1 falhou; campanha interrompida antes de nova execução"
if (( boot_count == 1 )); then
  python3 - "$campaign/summary.json" "$serial" "$expect_build" \
    "$payload_sha256" "$boot1" <<'PY'
import json, pathlib, sys
pathlib.Path(sys.argv[1]).write_text(json.dumps({
    "serial": sys.argv[2], "build": sys.argv[3], "payload_sha256": sys.argv[4],
    "boots": [{"number": 1, "boot_id": sys.argv[5], "pass": True}],
    "pass_1_of_1": True,
}, indent=2, sort_keys=True) + "\n")
PY
  ok "campanha PASS 1/1: $campaign"
  exit 0
fi
boot2="$(wait_for_clean_boot 2)"
pass2="$(run_one_boot 2 "$boot2")"

python3 - "$campaign/summary.json" "$serial" "$expect_build" "$payload_sha256" \
  "$boot1" "$pass1" "$boot2" "$pass2" <<'PY'
import json, pathlib, sys
out = pathlib.Path(sys.argv[1])
boots = [
    {"number": 1, "boot_id": sys.argv[5], "pass": sys.argv[6] == "1"},
    {"number": 2, "boot_id": sys.argv[7], "pass": sys.argv[8] == "1"},
]
data = {"serial": sys.argv[2], "build": sys.argv[3], "payload_sha256": sys.argv[4],
        "boots": boots, "pass_2_of_2": all(item["pass"] for item in boots)}
out.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
PY

if [[ "$pass1" == 1 && "$pass2" == 1 ]]; then
  ok "campanha PASS 2/2: $campaign"
  exit 0
fi
die "campanha não passou 2/2: $campaign"
