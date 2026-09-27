#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
  cat <<'EOF'
Uso: collect-app-runs.sh [opções]

Coleta os históricos mais recentes do app, estado do boot, logcat e evidência
do último panic. Não executa payload, não reinicia e não tenta obter root.

Opções:
  --serial SERIAL       Serial ADB.
  --count N             Quantidade de históricos; padrão 3.
  --package PACKAGE     Package Android; padrão io.github.rootmygalaxy.s23ultra.
  --output DIR          Diretório de saída.
  --no-logcat           Não coleta logcat atual.
  --forensics           Executa também collect-forensics.sh.
  --deep                Forense completa com slabinfo/kallsyms/tracefs.
  -h, --help            Mostra esta ajuda.
EOF
}

serial=
count=3
package=io.github.rootmygalaxy.s23ultra
output=
logcat_enabled=1
forensics=0
deep=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --count) count="${2:?quantidade ausente}"; shift 2 ;;
    --package) package="${2:?package ausente}"; shift 2 ;;
    --output) output="${2:?diretório ausente}"; shift 2 ;;
    --no-logcat) logcat_enabled=0; shift ;;
    --forensics) forensics=1; shift ;;
    --deep) deep=1; forensics=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done

[[ "$count" =~ ^[1-9][0-9]*$ ]] || die "--count deve ser inteiro positivo"
[[ "$package" =~ ^[A-Za-z0-9._]+$ ]] || die "package inválido: $package"
need_cmd "${ADB:-adb}"
need_cmd python3
serial="$(resolve_serial "$serial")"
require_device "$serial"
boot_id="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
short_boot="${boot_id%%-*}"
output="${output:-$repo_dir/evidence/app-runs/$(timestamp_utc)-$short_boot}"
mkdir -p "$output/history"

root_works=0
if adb_shell "$serial" "su -c id" 2>/dev/null | grep -q 'uid=0(root)'; then
  root_works=1
fi

access=run-as
if ! adb_shell "$serial" "run-as $package test -d files/install-history" >/dev/null 2>&1; then
  (( root_works )) || die "run-as indisponível e root não funciona"
  access=root
fi
note "coletando históricos via $access"

if [[ "$access" == run-as ]]; then
  list_command="run-as $package sh -c 'cd files/install-history && ls -1t *.json *.json.corrupt 2>/dev/null'"
else
  list_command="su -c 'cd /data/user/0/$package/files/install-history && ls -1t *.json *.json.corrupt 2>/dev/null'"
fi
mapfile -t histories < <(adb_shell "$serial" "$list_command" | strip_cr | head -n "$count")
[[ ${#histories[@]} -gt 0 ]] || die "nenhum histórico encontrado"

printf 'ordinal\tname\tbytes\n' > "$output/history-index.tsv"
ordinal=0
for name in "${histories[@]}"; do
  [[ "$name" =~ ^[A-Za-z0-9._-]+$ ]] || die "nome remoto inválido: $name"
  ordinal=$((ordinal + 1))
  destination="$output/history/$name"
  if [[ "$access" == run-as ]]; then
    "${ADB:-adb}" -s "$serial" exec-out run-as "$package" cat \
      "files/install-history/$name" > "$destination"
  else
    "${ADB:-adb}" -s "$serial" exec-out su -c \
      "cat /data/user/0/$package/files/install-history/$name" > "$destination"
  fi
  printf '%d\t%s\t%d\n' "$ordinal" "$name" "$(stat -c %s "$destination")" \
    >> "$output/history-index.tsv"
done

{
  printf 'serial=%s\n' "$serial"
  printf 'boot_id=%s\n' "$boot_id"
  printf 'root_available=%s\n' "$root_works"
  adb_shell "$serial" getprop sys.boot_completed | strip_cr | sed 's/^/boot_completed=/'
  adb_shell "$serial" getprop ro.build.version.incremental | strip_cr | sed 's/^/build=/'
  adb_shell "$serial" getprop ro.boot.bootreason | strip_cr | sed 's/^/bootreason=/'
  adb_shell "$serial" cat /proc/uptime | strip_cr | sed 's/^/uptime=/'
} > "$output/device-state.txt"

if (( logcat_enabled )); then
  note "coletando logcat"
  "${ADB:-adb}" -s "$serial" logcat -d -b all > "$output/logcat-current.txt" 2>&1 || true
fi

"${ADB:-adb}" -s "$serial" shell dumpsys dropbox 2>/dev/null |
  grep 'SYSTEM_LAST_KMSG_' > "$output/dropbox-last-kmsg-index.txt" || true
if [[ -s "$output/dropbox-last-kmsg-index.txt" ]]; then
  read -r entry_date entry_time entry_tag _ < <(tail -n 1 "$output/dropbox-last-kmsg-index.txt")
  if [[ -n "${entry_date:-}" && -n "${entry_time:-}" && -n "${entry_tag:-}" ]]; then
    "${ADB:-adb}" -s "$serial" shell dumpsys dropbox --print \
      "$entry_date" "$entry_time" "$entry_tag" \
      > "$output/dropbox-last-kmsg.txt" 2>&1 || true
  fi
fi
if (( root_works )); then
  "${ADB:-adb}" -s "$serial" exec-out su -c 'cat /proc/last_kmsg' \
    > "$output/proc-last-kmsg.txt" 2>&1 || true
  "${ADB:-adb}" -s "$serial" exec-out su -c \
    'grep -E "^(mm_struct|skbuff_head_cache|kmalloc-4k) " /proc/slabinfo' \
    > "$output/slab-snapshot.txt" 2>&1 || true
fi

if (( forensics )); then
  forensic_args=(--serial "$serial" --output "$output/forensics")
  if (( deep )); then forensic_args+=(--deep); fi
  "$script_dir/collect-forensics.sh" "${forensic_args[@]}"
fi

python3 "$script_dir/analyze-app-runs.py" "$output" --extract-logs \
  --json "$output/analysis.json" --markdown "$output/analysis.md"
python3 - "$output" "$serial" "$boot_id" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
files = {}
for path in sorted(root.rglob("*")):
    if path.is_file() and path.name != "manifest.json":
        data = path.read_bytes()
        files[str(path.relative_to(root))] = {
            "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        }
(root / "manifest.json").write_text(json.dumps({
    "serial": sys.argv[2], "boot_id": sys.argv[3], "files": files,
}, indent=2, sort_keys=True) + "\n")
PY
ok "coleta concluída: $output"
printf '%s\n' "$output"
