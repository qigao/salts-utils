param([string]$Preset, [switch]$HostSdk)
$ErrorActionPreference = 'Stop'
if ($env:CMETA_REF -cnotmatch '^[0-9a-f]{40}$') { throw 'CMETA_REF must select an immutable commit' }
$root = [IO.Path]::GetFullPath('build/upstream/cmeta')
$actual = git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual -cne $env:CMETA_REF) { throw 'Unexpected CMeta SDK source' }
if ($HostSdk) {
  $env:SALTS_BASE_ROOT = $env:HOST_SALTS_BASE_ROOT
  $env:VCPKG_INSTALLED_DIR = $env:HOST_VCPKG_INSTALLED_DIR
  $env:VCPKG_TARGET_TRIPLET = 'x64-linux'
}
$prefix = "$root/build/$Preset/stage"
Push-Location $root
try {
  if ($IsWindows) {
    & ./cmake/WithVs.cmd cmake --preset $Preset -DBUILD_TESTS=OFF "-DCMAKE_INSTALL_PREFIX=$prefix"
  } else {
    cmake --preset $Preset -DBUILD_TESTS=OFF "-DCMAKE_INSTALL_PREFIX=$prefix"
  }
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  if ($IsWindows) { & ./cmake/WithVs.cmd cmake --build --preset $Preset }
  else { cmake --build --preset $Preset }
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  cmake --install "build/$Preset"
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }
$name = if ($HostSdk) { 'HOST_CMETA_ROOT' } else { 'CMETA_ROOT' }
"$name=$($prefix.Replace('\', '/'))" >> $env:GITHUB_ENV
