#!/usr/bin/env bash
# Live monitor for the closed ZZHL payload: streams the exploit's own stage log,
# flags kernel panics, and detects/reports the reboot that follows a crash.
# Meant to be run in its own terminal window (see run-zzhl-closed-kitty.sh).
set -uo pipefail

SERIAL="${ANDROID_SERIAL:-RXCX602E20X}"
ADB=(adb -s "$SERIAL")
REMOTE=/data/local/tmp
HELPER="$REMOTE/closed-helper-zzhl"
PAYLOAD="$REMOTE/closed-payload.so"
ATTEMPTS="${EXPLOIT_ATTEMPTS:-1}"

C_RESET=$'\e[0m'; C_DIM=$'\e[2m'; C_OK=$'\e[32m'; C_WARN=$'\e[33m'; C_ERR=$'\e[31m'; C_HL=$'\e[36m'

say() { printf '%s[monitor]%s %s\n' "$C_HL" "$C_RESET" "$*"; }

wait_device() {
    printf '%s[monitor]%s waiting for device %s ...\n' "$C_HL" "$C_RESET" "$SERIAL"
    until "${ADB[@]}" shell true >/dev/null 2>&1; do sleep 2; done
    local up
    up=$("${ADB[@]}" shell 'cat /proc/uptime' 2>/dev/null | cut -d. -f1 | tr -d '\r')
    printf '%s[monitor]%s device up (uptime %ss)\n' "$C_OK" "$C_RESET" "$up"
}

# Pull and summarise the newest Samsung dumpstate last_kmsg (works without root)
report_panic() {
    local newest base tmp
    newest=$("${ADB[@]}" shell 'ls -t /data/log/dumpstate_lastkmsg_*.log.gz 2>/dev/null' \
             | head -1 | tr -d '\r')
    [[ -z "$newest" ]] && { say "no dumpstate kmsg found"; return; }
    base=$(basename "$newest" .log.gz)
    tmp=$(mktemp -d)
    "${ADB[@]}" pull "$newest" "$tmp/" >/dev/null 2>&1 || { rm -rf "$tmp"; return; }
    # Samsung ships these as ZIPs despite the .gz name
    unzip -o -q "$tmp/$base.log.gz" -d "$tmp/x" 2>/dev/null
    printf '%s=== panic report: %s ===%s\n' "$C_ERR" "$base" "$C_RESET"
    grep -m1 -A2 "Upload Cause" "$tmp/x/dumpstate_auto_comment.lst" 2>/dev/null
    grep -E "Unable to handle kernel paging request|Kernel panic|Insufficient stack" \
         "$tmp/x/dumpstate_lastkmsg.lst" 2>/dev/null | head -5
    grep -E "^\[.*\] *pc : |^\[.*\] *lr : " "$tmp/x/dumpstate_lastkmsg.lst" 2>/dev/null \
         | grep -v "modprobe\|bpfloader\|cpuidle" | head -4
    sed -n '/Call trace:/,/end trace/p' "$tmp/x/dumpstate_lastkmsg.lst" 2>/dev/null | head -14
    rm -rf "$tmp"
}

colourise() {
    while IFS= read -r line; do
        case "$line" in
            *temporary-root-ready*|*uid=0*) printf '%s%s%s\n' "$C_OK"   "$line" "$C_RESET" ;;
            *stage=*)                       printf '%s%s%s\n' "$C_HL"   "$line" "$C_RESET" ;;
            *failed*|*operation\ failed*)   printf '%s%s%s\n' "$C_ERR"  "$line" "$C_RESET" ;;
            *attempt=*)                     printf '%s%s%s\n' "$C_WARN" "$line" "$C_RESET" ;;
            *)                              printf '%s%s%s\n' "$C_DIM"  "$line" "$C_RESET" ;;
        esac
    done
}

wait_device
say "payload: $("${ADB[@]}" shell "sha256sum $PAYLOAD" 2>/dev/null | cut -c1-16 | tr -d '\r')..."
say "attempts=$ATTEMPTS  (set EXPLOIT_ATTEMPTS to change)"
say "starting run -- live output below"
echo

"${ADB[@]}" shell "rm -f $REMOTE/closed-live.log" >/dev/null 2>&1
"${ADB[@]}" shell "EXPLOIT_ATTEMPTS=$ATTEMPTS P0_ATTEMPT_TIMEOUT_SEC=45 \
  EXPLOIT_ATTEMPT_TIMEOUT_SEC=120 $HELPER --run-payload $PAYLOAD $HELPER \
  $REMOTE/closed-live.log" 2>&1 | colourise

echo
if "${ADB[@]}" shell true >/dev/null 2>&1; then
    say "run ended, device still alive (no reboot)"
    "${ADB[@]}" shell "$HELPER -c id" 2>/dev/null | colourise
else
    printf '%s[monitor]%s device dropped -- kernel crashed, waiting for reboot\n' "$C_ERR" "$C_RESET"
    wait_device
    report_panic
fi
echo
say "done. press enter to close"
read -r _
