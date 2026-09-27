#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "$script_dir/common.sh"
project_dir="$(cd -- "$repo_dir/../../.." && pwd)"
app_dir="$project_dir/app"
apk="$app_dir/build/outputs/apk/debug/app-debug.apk"

usage() {
  cat <<'EOF'
Uso: build-install-app.sh [opções]

Compila o engine AFZH3, sincroniza payload/factory/launcher, testa e gera o APK.
A instalação só ocorre com --install e serial explícito.

Opções:
  --install              Instala por adb install -r e verifica o APK puxado.
  --serial SERIAL        Serial obrigatório com --install.
  --skip-unit-tests      Gera o APK sem executar testDebugUnitTest.
  --output ARQUIVO       Relatório JSON final; padrão build/afzh3-app-bundle.json.
  -h, --help             Mostra esta ajuda.
EOF
}

serial=
install_apk=0
run_unit_tests=1
output="$repo_dir/build/afzh3-app-bundle.json"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --install) install_apk=1; shift ;;
    --serial) serial="${2:?serial ausente}"; shift 2 ;;
    --skip-unit-tests) run_unit_tests=0; shift ;;
    --output) output="${2:?arquivo ausente}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "opção desconhecida: $1" ;;
  esac
done

need_cmd make
need_cmd python3
need_cmd sha256sum
[[ -x "$app_dir/gradlew" ]] || die "Gradle wrapper ausente: $app_dir/gradlew"
ndk_root="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}/ndk"
ndk_dir="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-$ndk_root/28.2.13676358}}"
launcher_cc="$ndk_dir/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
if [[ ! -x "$launcher_cc" ]]; then
  for candidate in "$ndk_root/28.2.13676358" "$ndk_root"/*; do
    candidate_cc="$candidate/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
    if [[ -x "$candidate_cc" ]]; then
      ndk_dir="$candidate"
      launcher_cc="$candidate_cc"
      break
    fi
  done
fi
[[ -x "$launcher_cc" ]] || die "compilador Android API 35 ausente; defina ANDROID_NDK_HOME"
if (( install_apk )); then
  [[ -n "$serial" ]] || die "--serial é obrigatório com --install"
  need_cmd "${ADB:-adb}"
  require_device "$serial"
fi

note "compilando engine, factory e checks focados"
make -C "$repo_dir" so all tests test-aar-read-plan test-fops-layout
python3 "$script_dir/check-critical-reclaim.py" \
  --artifact "$repo_dir/build/payload.so"

note "compilando stability launcher"
mkdir -p "$project_dir/stability-launcher/build"
"$launcher_cc" -O2 -Wall -Wextra -Werror -fPIE \
  -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
  "$project_dir/stability-launcher/stability-launcher.c" \
  -pie -Wl,-z,relro,-z,now \
  -o "$project_dir/stability-launcher/build/stability-launcher"

note "sincronizando payload, factory e launcher no app"
python3 "$script_dir/afzh3-app-bundle.py" sync

gradle_tasks=(assembleDebug)
if (( run_unit_tests )); then
  gradle_tasks=(testDebugUnitTest assembleDebug)
fi
note "compilando APK: ${gradle_tasks[*]}"
(cd -- "$app_dir" && ./gradlew "${gradle_tasks[@]}")

installed_apk=
cleanup() {
  if [[ -n "$installed_apk" && -f "$installed_apk" ]]; then
    rm -f -- "$installed_apk"
  fi
}
trap cleanup EXIT

if (( install_apk )); then
  note "instalando APK no device $serial"
  "${ADB:-adb}" -s "$serial" install -r "$apk"
  package_path="$(adb_shell "$serial" pm path io.github.rootmygalaxy.s23ultra | strip_cr)"
  package_path="${package_path#package:}"
  [[ "$package_path" == /*.apk ]] || die "pm path inválido: $package_path"
  installed_apk="$(mktemp /tmp/afzh3-installed-apk.XXXXXX.apk)"
  "${ADB:-adb}" -s "$serial" pull "$package_path" "$installed_apk"
  python3 "$script_dir/afzh3-app-bundle.py" verify \
    --apk "$apk" --installed-apk "$installed_apk" --output "$output"
else
  python3 "$script_dir/afzh3-app-bundle.py" verify \
    --apk "$apk" --output "$output"
fi

git -C "$project_dir" diff --check
ok "pipeline concluído: $output"
sha256sum "$apk" "$repo_dir/build/payload.so" "$repo_dir/build/mm-exec-factory" \
  "$project_dir/stability-launcher/build/stability-launcher"
