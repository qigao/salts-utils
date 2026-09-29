#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_TOKEN:?GITHUB_TOKEN is required}"
: "${RUNNER_TEMP:?RUNNER_TEMP is required}"
: "${GITHUB_ENV:?GITHUB_ENV is required}"
: "${GITHUB_PATH:?GITHUB_PATH is required}"

salts_rid="${1:?Salts target RID is required}"
re2c_rid="${2:?re2c host RID is required}"
packages="${QIGAO_NUGET_PACKAGES:-$RUNNER_TEMP/qigao-nuget}"
config="$RUNNER_TEMP/qigao-nuget.config"
project="$RUNNER_TEMP/qigao-native-sdk-restore.csproj"
assets="$RUNNER_TEMP/obj/project.assets.json"

cat > "$config" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /></packageSources>
</configuration>
EOF

dotnet nuget add source "https://nuget.pkg.github.com/qigao/index.json"   --name github --username qigao --password "$GITHUB_TOKEN"   --store-password-in-clear-text --configfile "$config"

cat > "$project" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <RestorePackagesWithLockFile>false</RestorePackagesWithLockFile>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Salts.Native" Version="*" />
    <PackageReference Include="Qigao.Re2c.Binary" Version="*" />
  </ItemGroup>
</Project>
EOF

dotnet restore "$project" --packages "$packages" --configfile "$config" --no-cache --force-evaluate
[ -f "$assets" ] || { echo "native SDK restore error: missing $assets" >&2; exit 1; }

resolved_version() {
  python3 - "$assets" "$1" <<'PY'
import json, sys
assets, package = sys.argv[1], sys.argv[2]
with open(assets, encoding="utf-8") as f:
    libraries = json.load(f)["libraries"]
prefix = package.lower() + "/"
for key in libraries:
    if key.lower().startswith(prefix):
        print(key.split("/", 1)[1])
        break
else:
    raise SystemExit(f"package not resolved: {package}")
PY
}

salts_version="$(resolved_version Salts.Native)"
re2c_version="$(resolved_version Qigao.Re2c.Binary)"
salts_root="$packages/salts.native/$salts_version/sdk/$salts_rid"
re2c_root="$packages/qigao.re2c.binary/$re2c_version/tools/$re2c_rid"

fail() { printf 'native SDK restore error: %s\n' "$*" >&2; exit 1; }
[ -f "$salts_root/lib/cmake/Salts/SaltsConfig.cmake" ] || fail "missing SaltsConfig.cmake under $salts_root"
[ -f "$salts_root/include/cmeta/function.h" ] || fail "missing CMeta function reflection under $salts_root"
[ -f "$re2c_root/share/re2c/stdlib/unicode_categories.re" ] || fail "missing unicode_categories.re under $re2c_root"
[ -f "$re2c_root/share/re2c/stdlib/unicode_properties.re" ] || fail "missing unicode_properties.re under $re2c_root"
[ -f "$re2c_root/bin/re2c" ] || fail "missing re2c executable under $re2c_root"
chmod +x "$re2c_root/bin/re2c"

printf "SALTS_ROOT=%s\n" "$salts_root" >> "$GITHUB_ENV"
printf "RE2C_ROOT=%s\n" "$re2c_root" >> "$GITHUB_ENV"
printf "QIGAO_NUGET_PACKAGES=%s\n" "$packages" >> "$GITHUB_ENV"
printf "SALTS_PACKAGE_VERSION=%s\n" "$salts_version" >> "$GITHUB_ENV"
printf "RE2C_PACKAGE_VERSION=%s\n" "$re2c_version" >> "$GITHUB_ENV"
printf "%s\n" "$re2c_root/bin" >> "$GITHUB_PATH"

printf 'Restored latest Salts.Native -> %s\n' "$salts_version"
printf 'Restored latest Qigao.Re2c.Binary -> %s\n' "$re2c_version"
