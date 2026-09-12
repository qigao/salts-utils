#!/usr/bin/env bash
# Ubuntu's re2c package predates the Unicode data required by FindTools.cmake.
set -euo pipefail

: "${RUNNER_TEMP:?GitHub Actions RUNNER_TEMP is required}"
: "${GITHUB_PATH:?GitHub Actions GITHUB_PATH is required}"

readonly version=4.6
# Official release asset: https://github.com/skvadrik/re2c/releases/tag/4.6
readonly archive_sha256=75bf2696445e831d0d44e0d9f2909eeffc18c09757f222b2fb025f7e59fe130b
work_dir="$(mktemp -d "$RUNNER_TEMP/re2c-${version}.XXXXXX")"
readonly work_dir
readonly prefix="$work_dir/install"
readonly archive="$work_dir/re2c-${version}.tar.xz"

curl --fail --location --silent --show-error \
  --proto '=https' --proto-redir '=https' \
  "https://github.com/skvadrik/re2c/releases/download/${version}/re2c-${version}.tar.xz" \
  --output "$archive"
printf '%s  %s\n' "$archive_sha256" "$archive" | sha256sum --check --strict -
tar -xJf "$archive" -C "$work_dir"

# Build only the C/C++ generator, using the release's bootstrap sources.
backend_options=()
for backend in RE2D RE2GO RE2HS RE2JAVA RE2JS RE2OCAML RE2PY RE2RUST RE2SWIFT RE2V RE2ZIG; do
  backend_options+=("-DRE2C_BUILD_${backend}=OFF")
done
cmake -S "$work_dir/re2c-${version}" -B "$work_dir/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DRE2C_STDLIB_DIR="$prefix/share/re2c/stdlib" \
  -DRE2C_BUILD_TESTS=OFF \
  -DRE2C_REBUILD_LEXERS=OFF \
  -DRE2C_REBUILD_PARSERS=OFF \
  -DRE2C_REBUILD_DOCS=OFF \
  "${backend_options[@]}"
cmake --build "$work_dir/build" --parallel 2
cmake --install "$work_dir/build"

actual_version="$("$prefix/bin/re2c" --version)"
if [[ "$actual_version" != "re2c $version" ]]; then
  printf 'Expected re2c %s, got: %s\n' "$version" "$actual_version" >&2
  exit 1
fi
printf '%s\n' "$actual_version"

# Keep these aligned with FindTools.cmake; a binary-only install is insufficient.
printf '%s  %s\n' \
  56562ef44b0adef04258f59f8b6cd83d3936f7438f0d8162b5c0ccf30c05cf9f \
  "$prefix/share/re2c/stdlib/unicode_categories.re" \
  0980dcc0aad753cb3011f0e5700d056a8198df039492e6813c4f88993ea44f63 \
  "$prefix/share/re2c/stdlib/unicode_properties.re" \
  | sha256sum --check --strict -

# Publish only a fully validated installation to subsequent Salts/Utils steps.
printf '%s\n' "$prefix/bin" >> "$GITHUB_PATH"
