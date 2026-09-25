#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
  cat <<'EOF'
Uso: extract-btf-layouts.sh (--btf ARQUIVO | --serial SERIAL) [opções]

Extrai layouts de structs via pahole. Com --serial, copia BTF do device usando
root já existente; não altera o kernel.

Opções:
  --btf ARQUIVO       BTF/vmlinux local.
  --serial SERIAL     Device com /sys/kernel/btf/vmlinux legível via su.
  --output DIR        Diretório de saída; padrão /tmp/btf-layouts-<UTC>.
  --struct NOME       Struct a extrair; repetível.
  -h, --help          Mostra esta ajuda.
EOF
}

btf=
serial=
output=
structs=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --btf) btf="${2:?arquivo ausente}"; shift 2 ;;
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --output) output="${2:?diretório ausente}"; shift 2 ;;
    --struct) structs+=("${2:?nome ausente}"); shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done
[[ -n "$btf" && -z "$serial" || -z "$btf" && -n "$serial" ]] ||
  die "use exatamente um entre --btf e --serial"
need_cmd pahole
output="${output:-/tmp/btf-layouts-$(timestamp_utc)}"
mkdir -p "$output"
if [[ -n "$serial" ]]; then
  need_cmd "${ADB:-adb}"
  require_device "$serial"
  btf="$output/vmlinux.btf"
  note "copiando BTF somente-leitura de $serial"
  "${ADB:-adb}" -s "$serial" exec-out /system/bin/su -c \
    'cat /sys/kernel/btf/vmlinux' > "$btf"
fi
[[ -s "$btf" ]] || die "BTF ausente ou vazio: $btf"
if [[ ${#structs[@]} -eq 0 ]]; then
  structs=(pipe_buffer pool_workqueue worker_pool workqueue_struct work_struct subprocess_info completion)
fi

found=0
missing=0
for name in "${structs[@]}"; do
  target="$output/$(safe_name "$name").txt"
  if pahole -F btf -C "$name" "$btf" > "$target" 2>&1; then
    found=$((found + 1))
  else
    missing=$((missing + 1))
    printf 'missing: %s\n' "$name" >> "$output/missing.txt"
    rm -f "$target"
  fi
done
printf 'btf=%s\nfound=%d\nmissing=%d\n' "$btf" "$found" "$missing" > "$output/summary.txt"
(( found > 0 )) || die "nenhum layout extraído"
ok "layouts extraídos: $output (found=$found missing=$missing)"
