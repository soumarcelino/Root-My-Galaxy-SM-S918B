#!/usr/bin/env bash
# Clean-boot soak wrapper. Defaults to three boots and never contacts Codex.
set -uo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/.." && pwd)"
serial="${SERIAL:-RXCX602E20X}"
runner="${RUNNER:-/home/matias/Projects/ksu-payload-functional/simple-root}"
total="${TOTAL_BOOTS:-3}"
batch_size="${BATCH_SIZE:-3}"
campaign="${CAMPAIGN_DIR:-$repo_dir/evidence/reliability/$(date -u +%Y%m%dT%H%M%SZ)-soak-${total}}"
mkdir -p "$campaign"
log_file="$campaign/campaign.log"
live_log_script="/home/matias/.codex/skills/kitty-live-logs/scripts/live_logs.py"
exec > >(tee -a "$log_file") 2>&1

open_live_log_window() {
  if ! command -v kitty-codex >/dev/null 2>&1; then
    printf '[soak] kitty-codex indisponível; acompanhe %s\n' "$log_file"
    return
  fi
  if [[ ! -r "$live_log_script" ]]; then
    printf '[soak] visualizador Kitty ausente; acompanhe %s\n' "$log_file"
    return
  fi
  kitty-codex launch --type=os-window --cwd="$repo_dir" \
    --title "Logs · ${campaign##*/}" -- \
    python3 "$live_log_script" -- tail -n 30 -F "$log_file" ||
    printf '[soak] não abriu janela Kitty; acompanhe %s\n' "$log_file"
}

open_live_log_window

wait_device() {
  while ! adb -s "$serial" get-state 2>/dev/null | grep -qx device; do
    printf '[soak] device offline; aguardando %s\n' "$serial"
    sleep 15
  done
}

profile_for_batch() {
  local batch="$1" summary="$campaign/summary.json"
  if (( batch == 1 )); then printf 'standard\n'; return; fi
  if (( batch == 2 )); then printf 'conservative\n'; return; fi
  if (( batch == 3 )); then printf 'balanced\n'; return; fi
  if [[ -s "$summary" ]]; then
    python3 - "$summary" <<'PY'
import json, sys
print(json.load(open(sys.argv[1])).get("recommended_profile", "standard"))
PY
  else
    printf 'standard\n'
  fi
}

profile_values() {
  case "$1" in
    conservative) printf '40000 4194304\n' ;;
    balanced) printf '41000 3145728\n' ;;
    *) printf '42000 2097152\n' ;;
  esac
}

[[ -x "$runner" ]] || { printf '[soak] runner ausente: %s\n' "$runner"; exit 1; }
payload="$(dirname -- "$runner")/assets/ksu-payload"
payload_sha="$(sha256sum "$payload" | awk '{print $1}')"
printf '%s\n' "$payload_sha" > "$campaign/payload.sha256"
global_seen="$campaign/seen-test-boot-ids.txt"
touch "$global_seen"

completed="$(find "$campaign" -mindepth 2 -maxdepth 2 -path '*/record.json' -type f | wc -l)"
printf '[soak] retomando em %d/%d boots concluídos\n' "$completed" "$total"
while (( completed < total )); do
  wait_device
  if (( completed > 0 )); then
    previous_dir="$campaign/boot-$(printf '%03d' "$completed")"
    if [[ -f "$previous_dir/record.json" ]] &&
       python3 - "$previous_dir/record.json" <<'PY'
import json, sys
sys.exit(0 if json.load(open(sys.argv[1]))["pass"] is False else 1)
PY
    then
      if [[ ! -e "$previous_dir/postfailure-forensics/manifest.json" ]]; then
        "$script_dir/collect-forensics.sh" --serial "$serial" \
          --bugreport --output "$previous_dir/postfailure-forensics" || true
      fi
    fi
  fi
  build="$(adb -s "$serial" shell getprop ro.build.version.incremental 2>/dev/null | tr -d '\r')"
  [[ -n "$build" ]] || { sleep 15; continue; }
  number=$((completed + 1))
  batch=$(((number - 1) / batch_size + 1))
  profile="$(profile_for_batch "$batch")"
  read -r max_temp min_mem <<<"$(profile_values "$profile")"
  boot_dir="$campaign/boot-$(printf '%03d' "$number")"
  mkdir -p "$boot_dir"
  started="$(date +%s)"
  printf '[soak] boot=%d/%d batch=%d profile=%s build=%s\n' \
    "$number" "$total" "$batch" "$profile" "$build"

  set +e
  "$script_dir/validate-two-boots.sh" --execute --boots 1 \
    --serial "$serial" --expect-build "$build" --expect-sha256 "$payload_sha" \
    --runner "$runner" --evidence-dir "$boot_dir/evidence" --timeout 900 \
    --pre-mutation-retries 1 --quiet-seconds 300 --skip-host-quiet \
    --max-temp-mc "$max_temp" --min-mem-kb "$min_mem"
  rc=$?
  set -e
  ended="$(date +%s)"
  tested_boot_id="$(sed -n '1p' "$boot_dir/evidence/seen-boot-ids.txt" 2>/dev/null || true)"
  if [[ -z "$tested_boot_id" ]]; then
    printf '[soak] boot=%d não contado: boot_id de teste ausente\n' "$number"
    sleep 15
    continue
  fi
  if grep -Fxq "$tested_boot_id" "$global_seen"; then
    printf '[soak] boot=%d não contado: boot_id repetido %s\n' "$number" "$tested_boot_id"
    sleep 15
    continue
  fi
  printf '%s\n' "$tested_boot_id" >> "$global_seen"
  pass=false
  (( rc == 0 )) && pass=true
  python3 - "$boot_dir/record.json" "$number" "$batch" "$profile" "$pass" \
    "$rc" "$((ended - started))" "$tested_boot_id" "$payload_sha" <<'PY'
import json, pathlib, sys
pathlib.Path(sys.argv[1]).write_text(json.dumps({
    "number": int(sys.argv[2]), "batch": int(sys.argv[3]), "profile": sys.argv[4],
    "pass": sys.argv[5] == "true", "exit_code": int(sys.argv[6]),
    "duration_seconds": int(sys.argv[7]), "boot_id": sys.argv[8],
    "payload_sha256": sys.argv[9],
}, indent=2, sort_keys=True) + "\n")
PY
  completed=$number

  if (( completed % batch_size == 0 )); then
    "$script_dir/summarize-soak.py" "$campaign" --output "$campaign/summary.json"
    printf '[soak] lote %d concluído; resumo=%s\n' "$batch" "$campaign/summary.json"
  fi
done

# The final failed boot has no next iteration to collect its recovery boot.
if (( completed > 0 )); then
  last_dir="$campaign/boot-$(printf '%03d' "$completed")"
  if [[ -f "$last_dir/record.json" ]] &&
     python3 - "$last_dir/record.json" <<'PY'
import json, sys
sys.exit(0 if json.load(open(sys.argv[1]))["pass"] is False else 1)
PY
  then
    wait_device
    if [[ ! -e "$last_dir/postfailure-forensics/manifest.json" ]]; then
      "$script_dir/collect-forensics.sh" --serial "$serial" \
        --bugreport --output "$last_dir/postfailure-forensics" || true
    fi
  fi
fi

"$script_dir/summarize-soak.py" "$campaign" --output "$campaign/summary.json"
printf '[soak] campanha concluída: %s\n' "$campaign"
