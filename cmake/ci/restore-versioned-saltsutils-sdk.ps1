param(
  [Parameter(Mandatory=$true)][string]$Version,
  [Parameter(Mandatory=$true)][string]$Rid,
  [Parameter(Mandatory=$true)][string]$Destination
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) { throw "GITHUB_TOKEN is required" }
if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { throw "RUNNER_TEMP is required" }

$packages = if ($env:QIGAO_NUGET_PACKAGES) {
  $env:QIGAO_NUGET_PACKAGES
} else {
  Join-Path $env:RUNNER_TEMP "qigao-nuget"
}
$config = Join-Path $env:RUNNER_TEMP "qigao-saltsutils-baseline.config"
$project = Join-Path $env:RUNNER_TEMP "qigao-saltsutils-baseline.csproj"

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
    <PackageReference Include="SaltsUtils.Native" Version="[$Version]" />
  </ItemGroup>
</Project>
"@ | Set-Content -LiteralPath $project

dotnet restore $project --packages $packages --configfile $config --no-cache
if ($LASTEXITCODE -ne 0) { throw "failed to restore SaltsUtils.Native $Version" }

$source = Join-Path $packages "saltsutils.native\$Version\sdk\$Rid"
$configPath = Join-Path $source "lib\cmake\SaltsUtils\SaltsUtilsConfig.cmake"
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
  throw "published SaltsUtils SDK is missing: $configPath"
}

if (Test-Path -LiteralPath $Destination) {
  Remove-Item -LiteralPath $Destination -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Copy-Item -Path (Join-Path $source "*") -Destination $Destination -Recurse -Force

$manifest = Join-Path $Destination "salts-utils-sdk-manifest.txt"
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
  throw "published SaltsUtils SDK manifest is missing: $manifest"
}

Write-Host "Restored published SaltsUtils.Native $Version ($Rid) to $Destination"
