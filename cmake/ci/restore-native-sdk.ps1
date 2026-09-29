param(
  [Parameter(Mandatory=$true)][string]$SaltsRid,
  [Parameter(Mandatory=$true)][string]$Re2cRid
)
$ErrorActionPreference = "Stop"

foreach ($name in @("GITHUB_TOKEN", "RUNNER_TEMP", "GITHUB_ENV", "GITHUB_PATH")) {
  if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))) {
    throw "$name is required"
  }
}

$packages = if ($env:QIGAO_NUGET_PACKAGES) { $env:QIGAO_NUGET_PACKAGES } else { Join-Path $env:RUNNER_TEMP "qigao-nuget" }
$config = Join-Path $env:RUNNER_TEMP "qigao-nuget.config"
$project = Join-Path $env:RUNNER_TEMP "qigao-native-sdk-restore.csproj"

@'
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources><clear /></packageSources>
</configuration>
'@ | Set-Content -LiteralPath $config -Encoding utf8NoBOM

dotnet nuget add source "https://nuget.pkg.github.com/qigao/index.json" --name github --username qigao --password $env:GITHUB_TOKEN --store-password-in-clear-text --configfile $config
if ($LASTEXITCODE -ne 0) { throw "failed to configure GitHub Packages source" }

@'
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
'@ | Set-Content -LiteralPath $project -Encoding utf8NoBOM

dotnet restore $project --packages $packages --configfile $config --no-cache --force-evaluate
if ($LASTEXITCODE -ne 0) { throw "failed to restore latest native SDKs" }

$saltsPackages = @(Get-ChildItem -LiteralPath (Join-Path $packages "salts.native") -Directory)
if ($saltsPackages.Count -ne 1) { throw "expected exactly one restored Salts.Native package, found $($saltsPackages.Count)" }
$re2cPackages = @(Get-ChildItem -LiteralPath (Join-Path $packages "qigao.re2c.binary") -Directory)
if ($re2cPackages.Count -ne 1) { throw "expected exactly one restored Qigao.Re2c.Binary package, found $($re2cPackages.Count)" }

$saltsRoot = Join-Path $saltsPackages[0].FullName "sdk\$SaltsRid"
$re2cRoot = Join-Path $re2cPackages[0].FullName "tools\$Re2cRid"
$required = @(
  (Join-Path $saltsRoot "lib\cmake\Salts\SaltsConfig.cmake"),
  (Join-Path $saltsRoot "include\cmeta\function.h"),
  (Join-Path $re2cRoot "bin\re2c.exe"),
  (Join-Path $re2cRoot "share\re2c\stdlib\unicode_categories.re"),
  (Join-Path $re2cRoot "share\re2c\stdlib\unicode_properties.re")
)
foreach ($path in $required) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "missing restored SDK file: $path" }
}
& (Join-Path $re2cRoot "bin\re2c.exe") --version | Out-Null
if ($LASTEXITCODE -ne 0) { throw "restored re2c executable cannot run" }

"SALTS_ROOT=$saltsRoot" >> $env:GITHUB_ENV
"RE2C_ROOT=$re2cRoot" >> $env:GITHUB_ENV
"QIGAO_NUGET_PACKAGES=$packages" >> $env:GITHUB_ENV
(Join-Path $re2cRoot "bin") >> $env:GITHUB_PATH
