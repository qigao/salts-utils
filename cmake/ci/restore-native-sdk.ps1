param(
  [Parameter(Mandatory=$true)][string]$SaltsRid,
  [Parameter(Mandatory=$true)][string]$Re2cRid,
  [switch]$WithTurboWasm,
  [switch]$Local
)
$ErrorActionPreference = "Stop"
$tag = $env:SALTS_CANONICAL_TAG
if (-not $tag) { throw "An explicit published SALTS_CANONICAL_TAG is required" }
if ($tag -match '^v2\.3\.0-rc\.([1-9][0-9]*)$') {
  if ([int]$Matches[1] -le 17) {
    throw "Salts rc.17 or earlier lacks the canonical ServiceHost/Configurator"
  }
} elseif ($tag -ne 'v2.3.0') {
  throw "Unsupported Salts canonical release tag: $tag"
}
$saltsVersion = $tag.Substring(1)

$requiredEnvironment = @("GITHUB_TOKEN")
if (-not $Local) { $requiredEnvironment += @("RUNNER_TEMP", "GITHUB_ENV", "GITHUB_PATH") }
foreach ($name in $requiredEnvironment) {
  if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))) {
    throw "$name is required"
  }
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$restoreRoot = if ($Local) { Join-Path $repositoryRoot "build/native-sdk" } else { $env:RUNNER_TEMP }
$packages = if ($env:QIGAO_NUGET_PACKAGES) { $env:QIGAO_NUGET_PACKAGES } elseif ($Local) { Join-Path $repositoryRoot "stage/nuget" } else { Join-Path $restoreRoot "qigao-nuget" }
$packages = [IO.Path]::GetFullPath($packages)
$config = Join-Path $repositoryRoot "cmake/vcpkg-cache.nuget.config"
$project = Join-Path $restoreRoot "qigao-native-sdk-restore.csproj"
New-Item -ItemType Directory -Path $restoreRoot -Force | Out-Null

@'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <RestorePackagesWithLockFile>false</RestorePackagesWithLockFile>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Salts.Native" Version="[$(SaltsCanonicalVersion)]" />
    <PackageReference Include="Qigao.Re2c.Binary" Version="*" />
    <PackageReference Include="TurboWasm.Native" Version="*" Condition="'$(WithTurboWasm)' == 'true'" />
  </ItemGroup>
</Project>
'@ | Set-Content -LiteralPath $project -Encoding utf8NoBOM

$restoreArgs = @($project, "--packages", $packages, "--configfile", $config, "--no-cache", "--force-evaluate",
  "-p:SaltsCanonicalVersion=$saltsVersion")
if ($WithTurboWasm) { $restoreArgs += "-p:WithTurboWasm=true" }
dotnet restore @restoreArgs
if ($LASTEXITCODE -ne 0) { throw "failed to restore the latest Salts and native tools" }

$assets = Get-Content -LiteralPath (Join-Path $restoreRoot "obj/project.assets.json") -Raw | ConvertFrom-Json -AsHashtable
function Get-RestoredPackage([string]$name) {
  $keys = @($assets.libraries.Keys | Where-Object { $_.StartsWith("$name/", [StringComparison]::OrdinalIgnoreCase) })
  if ($keys.Count -ne 1) { throw "expected one resolved $name package" }
  return Join-Path $packages $assets.libraries[$keys[0]].path
}

$saltsPackage = Get-RestoredPackage "Salts.Native"
if ((Split-Path $saltsPackage -Leaf) -ne $saltsVersion) {
  throw "Resolved Salts.Native package is not the explicitly tagged version"
}
$saltsRoot = Join-Path $saltsPackage "sdk\$SaltsRid"
$saltsHostRoot = Join-Path $saltsPackage "sdk\$Re2cRid"
$re2cRoot = Join-Path (Get-RestoredPackage "Qigao.Re2c.Binary") "tools\$Re2cRid"
$turbowasmPackage = if ($WithTurboWasm) { Get-RestoredPackage "TurboWasm.Native" } else { $null }
$turbowasmRoot = if ($WithTurboWasm) { Join-Path $turbowasmPackage "sdk\$SaltsRid" } else { $null }
$required = @(
  (Join-Path $saltsRoot "lib\cmake\Salts\SaltsConfig.cmake"),
  (Join-Path $saltsRoot "include\cmeta\function.h"),
  (Join-Path $saltsHostRoot "lib\cmake\Salts\SaltsConfig.cmake"),
  (Join-Path $re2cRoot "bin\re2c.exe"),
  (Join-Path $re2cRoot "share\re2c\stdlib\unicode_categories.re"),
  (Join-Path $re2cRoot "share\re2c\stdlib\unicode_properties.re")
)
foreach ($path in $required) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "missing restored SDK file: $path" }
}
# Exact released Salts package: no source checkout, old target, alias, or
# floating NuGet resolution may masquerade as the canonical SDK.
foreach ($relative in @(
    'include/salts/service_host.h',
    'include/salts/configurator.h',
    'include/salts/component_provider_abi.h',
    'lib/cmake/Salts/SaltsTargets.cmake',
    'salts-sdk-manifest.txt')) {
  if (-not (Test-Path -LiteralPath (Join-Path $saltsRoot $relative) -PathType Leaf)) {
    throw "Tagged Salts SDK missing canonical file: $relative"
  }
}
foreach ($oldHeader in @('component_plugin.h','component_plugin_abi.h')) {
  if (Test-Path -LiteralPath (Join-Path $saltsRoot "include/salts/$oldHeader")) {
    throw "Tagged Salts SDK still exports obsolete $oldHeader"
  }
}
$manifest = Get-Content -LiteralPath (Join-Path $saltsRoot 'salts-sdk-manifest.txt')
if (-not ($manifest -contains "version=$saltsVersion")) {
  throw "Canonical Salts tag and installed RID manifest disagree"
}
$targetsFile = Join-Path $saltsRoot 'lib/cmake/Salts/SaltsTargets.cmake'
foreach ($target in @('Salts::Configurator','Salts::ServiceHost','Salts::ComponentProviderABI','Salts::Unicode')) {
  if (-not (Select-String -LiteralPath $targetsFile -SimpleMatch $target -Quiet)) {
    throw "Canonical Salts SDK missing $target"
  }
}
$targetText = Get-Content -LiteralPath $targetsFile -Raw
if ($targetText -match 'add_library\(Salts::Component(Plugin)?\s') {
  throw "Legacy Component runtime exported by supposed canonical SDK"
}

& (Join-Path $re2cRoot "bin\re2c.exe") --version | Out-Null
if ($LASTEXITCODE -ne 0) { throw "restored re2c executable cannot run" }

if ($WithTurboWasm) {
  $twRequired = @(
    (Join-Path $turbowasmRoot "lib\cmake\TurboWasm\TurboWasmConfig.cmake"),
    (Join-Path $turbowasmRoot "include\turbowasm\component.h"),
    (Join-Path $turbowasmRoot "lib\cmake\TurboWasm\TurboWasmTargets.cmake")
  )
  foreach ($path in $twRequired) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "missing restored TurboWasm SDK file: $path" }
  }
  if (-not (Select-String -LiteralPath (Join-Path $turbowasmRoot "lib\cmake\TurboWasm\TurboWasmTargets.cmake") -SimpleMatch "TurboWasm::Component" -Quiet)) {
    throw "released TurboWasm package does not export TurboWasm::Component"
  }
  $turbowasmVersion = Split-Path $turbowasmPackage -Leaf
  $env:TURBOWASM_ROOT = $turbowasmRoot
  if (-not $Local) {
    "TURBOWASM_ROOT=$turbowasmRoot" >> $env:GITHUB_ENV
    "TURBOWASM_VERSION=$turbowasmVersion" >> $env:GITHUB_ENV
  }
  Write-Host "restored TurboWasm.Native $turbowasmVersion for $SaltsRid"
}

if ($Local) {
  # Stable entry paths let an already running IDE consume the resolved package.
  # Only replace junctions owned by this restore step, never SDK directories.
  function Set-LocalPackageLink([string]$relativePath, [string]$target) {
    $link = Join-Path $repositoryRoot "stage/dependencies/$relativePath"
    New-Item -ItemType Directory -Path (Split-Path $link -Parent) -Force | Out-Null
    $existing = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
    if ($existing) {
      if ($existing.LinkType -ne 'Junction') { throw "refusing to replace non-junction SDK path: $link" }
      if ($existing.Target -eq $target) { return $link }
      Remove-Item -LiteralPath $link -Force
    }
    New-Item -ItemType Junction -Path $link -Target $target | Out-Null
    return $link
  }
  $saltsRoot = Set-LocalPackageLink "salts/$SaltsRid" $saltsRoot
  $saltsHostRoot = Set-LocalPackageLink "salts/$Re2cRid" $saltsHostRoot
  $re2cRoot = Set-LocalPackageLink "re2c/$Re2cRid" $re2cRoot
}

$env:SALTS_ROOT = $saltsRoot
$env:SALTS_HOST_ROOT = $saltsHostRoot
$env:RE2C_ROOT = $re2cRoot
$env:QIGAO_NUGET_PACKAGES = $packages
if (-not $Local) {
  "SALTS_ROOT=$saltsRoot" >> $env:GITHUB_ENV
  "SALTS_HOST_ROOT=$saltsHostRoot" >> $env:GITHUB_ENV
  "SALTS_VERSION=$(Split-Path $saltsPackage -Leaf)" >> $env:GITHUB_ENV
  "RE2C_ROOT=$re2cRoot" >> $env:GITHUB_ENV
  "QIGAO_NUGET_PACKAGES=$packages" >> $env:GITHUB_ENV
  (Join-Path $re2cRoot "bin") >> $env:GITHUB_PATH
}
Write-Host "restored Salts.Native $(Split-Path $saltsPackage -Leaf) for $SaltsRid"
