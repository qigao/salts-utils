param(
  [Parameter(Mandatory=$true)][string]$SaltsRid,
  [Parameter(Mandatory=$true)][string]$Re2cRid
)
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) { throw "GITHUB_TOKEN is required" }
if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { throw "RUNNER_TEMP is required" }
if ([string]::IsNullOrWhiteSpace($env:GITHUB_ENV)) { throw "GITHUB_ENV is required" }
if ([string]::IsNullOrWhiteSpace($env:GITHUB_PATH)) { throw "GITHUB_PATH is required" }

$saltsVersion = if ($env:SALTS_SDK_VERSION) { $env:SALTS_SDK_VERSION } else { "1.7.3" }
$re2cVersion = if ($env:RE2C_BINARY_VERSION) { $env:RE2C_BINARY_VERSION } else { "4.6.3" }
$packages = if ($env:QIGAO_NUGET_PACKAGES) { $env:QIGAO_NUGET_PACKAGES } else { Join-Path $env:RUNNER_TEMP "qigao-nuget" }
$config = Join-Path $env:RUNNER_TEMP "qigao-nuget.config"
$project = Join-Path $env:RUNNER_TEMP "qigao-native-sdk-restore.csproj"

@'
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /></packageSources>
</configuration>
'@ | Set-Content -LiteralPath $config

dotnet nuget add source "https://nuget.pkg.github.com/qigao/index.json" --name github --username qigao --password $env:GITHUB_TOKEN --store-password-in-clear-text --configfile $config
if ($LASTEXITCODE -ne 0) { throw "failed to configure GitHub Packages source" }

@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net8.0</TargetFramework></PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Salts.Native" Version="[$saltsVersion]" />
    <PackageReference Include="Qigao.Re2c.Binary" Version="[$re2cVersion]" />
  </ItemGroup>
</Project>
"@ | Set-Content -LiteralPath $project

dotnet restore $project --packages $packages --configfile $config --no-cache
if ($LASTEXITCODE -ne 0) { throw "failed to restore versioned native SDKs" }

$saltsRoot = Join-Path $packages "salts.native\$saltsVersion\sdk\$SaltsRid"
$re2cRoot = Join-Path $packages "qigao.re2c.binary\$re2cVersion\tools\$Re2cRid"
$saltsConfig = Join-Path $saltsRoot "lib\cmake\Salts\SaltsConfig.cmake"
$functionHeader = Join-Path $saltsRoot "include\cmeta\function.h"
$re2cExe = Join-Path $re2cRoot "bin\re2c.exe"
$unicodeCategories = Join-Path $re2cRoot "share\re2c\stdlib\unicode_categories.re"
$unicodeProperties = Join-Path $re2cRoot "share\re2c\stdlib\unicode_properties.re"
foreach ($path in @($saltsConfig, $functionHeader, $re2cExe, $unicodeCategories, $unicodeProperties)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "missing restored SDK file: $path" }
}
$version = (& $re2cExe --version).Trim()
if ($version -ne "re2c 4.6") { throw "unexpected re2c version: $version" }

"SALTS_ROOT=$saltsRoot" >> $env:GITHUB_ENV
"RE2C_ROOT=$re2cRoot" >> $env:GITHUB_ENV
"QIGAO_NUGET_PACKAGES=$packages" >> $env:GITHUB_ENV
(Join-Path $re2cRoot "bin") >> $env:GITHUB_PATH
