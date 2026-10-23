param(
  [Parameter(Mandatory=$true)][string]$SaltsRid,
  [Parameter(Mandatory=$true)][string]$Re2cRid,
  [switch]$WithTurboWasm,
  [switch]$Local
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) { throw 'GITHUB_TOKEN is required' }
if (-not $Local) {
  foreach ($name in @('RUNNER_TEMP', 'GITHUB_ENV', 'GITHUB_PATH')) {
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))) { throw "$name is required" }
  }
}
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$restoreRoot = if ($Local) { Join-Path $repositoryRoot 'build/native-sdk' } else { $env:RUNNER_TEMP }
$packages = if ($env:QIGAO_NUGET_PACKAGES) { $env:QIGAO_NUGET_PACKAGES } elseif ($Local) { Join-Path $repositoryRoot 'stage/nuget' } else { Join-Path $restoreRoot 'qigao-native-packages' }
New-Item -ItemType Directory -Path $restoreRoot,$packages -Force | Out-Null
$result = Join-Path $restoreRoot 'saltsutils-native-sdk.json'
$args = @((Join-Path $repositoryRoot 'cmake/ci/github_packages.py'), 'native-sdk',
  '--packages', $packages, '--salts-rid', $SaltsRid, '--host-rid', $Re2cRid, '--result', $result)
if ($WithTurboWasm) { $args += '--with-turbowasm' }
& python @args
if ($LASTEXITCODE -ne 0) { throw 'curl GitHub Packages restore failed' }
$resolved = Get-Content -LiteralPath $result -Raw | ConvertFrom-Json

if ($Local) {
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
  $env:SALTS_ROOT = Set-LocalPackageLink "salts/$SaltsRid" $resolved.SALTS_ROOT
  $env:SALTS_HOST_ROOT = Set-LocalPackageLink "salts/$Re2cRid" $resolved.SALTS_HOST_ROOT
  $env:RE2C_ROOT = Set-LocalPackageLink "re2c/$Re2cRid" $resolved.RE2C_ROOT
  $env:QIGAO_NUGET_PACKAGES = $packages
  if ($WithTurboWasm) { $env:TURBOWASM_ROOT = $resolved.TURBOWASM_ROOT }
}
Write-Host "restored Salts.Native $($resolved.SALTS_VERSION) with curl for $SaltsRid"
