#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
helper="$repo_root/.github/scripts/verify-release-asset-identity.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin" "$tmp/dist" "$tmp/runner"
package="$tmp/dist/SaltsUtils.Native.4.3.0.nupkg"
printf 'prepared package bytes\n' > "$package"
printf 'different package bytes\n' > "$tmp/different.nupkg"

cat > "$tmp/bin/gh" <<'MOCK_GH'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == release && "$2" == view ]]; then
  if [[ "${MOCK_RELEASE_EXISTS:-false}" != true ]]; then exit 1; fi
  if [[ " $* " == *" --json assets "* ]]; then
    case "${MOCK_ASSET_COUNT:-1}" in
      0) printf '{"assets":[]}\n' ;;
      1) printf '{"assets":[{"name":"SaltsUtils.Native.4.3.0.nupkg"}]}\n' ;;
      2) printf '{"assets":[{"name":"SaltsUtils.Native.4.3.0.nupkg"},{"name":"SaltsUtils.Native.4.3.0.nupkg"}]}\n' ;;
      *) exit 3 ;;
    esac
  fi
  exit 0
fi
if [[ "$1" == release && "$2" == download ]]; then
  pattern=""
  dir=""
  while (($#)); do
    case "$1" in
      --pattern) pattern="$2"; shift 2 ;;
      --dir) dir="$2"; shift 2 ;;
      *) shift ;;
    esac
  done
  test -n "$pattern" && test -n "$dir"
  cp "$MOCK_RELEASE_PACKAGE" "$dir/$pattern"
  exit 0
fi
echo "unexpected gh invocation: $*" >&2
exit 4
MOCK_GH
chmod +x "$tmp/bin/gh"

export PATH="$tmp/bin:$PATH"
export GITHUB_REPOSITORY="qigao/salts-utils"
export MOCK_RELEASE_PACKAGE="$package"

run_guard() {
  bash "$helper" "v4.3.0" "4.3.0" "$package" "$GITHUB_REPOSITORY" "$tmp/runner" >/dev/null 2>&1
}
expect_success() {
  if ! run_guard; then echo "expected success: $*" >&2; exit 1; fi
}
expect_failure() {
  if run_guard; then echo "expected failure: $*" >&2; exit 1; fi
}

# A not-yet-created release is allowed to proceed.
export MOCK_RELEASE_EXISTS=false
expect_success "new release"

# An existing immutable asset is idempotent only when its bytes match.
export MOCK_RELEASE_EXISTS=true
export MOCK_ASSET_COUNT=1
export MOCK_RELEASE_PACKAGE="$package"
expect_success "identical existing asset"

# Missing, duplicate, and changed assets must fail before package publication.
export MOCK_ASSET_COUNT=0
expect_failure "missing asset"
export MOCK_ASSET_COUNT=2
expect_failure "duplicate asset"
export MOCK_ASSET_COUNT=1
export MOCK_RELEASE_PACKAGE="$tmp/different.nupkg"
expect_failure "SHA-256 mismatch"

echo "release asset identity cases passed: new, identical, missing, duplicate, mismatch"
