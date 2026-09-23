#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required}"
: "${RUNNER_TEMP:?RUNNER_TEMP is required}"
: "${GITHUB_ENV:?GITHUB_ENV is required}"
: "${GITHUB_PATH:?GITHUB_PATH is required}"

salts_rid="${1:?Salts target RID is required}"
re2c_rid="${2:?re2c host RID is required}"
salts_version="${SALTS_SDK_VERSION:-1.5.0}"
re2c_version="${RE2C_BINARY_VERSION:-4.6.3}"
packages="${QIGAO_NUGET_PACKAGES:-$RUNNER_TEMP/qigao-nuget}"
config="$RUNNER_TEMP/qigao-nuget.config"
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

cat > "$project" <<EOF
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net8.0</TargetFramework></PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Salts.Native" Version="[$salts_version]" />
    <PackageReference Include="Qigao.Re2c.Binary" Version="[$re2c_version]" />
  </ItemGroup>
</Project>
EOF

dotnet restore "$project" --packages "$packages" --configfile "$config" --no-cache

salts_package="$packages/salts.native/$salts_version"
re2c_package="$packages/qigao.re2c.binary/$re2c_version"
fail() { printf 'native SDK restore error: %s\n' "$*" >&2; exit 1; }
[ -d "$salts_package" ] || fail "missing Salts.Native package root: $salts_package"
[ -d "$re2c_package" ] || fail "missing Qigao.Re2c.Binary package root: $re2c_package"
salts_root="$salts_package/sdk/$salts_rid"
re2c_root="$re2c_package/tools/$re2c_rid"
[ -f "$salts_root/lib/cmake/Salts/SaltsConfig.cmake" ] || fail "missing SaltsConfig.cmake under $salts_root"
[ -f "$salts_root/include/cmeta/function.h" ] || fail "missing CMeta function reflection under $salts_root"
[ -f "$re2c_root/share/re2c/stdlib/unicode_categories.re" ] || fail "missing unicode_categories.re under $re2c_root"
[ -f "$re2c_root/share/re2c/stdlib/unicode_properties.re" ] || fail "missing unicode_properties.re under $re2c_root"
[ -f "$re2c_root/bin/re2c" ] || fail "missing re2c executable under $re2c_root"
chmod +x "$re2c_root/bin/re2c"
actual_re2c="$("$re2c_root/bin/re2c" --version)" || fail "cannot execute $re2c_root/bin/re2c"
[ "$actual_re2c" = "re2c 4.6" ] || fail "unexpected re2c version: $actual_re2c"

printf "SALTS_ROOT=%s\n" "$salts_root" >> "$GITHUB_ENV"
printf "RE2C_ROOT=%s\n" "$re2c_root" >> "$GITHUB_ENV"
printf "QIGAO_NUGET_PACKAGES=%s\n" "$packages" >> "$GITHUB_ENV"
printf "%s\n" "$re2c_root/bin" >> "$GITHUB_PATH"
