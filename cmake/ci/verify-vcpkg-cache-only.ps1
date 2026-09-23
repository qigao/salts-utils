param(
  [switch]$Capture
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
  throw "VCPKG_ROOT is unavailable"
}
if ([string]::IsNullOrWhiteSpace($env:VCPKG_BINARY_SOURCES)) {
  throw "VCPKG_BINARY_SOURCES is unavailable"
}
if ($env:VCPKG_BINARY_SOURCES -notmatch "nugetconfig") {
  throw "shared NuGet binary cache is not configured"
}
if ($env:VCPKG_NUGET_REPOSITORY -ne "https://github.com/qigao/vcpkg-cache") {
  throw "unexpected vcpkg cache repository: $env:VCPKG_NUGET_REPOSITORY"
}

$vcpkg = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
  throw "missing canonical vcpkg executable: $vcpkg"
}

$installRoot = Join-Path $env:GITHUB_WORKSPACE "vcpkg_installed"
# Keep the cache-only restored install tree in place. CMake uses the same
# VCPKG_INSTALLED_DIR, so configure/build performs no second dependency restore.

$args = @(
  "install",
  "--x-manifest-root=$env:GITHUB_WORKSPACE",
  "--x-install-root=$installRoot",
  "--triplet=x64-windows",
  "--only-binarycaching"
)
if ($Capture) {
  $args += "--x-feature=capture"
}

Write-Host "Verifying SaltsUtils Windows dependencies are fully restorable from qigao/vcpkg-cache"
Write-Host "Capture feature: $($Capture.IsPresent)"

& $vcpkg @args
if ($LASTEXITCODE -ne 0) {
  throw "shared vcpkg cache miss: Windows dependency restore is not binary-cache complete"
}

if (Test-Path -LiteralPath $installRoot) {
  Remove-Item -LiteralPath $installRoot -Recurse -Force
}
