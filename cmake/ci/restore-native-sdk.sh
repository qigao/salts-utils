#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required}"
: "${RUNNER_TEMP:?RUNNER_TEMP is required}"
: "${GITHUB_ENV:?GITHUB_ENV is required}"
: "${GITHUB_PATH:?GITHUB_PATH is required}"

salts_rid="${1:?Salts target RID is required}"
re2c_rid="${2:?re2c host RID is required}"
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
</Project>
EOF

(
  cd "$RUNNER_TEMP"
  dotnet add "$project" package Salts.Native
  dotnet add "$project" package Qigao.Re2c.Binary
)
dotnet restore "$project" --packages "$packages" --configfile "$config" --no-cache --force-evaluate

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
salts_root="$salts_package/sdk/$salts_rid"
re2c_root="$re2c_package/tools/$re2c_rid"
fail() { printf 'native SDK restore error: %s\n' "$*" >&2; exit 1; }

[ -f "$salts_root/lib/cmake/Salts/SaltsConfig.cmake" ] || fail "missing SaltsConfig.cmake under $salts_root"
[ -f "$salts_root/include/cmeta/function.h" ] || fail "missing CMeta function reflection under $salts_root"
[ -f "$re2c_root/share/re2c/stdlib/unicode_categories.re" ] || fail "missing unicode_categories.re under $re2c_root"
[ -f "$re2c_root/share/re2c/stdlib/unicode_properties.re" ] || fail "missing unicode_properties.re under $re2c_root"
[ -f "$re2c_root/bin/re2c" ] || fail "missing re2c executable under $re2c_root"
chmod +x "$re2c_root/bin/re2c"
"$re2c_root/bin/re2c" --version >/dev/null || fail "cannot execute $re2c_root/bin/re2c"

printf "SALTS_ROOT=%s\n" "$salts_root" >> "$GITHUB_ENV"
printf "RE2C_ROOT=%s\n" "$re2c_root" >> "$GITHUB_ENV"
printf "QIGAO_NUGET_PACKAGES=%s\n" "$packages" >> "$GITHUB_ENV"
printf "%s\n" "$re2c_root/bin" >> "$GITHUB_PATH"
