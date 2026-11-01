#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required}"

salts_rid="${1:?Salts target RID is required}"
host_rid="${2:?re2c/Salts host RID is required}"
with_turbowasm="${3:-0}"
case "$with_turbowasm" in
  0|1) ;;
  *) echo "usage: restore-native-sdk.sh <salts-rid> <host-rid> [with-turbowasm:0|1] [ci|local]" >&2; exit 1 ;;
esac
mode="${4:-ci}"
repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
case "$mode" in
  local)
    restore_root="$repository_root/build/native-sdk"
    packages="${QIGAO_NUGET_PACKAGES:-$repository_root/stage/nuget}"
    ;;
  ci)
    : "${RUNNER_TEMP:?RUNNER_TEMP is required}"
    : "${GITHUB_ENV:?GITHUB_ENV is required}"
    : "${GITHUB_PATH:?GITHUB_PATH is required}"
    restore_root="$RUNNER_TEMP"
    packages="${QIGAO_NUGET_PACKAGES:-$RUNNER_TEMP/qigao-native-packages}"
    ;;
  *) echo "restore mode must be ci or local" >&2; exit 1 ;;
esac
mkdir -p "$restore_root" "$packages"
result="$restore_root/saltsutils-native-sdk.json"
set -- native-sdk --packages "$packages" --salts-rid "$salts_rid" --host-rid "$host_rid" --result "$result"
if [ "$with_turbowasm" = 1 ]; then set -- "$@" --with-turbowasm; fi
python3 "$repository_root/cmake/ci/github_packages.py" "$@"

if [ "$mode" = local ]; then
  read_json() {
    python3 - "$result" "$1" <<'PY'
import json, pathlib, sys
print(json.loads(pathlib.Path(sys.argv[1]).read_text())[sys.argv[2]])
PY
  }
  saltscore_root="$(read_json SALTSCORE_ROOT)"
  saltscore_host_root="$(read_json SALTSCORE_HOST_ROOT)"
  cmeta_root="$(read_json CMETA_ROOT)"
  cmeta_host_root="$(read_json CMETA_HOST_ROOT)"
  re2c_root="$(read_json RE2C_ROOT)"
  saltscore_version="$(read_json SALTSCORE_VERSION)"
  cmeta_version="$(read_json CMETA_VERSION)"
  local_link() {
    local link="$repository_root/stage/dependencies/$1" target="$2"
    mkdir -p "$(dirname "$link")"
    if [ -e "$link" ] && [ ! -L "$link" ]; then
      echo "refusing to replace non-symlink SDK path: $link" >&2
      exit 1
    fi
    ln -sfn "$target" "$link"
    printf '%s\n' "$link"
  }
  saltscore_root="$(local_link "saltscore/$salts_rid" "$saltscore_root")"
  saltscore_host_root="$(local_link "saltscore/$host_rid" "$saltscore_host_root")"
  cmeta_root="$(local_link "cmeta/$salts_rid" "$cmeta_root")"
  cmeta_host_root="$(local_link "cmeta/$host_rid" "$cmeta_host_root")"
  re2c_root="$(local_link "re2c/$host_rid" "$re2c_root")"
  {
    printf 'export SALTSCORE_ROOT=%q\n' "$saltscore_root"
    printf 'export SALTSCORE_HOST_ROOT=%q\n' "$saltscore_host_root"
    printf 'export CMETA_ROOT=%q\n' "$cmeta_root"
    printf 'export CMETA_HOST_ROOT=%q\n' "$cmeta_host_root"
    printf 'export RE2C_ROOT=%q\n' "$re2c_root"
    printf 'export QIGAO_NUGET_PACKAGES=%q\n' "$packages"
    if [ "$with_turbowasm" = 1 ]; then
      printf 'export TURBOWASM_ROOT=%q\n' "$(read_json TURBOWASM_ROOT)"
    fi
  } > "$restore_root/env.sh"
  printf 'restored SaltsCore.Native %s and CMeta.Native %s with curl; source %s/env.sh\n' \
    "$saltscore_version" "$cmeta_version" "$restore_root"
fi
