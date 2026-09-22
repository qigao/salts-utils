#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required}"
: "${RUNNER_TEMP:?RUNNER_TEMP is required}"
: "${GITHUB_ENV:?GITHUB_ENV is required}"
: "${GITHUB_PATH:?GITHUB_PATH is required}"

salts_rid="${1:?Salts target RID is required}"
re2c_rid="${2:?re2c host RID is required}"
salts_version="${SALTS_SDK_VERSION:-1.1.0}"
re2c_version="${RE2C_TOOLS_VERSION:-4.6.1}"
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
    <PackageReference Include="Qigao.Re2c.Tools" Version="[$re2c_version]" />
  </ItemGroup>
</Project>
EOF

dotnet restore "$project" --packages "$packages" --configfile "$config" --no-cache

salts_root="$packages/salts.native/$salts_version/sdk/$salts_rid"
re2c_root="$packages/qigao.re2c.tools/$re2c_version/tools/$re2c_rid"
test -f "$salts_root/lib/cmake/Salts/SaltsConfig.cmake"
test -f "$re2c_root/share/re2c/stdlib/unicode_categories.re"
test -f "$re2c_root/share/re2c/stdlib/unicode_properties.re"
test -f "$re2c_root/bin/re2c"
chmod +x "$re2c_root/bin/re2c"
test "$("$re2c_root/bin/re2c" --version)" = "re2c 4.6"

printf "SALTS_ROOT=%s\n" "$salts_root" >> "$GITHUB_ENV"
printf "RE2C_ROOT=%s\n" "$re2c_root" >> "$GITHUB_ENV"
printf "QIGAO_NUGET_PACKAGES=%s\n" "$packages" >> "$GITHUB_ENV"
printf "%s\n" "$re2c_root/bin" >> "$GITHUB_PATH"
