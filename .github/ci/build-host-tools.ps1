$ErrorActionPreference = 'Stop'
# Android code generation runs this explicitly built native compiler. These
# process-local roots cannot replace the target roots in subsequent CI steps.
$env:SALTS_BASE_ROOT = $env:HOST_SALTS_BASE_ROOT
$env:CMETA_ROOT = $env:HOST_CMETA_ROOT
$env:VCPKG_INSTALLED_DIR = $env:HOST_VCPKG_INSTALLED_DIR
$env:VCPKG_TARGET_TRIPLET = 'x64-linux'
$preset = 'llvm-native-release-user'
cmake --preset $preset -DBUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset $preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --install "build/$preset"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
"HOST_UTILS_ROOT=$([IO.Path]::GetFullPath("build/$preset/stage"))" >> $env:GITHUB_ENV
