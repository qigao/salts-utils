#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required}"

salts_rid="${1:?Salts target RID is required}"
re2c_rid="${2:?re2c host RID is required}"
with_turbowasm="${3:-0}"
case "$with_turbowasm" in
  0|1) ;;
  *) echo "usage: restore-native-sdk.sh <salts-rid> <re2c-rid> [with-turbowasm:0|1]" >&2; exit 1 ;;
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
    packages="${QIGAO_NUGET_PACKAGES:-$RUNNER_TEMP/qigao-nuget}"
    ;;
  *) echo "restore mode must be ci or local" >&2; exit 1 ;;
esac
mkdir -p "$restore_root"
config="$repository_root/cmake/vcpkg-cache.nuget.config"
project="$restore_root/qigao-native-sdk-restore.csproj"

cat > "$project" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <RestorePackagesWithLockFile>false</RestorePackagesWithLockFile>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Salts.Native" Version="[2.0.0]" />
    <PackageReference Include="Qigao.Re2c.Binary" Version="*" />
    <PackageReference Include="TurboWasm.Native" Version="*" Condition="'$(WithTurboWasm)' == 'true'" />
  </ItemGroup>
</Project>
EOF

restore_args=()
if [ "$with_turbowasm" = "1" ]; then
  restore_args+=("-p:WithTurboWasm=true")
fi
if [ "${#restore_args[@]}" -gt 0 ]; then
  dotnet restore "$project" --packages "$packages" --configfile "$config" \
    --no-cache --force-evaluate "${restore_args[@]}"
else
  # macOS still ships Bash 3.2. Under `set -u`, expanding an empty array
  # raises "unbound variable", so keep the zero-extra-argument path explicit.
  dotnet restore "$project" --packages "$packages" --configfile "$config" \
    --no-cache --force-evaluate
fi

restored_package_dir() {
  python3 - "$restore_root/obj/project.assets.json" "$packages" "$1" <<'PY'
import json, pathlib, sys
assets = json.loads(pathlib.Path(sys.argv[1]).read_text())
matches = [v['path'] for k, v in assets['libraries'].items()
           if k.lower().startswith(sys.argv[3].lower() + '/')]
if len(matches) != 1:
    raise SystemExit('expected one resolved ' + sys.argv[3] + ' package')
print(pathlib.Path(sys.argv[2]) / matches[0])
PY
}

salts_package="$(restored_package_dir Salts.Native)"
salts_version="$(basename "$salts_package")"
re2c_package="$(restored_package_dir Qigao.Re2c.Binary)"
turbowasm_package=""
if [ "$with_turbowasm" = "1" ]; then
  turbowasm_package="$(restored_package_dir TurboWasm.Native)"
fi
salts_root="$salts_package/sdk/$salts_rid"
salts_host_root="$salts_package/sdk/$re2c_rid"
re2c_root="$re2c_package/tools/$re2c_rid"
turbowasm_root=""
if [ "$with_turbowasm" = "1" ]; then
  turbowasm_root="$turbowasm_package/sdk/$salts_rid"
fi
fail() { printf 'native SDK restore error: %s\n' "$*" >&2; exit 1; }

[ -f "$salts_root/lib/cmake/Salts/SaltsConfig.cmake" ] || fail "missing SaltsConfig.cmake under $salts_root"
[ -f "$salts_root/include/cmeta/function.h" ] || fail "missing CMeta function reflection under $salts_root"
[ -f "$salts_host_root/lib/cmake/Salts/SaltsConfig.cmake" ] || fail "missing host SaltsConfig.cmake under $salts_host_root"
[ -f "$re2c_root/share/re2c/stdlib/unicode_categories.re" ] || fail "missing unicode_categories.re under $re2c_root"
[ -f "$re2c_root/share/re2c/stdlib/unicode_properties.re" ] || fail "missing unicode_properties.re under $re2c_root"
[ -f "$re2c_root/bin/re2c" ] || fail "missing re2c executable under $re2c_root"
chmod +x "$re2c_root/bin/re2c"
"$re2c_root/bin/re2c" --version >/dev/null || fail "cannot execute $re2c_root/bin/re2c"

if [ "$with_turbowasm" = "1" ]; then
  [ -f "$turbowasm_root/lib/cmake/TurboWasm/TurboWasmConfig.cmake" ] || fail "missing TurboWasmConfig.cmake under $turbowasm_root"
  [ -f "$turbowasm_root/include/turbowasm/component.h" ] || fail "missing released TurboWasm Component façade under $turbowasm_root"
  grep -q "TurboWasm::Component" "$turbowasm_root/lib/cmake/TurboWasm/TurboWasmTargets.cmake" ||
    fail "released TurboWasm package does not export TurboWasm::Component"
  turbowasm_version="$(basename "$turbowasm_package")"
  if [ "$mode" = ci ]; then
    printf "TURBOWASM_ROOT=%s\n" "$turbowasm_root" >> "$GITHUB_ENV"
    printf "TURBOWASM_VERSION=%s\n" "$turbowasm_version" >> "$GITHUB_ENV"
  fi
  printf 'restored TurboWasm.Native %s for %s\n' "$turbowasm_version" "$salts_rid"
fi

if [ "$mode" = local ]; then
  {
    printf 'export SALTS_ROOT=%q\n' "$salts_root"
    printf 'export SALTS_HOST_ROOT=%q\n' "$salts_host_root"
    printf 'export RE2C_ROOT=%q\n' "$re2c_root"
    printf 'export QIGAO_NUGET_PACKAGES=%q\n' "$packages"
    if [ "$with_turbowasm" = 1 ]; then
      printf 'export TURBOWASM_ROOT=%q\n' "$turbowasm_root"
    fi
  } > "$restore_root/env.sh"
  printf 'restored Salts.Native %s for %s; source %s/env.sh\n' "$salts_version" "$salts_rid" "$restore_root"
  exit 0
fi

printf "SALTS_ROOT=%s\n" "$salts_root" >> "$GITHUB_ENV"
printf "SALTS_HOST_ROOT=%s\n" "$salts_host_root" >> "$GITHUB_ENV"
printf "SALTS_VERSION=%s\n" "$salts_version" >> "$GITHUB_ENV"
printf 'restored Salts.Native %s for %s\n' "$salts_version" "$salts_rid"
printf "RE2C_ROOT=%s\n" "$re2c_root" >> "$GITHUB_ENV"
printf "QIGAO_NUGET_PACKAGES=%s\n" "$packages" >> "$GITHUB_ENV"
printf "%s\n" "$re2c_root/bin" >> "$GITHUB_PATH"
