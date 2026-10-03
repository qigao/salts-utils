#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required}"
: "${RUNNER_TEMP:?RUNNER_TEMP is required}"
: "${GITHUB_ENV:?GITHUB_ENV is required}"
: "${GITHUB_PATH:?GITHUB_PATH is required}"

salts_rid="${1:?Salts target RID is required}"
re2c_rid="${2:?re2c host RID is required}"
with_turbowasm="${3:-0}"
case "$with_turbowasm" in
  0|1) ;;
  *) echo "usage: restore-native-sdk.sh <salts-rid> <re2c-rid> [with-turbowasm:0|1]" >&2; exit 1 ;;
esac
packages="${QIGAO_NUGET_PACKAGES:-$RUNNER_TEMP/qigao-nuget}"
config="$RUNNER_TEMP/NuGet.Config"
project="$RUNNER_TEMP/qigao-native-sdk-restore.csproj"

cat > "$config" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /></packageSources>
</configuration>
EOF

dotnet nuget add source "https://nuget.pkg.github.com/qigao/index.json" \
  --name github --username qigao --password "$GITHUB_TOKEN" \
  --store-password-in-clear-text --configfile "$config"

cat > "$project" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <RestorePackagesWithLockFile>false</RestorePackagesWithLockFile>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Salts.Native" Version="*" />
    <PackageReference Include="Qigao.Re2c.Binary" Version="*" />
    <PackageReference Include="TurboWasm.Native" Version="*" Condition="'$(WithTurboWasm)' == 'true'" />
  </ItemGroup>
</Project>
EOF

restore_args=()
if [ "$with_turbowasm" = "1" ]; then
  restore_args+=("-p:WithTurboWasm=true")
fi
dotnet restore "$project" --packages "$packages" --configfile "$config" --no-cache --force-evaluate "${restore_args[@]}"

single_package_dir() {
  local package_root="$1"
  local package_name="$2"
  local found=()
  while IFS= read -r path; do
    found+=("$path")
  done < <(find "$package_root" -mindepth 1 -maxdepth 1 -type d -print)
  [ "${#found[@]}" -eq 1 ] || {
    printf 'native SDK restore error: expected exactly one %s package, found %s\n' "$package_name" "${#found[@]}" >&2
    exit 1
  }
  printf '%s\n' "${found[0]}"
}

salts_package="$(single_package_dir "$packages/salts.native" Salts.Native)"
re2c_package="$(single_package_dir "$packages/qigao.re2c.binary" Qigao.Re2c.Binary)"
turbowasm_package=""
if [ "$with_turbowasm" = "1" ]; then
  turbowasm_package="$(single_package_dir "$packages/turbowasm.native" TurboWasm.Native)"
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
  printf "TURBOWASM_ROOT=%s\n" "$turbowasm_root" >> "$GITHUB_ENV"
fi

printf "SALTS_ROOT=%s\n" "$salts_root" >> "$GITHUB_ENV"
printf "SALTS_HOST_ROOT=%s\n" "$salts_host_root" >> "$GITHUB_ENV"
printf "RE2C_ROOT=%s\n" "$re2c_root" >> "$GITHUB_ENV"
printf "QIGAO_NUGET_PACKAGES=%s\n" "$packages" >> "$GITHUB_ENV"
printf "%s\n" "$re2c_root/bin" >> "$GITHUB_PATH"
