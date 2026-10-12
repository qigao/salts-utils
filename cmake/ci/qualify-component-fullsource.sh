#!/usr/bin/env bash
# P3.4a first-party Salts source sanitizer qualification (#637).
# This is intentionally an independent build, not a substitute for the
# SaltsUtils IDL consumers' restored Salts.Native SDK.
set -euo pipefail

: "${GITHUB_WORKSPACE:?}"
: "${VCPKG_CACHE_REPOSITORY_ROOT:?}"
: "${QIGAO_TARGET_TRIPLET:?}"
: "${QIGAO_HOST_TRIPLET:?}"
source_root="$GITHUB_WORKSPACE/third_party/salts-fullsource-exact"
expected_sha="540e1da7c2448f15f04aeaaee1106f73a2fbcc7e"
test "$(git -C "$source_root" rev-parse HEAD)" = "$expected_sha"
test -f "$source_root/component-plugin/src/component_plugin.c"
test -f "$source_root/cnet/src/cnet_tls.c"
test -f "$source_root/component-runtime/src/service_host.c"

vcpkg_install="$GITHUB_WORKSPACE/build/ci-component-idl-fullsource-vcpkg"
evidence_root="$GITHUB_WORKSPACE/build/ci-component-idl-fullsource-evidence"
mkdir -p "$evidence_root"
printf '%s\n' "$expected_sha" > "$evidence_root/salts-source-sha.txt"
printf '%s\n' "${GITHUB_SHA:-unknown}" > "$evidence_root/salts-utils-github-sha.txt"

for mode in asan-ubsan tsan; do
  build_dir="$GITHUB_WORKSPACE/build/ci-component-idl-fullsource-$mode"
  mkdir -p "$build_dir/.cmake/api/v1/query"
  : > "$build_dir/.cmake/api/v1/query/codemodel-v2"
  flags=(
    -DENABLE_SANITIZER_ADDRESS=OFF
    -DENABLE_SANITIZER_UNDEFINED=OFF
    -DENABLE_SANITIZER_THREAD=OFF
  )
  if [ "$mode" = asan-ubsan ]; then
    flags=(
      -DENABLE_SANITIZER_ADDRESS=ON
      -DENABLE_SANITIZER_UNDEFINED=ON
      -DENABLE_SANITIZER_THREAD=OFF
    )
  else
    flags=(
      -DENABLE_SANITIZER_ADDRESS=OFF
      -DENABLE_SANITIZER_UNDEFINED=OFF
      -DENABLE_SANITIZER_THREAD=ON
    )
  fi
  cmake -S "$source_root" -B "$build_dir" -G Ninja \
    "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_CACHE_REPOSITORY_ROOT/cmake/QigaoVcpkgToolchain.cmake" \
    "-DVCPKG_INSTALLED_DIR=$vcpkg_install" \
    "-DVCPKG_TARGET_TRIPLET=$QIGAO_TARGET_TRIPLET" \
    "-DVCPKG_HOST_TRIPLET=$QIGAO_HOST_TRIPLET" \
    -DVCPKG_MANIFEST_MODE=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_SUPPRESS_REGENERATION=ON \
    -DBUILD_TESTS=ON -DENABLE_TESTS=ON \
    -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF \
    -DCMETA_BUILD_BENCHMARKS=OFF -DCMETA_BUILD_NATIVE_THUNKS=OFF \
    -DCNET_BUILD_BENCHMARKS=OFF -DNATIVE_IO_BUILD_BENCHMARKS=OFF \
    -DSALTS_ENABLE_COMPONENT_RUNTIME_CANDIDATE=ON \
    "-DCMAKE_INSTALL_PREFIX=$build_dir/never-installed" \
    "${flags[@]}"
  # The unmodified Salts targets define the original Plugin/ComponentPlugin,
  # CMeta, Platform, CNet and candidate ServiceHost ownership graph.
  cmake --build "$build_dir" --parallel 2 --target \
    cnet \
    cmeta_native_service_host_candidate_generation_test \
    cmeta_native_service_host_candidate_publication_test \
    cmeta_native_service_host_candidate_concurrency_test \
    cmeta_native_component_plugin_generation_test \
    cmeta_native_component_plugin_publication_test \
    cmeta_native_component_plugin_concurrency_test \
    cmeta_native_component_plugin_act_native_io_test \
    cmeta_native_component_plugin_dso_test

  python3 "$GITHUB_WORKSPACE/cmake/ci/verify-component-fullsource.py" \
    --salts-root "$source_root" --build-dir "$build_dir" --mode "$mode" \
    --output "$evidence_root/source-link-$mode.json"
  cp "$build_dir/compile_commands.json" "$evidence_root/compile_commands-$mode.json"

  export LD_LIBRARY_PATH="$build_dir/bin:$vcpkg_install/$QIGAO_TARGET_TRIPLET/lib:${LD_LIBRARY_PATH:-}"
  ctest --test-dir "$build_dir" --no-tests=error \
    --output-on-failure --timeout 120 --repeat until-fail:5 \
    --output-log "$evidence_root/ctest-$mode.log" \
    -R '^cmeta_native_(service_host_candidate_(generation|publication|concurrency)|component_plugin_(generation|publication|concurrency|act_native_io|dso))_test$'
done
