#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
  cat <<'EOF'
Uso: collect-forensics.sh [opções]

Coleta evidência somente-leitura. Usa su apenas quando já funciona.

Opções:
  --serial SERIAL       Serial ADB.
  --output DIR          Diretório de saída; padrão evidence/forensics/<UTC>-<boot>.
  --deep                Inclui kallsyms, slabinfo e tracefs; arquivos maiores.
  --bugreport           Tenta obter dumpstate completo (útil após reboot por panic).
  --previous-lastkmsg P  Ignora dump Samsung se ainda for o arquivo P.
  -h, --help            Mostra esta ajuda.
EOF
}

serial=
output=
deep=0
bugreport=0
previous_lastkmsg=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --output) output="${2:?diretório ausente}"; shift 2 ;;
    --deep) deep=1; shift ;;
    --bugreport) bugreport=1; shift ;;
    --previous-lastkmsg) previous_lastkmsg="${2:?caminho ausente}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done

need_cmd "${ADB:-adb}"
need_cmd python3
need_cmd unzip
if (( bugreport )); then need_cmd timeout; fi
serial="$(resolve_serial "$serial")"
require_device "$serial"
boot_id="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
short_boot="${boot_id%%-*}"
output="${output:-$repo_dir/evidence/forensics/$(timestamp_utc)-$short_boot}"
mkdir -p "$output"

note "coletando evidência de $serial em $output"
"$script_dir/preflight-device.sh" --serial "$serial" --output "$output/preflight.json"
"${ADB:-adb}" -s "$serial" shell getprop > "$output/getprop.txt" 2>&1 || true
"${ADB:-adb}" -s "$serial" shell cat /proc/version > "$output/proc-version.txt" 2>&1 || true
"${ADB:-adb}" -s "$serial" shell cat /proc/uptime > "$output/uptime.txt" 2>&1 || true
"${ADB:-adb}" -s "$serial" logcat -d -b all > "$output/logcat-all.txt" 2>&1 || true
"${ADB:-adb}" -s "$serial" logcat -L -d -b all > "$output/logcat-previous.txt" 2>&1 || true
if adb_shell "$serial" test -r /data/local/tmp/rmg-trace.txt 2>/dev/null; then
  "${ADB:-adb}" -s "$serial" exec-out cat /data/local/tmp/rmg-trace.txt \
    > "$output/rmg-trace.txt" 2>&1 || true
fi

# DropBox exposes the previous boot's last_kmsg to the shell user even when
# /proc/last_kmsg and pstore are root-only. Samsung may truncate this entry;
# retain that marker rather than treating an absent panic line as proof of a
# clean shutdown.
"${ADB:-adb}" -s "$serial" shell dumpsys dropbox 2>/dev/null |
  grep 'SYSTEM_LAST_KMSG_' > "$output/dropbox-last-kmsg-index.txt" || true
if [[ -s "$output/dropbox-last-kmsg-index.txt" ]]; then
  read -r entry_date entry_time entry_tag _ < <(
    tail -n 1 "$output/dropbox-last-kmsg-index.txt")
  if [[ -n "$entry_date" && -n "$entry_time" && -n "$entry_tag" ]]; then
    "${ADB:-adb}" -s "$serial" shell dumpsys dropbox --print \
      "$entry_date" "$entry_time" "$entry_tag" \
      > "$output/dropbox-last-kmsg.txt" 2>&1 || true
  fi
fi

# Samsung exposes a full previous-boot last_kmsg archive to ADB shell. Its
# .log.gz suffix is misleading: the file is a ZIP containing the panic stack.
# Compare the remote path with the pre-run baseline so an old panic cannot be
# reported as evidence from the current attempt.
latest_lastkmsg="$(adb_shell "$serial" \
  'ls -t /data/log/dumpstate_lastkmsg_*.log.gz 2>/dev/null | head -n 1' \
  2>/dev/null | strip_cr || true)"
lastkmsg_status=missing
if [[ -n "$latest_lastkmsg" && "$latest_lastkmsg" == "$previous_lastkmsg" ]]; then
  lastkmsg_status=unchanged
elif [[ "$latest_lastkmsg" == /data/log/dumpstate_lastkmsg_*.log.gz ]]; then
  printf '%s\n' "$latest_lastkmsg" > "$output/samsung-lastkmsg-source.txt"
  if "${ADB:-adb}" -s "$serial" pull "$latest_lastkmsg" \
      "$output/samsung-lastkmsg.log.gz" > "$output/samsung-lastkmsg-pull.txt" 2>&1; then
    lastkmsg_status=downloaded
    if ! unzip -p "$output/samsung-lastkmsg.log.gz" dumpstate_lastkmsg.lst \
        > "$output/samsung-lastkmsg.txt" 2> "$output/samsung-lastkmsg-extract.txt"; then
      lastkmsg_status=extract_failed
    else
      python3 - "$output/samsung-lastkmsg.txt" \
        "$output/panic-excerpt.txt" <<'PY'
from pathlib import Path
import sys

lines = Path(sys.argv[1]).read_text(errors="replace").splitlines()
markers = [i for i, line in enumerate(lines)
           if "Unable to handle kernel paging request" in line]
if not markers:
    markers = [i for i, line in enumerate(lines)
               if "Kernel panic - not syncing" in line]
if markers:
    start = max(0, markers[-1] - 2)
    excerpt = lines[start:start + 100]
    Path(sys.argv[2]).write_text("\n".join(
        line[:1200] + (" [line truncated]" if len(line) > 1200 else "")
        for line in excerpt
    ) + "\n")
else:
    Path(sys.argv[2]).write_text("No kernel panic marker found in archive.\n")
PY
    fi
  else
    lastkmsg_status=pull_failed
  fi
fi
printf '%s\n' "$lastkmsg_status" > "$output/samsung-lastkmsg-status.txt"
note "Samsung last_kmsg: $lastkmsg_status"

root_works=0
if adb_shell "$serial" "/system/bin/su -c 'id'" 2>/dev/null | grep -q 'uid=0(root)'; then
  root_works=1
  note "root disponível; coletando canais privilegiados"
  "${ADB:-adb}" -s "$serial" exec-out /system/bin/su -c 'cat /proc/last_kmsg' \
    > "$output/last_kmsg.txt" 2>&1 || true
  "${ADB:-adb}" -s "$serial" exec-out /system/bin/su -c 'dmesg' \
    > "$output/dmesg.txt" 2>&1 || true
  "${ADB:-adb}" -s "$serial" exec-out /system/bin/su -c \
    'for f in /sys/fs/pstore/*; do [ -f "$f" ] || continue; echo "===== $f ====="; cat "$f"; done' \
    > "$output/pstore.txt" 2>&1 || true
  if (( deep )); then
    "${ADB:-adb}" -s "$serial" exec-out /system/bin/su -c 'cat /proc/kallsyms' \
      > "$output/kallsyms.txt" 2>&1 || true
    "${ADB:-adb}" -s "$serial" exec-out /system/bin/su -c 'cat /proc/slabinfo' \
      > "$output/slabinfo.txt" 2>&1 || true
    "${ADB:-adb}" -s "$serial" exec-out /system/bin/su -c \
      'cat /sys/kernel/tracing/trace 2>/dev/null || cat /sys/kernel/debug/tracing/trace 2>/dev/null' \
      > "$output/trace.txt" 2>&1 || true
  fi
else
  note "root indisponível; coleta limitada a canais públicos"
fi

if (( bugreport )); then
  note "tentando bugreport para recuperar trace completo do boot anterior"
  timeout 360 "${ADB:-adb}" -s "$serial" bugreport "$output/bugreport.zip" \
    > "$output/bugreport-command.txt" 2>&1 || true
fi

python3 - "$output" "$serial" "$boot_id" "$root_works" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
files = {}
for path in sorted(root.iterdir()):
    if path.is_file() and path.name != "manifest.json":
        digest = hashlib.sha256()
        size = 0
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
                size += len(chunk)
        files[path.name] = {"bytes": size, "sha256": digest.hexdigest()}
manifest = {
    "serial": sys.argv[2], "boot_id": sys.argv[3],
    "root_available": sys.argv[4] == "1", "files": files,
}
(root / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
PY

ok "forense concluída: $output"
