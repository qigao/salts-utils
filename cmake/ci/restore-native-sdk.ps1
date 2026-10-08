param(
  [Parameter(Mandatory=$true)][string]$SaltsRid,
  [Parameter(Mandatory=$true)][string]$Re2cRid,
  [switch]$WithTurboWasm,
  [switch]$Local
)
$ErrorActionPreference = "Stop"

$requiredEnvironment = @("GITHUB_TOKEN", "VCPKG_ROOT")
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
New-Item -ItemType Directory -Path $restoreRoot -Force | Out-Null
$nugetOutput = @(& (Join-Path $env:VCPKG_ROOT "vcpkg.exe") fetch nuget)
if ($LASTEXITCODE -ne 0) { throw "failed to acquire the vcpkg NuGet client" }
$nuget = $nugetOutput[-1]
if (-not (Test-Path -LiteralPath $nuget -PathType Leaf)) { throw "NuGet client is missing: $nuget" }
# A fresh destination forces latest resolution without replacing SDKs in use.
$installRoot = Join-Path $packages ("restore." + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $installRoot | Out-Null
function Install-NativePackage([string]$name) {
  & $nuget install $name -ConfigFile $config -OutputDirectory $installRoot `
    -ExcludeVersion -PackageSaveMode nuspec -NoHttpCache -DirectDownload -NonInteractive | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "failed to install the latest $name package" }
  return Join-Path $installRoot $name
}

function Get-PackageVersion([string]$package) {
  $manifests = @(Get-ChildItem -LiteralPath $package -Filter '*.nuspec' -File)
  if ($manifests.Count -ne 1) { throw "expected one package manifest under $package" }
  [xml]$manifest = Get-Content -LiteralPath $manifests[0].FullName -Raw
  $version = [string]$manifest.package.metadata.version
  if ($manifest.package.metadata.id -ine (Split-Path $package -Leaf) -or [string]::IsNullOrWhiteSpace($version)) {
    throw "invalid package identity under $package"
  }
  return $version
}

$saltsPackage = Install-NativePackage "Salts.Native"
$saltsVersion = Get-PackageVersion $saltsPackage
$saltsRoot = Join-Path $saltsPackage "sdk\$SaltsRid"
$saltsHostRoot = Join-Path $saltsPackage "sdk\$Re2cRid"
$re2cRoot = Join-Path (Install-NativePackage "Qigao.Re2c.Binary") "tools\$Re2cRid"
$turbowasmPackage = if ($WithTurboWasm) { Install-NativePackage "TurboWasm.Native" } else { $null }
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
  $turbowasmVersion = Get-PackageVersion $turbowasmPackage
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
  "SALTS_VERSION=$saltsVersion" >> $env:GITHUB_ENV
  "RE2C_ROOT=$re2cRoot" >> $env:GITHUB_ENV
  "QIGAO_NUGET_PACKAGES=$packages" >> $env:GITHUB_ENV
  (Join-Path $re2cRoot "bin") >> $env:GITHUB_PATH
}
Write-Host "restored Salts.Native $saltsVersion for $SaltsRid"
