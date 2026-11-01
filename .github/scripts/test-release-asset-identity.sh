#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
helper="$repo_root/.github/scripts/verify-release-asset-identity.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin" "$tmp/dist" "$tmp/runner"
package="$tmp/dist/SaltsUtils.Native.0.1.0.nupkg"
printf 'prepared package bytes\n' > "$package"
printf 'different package bytes\n' > "$tmp/different.nupkg"

cat > "$tmp/bin/gh" <<'MOCK_GH'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$1" == release && "$2" == view ]]; then
  if [[ "${MOCK_RELEASE_VIEW_ERROR:-false}" == true ]]; then
    echo "API unavailable" >&2
    exit 1
  fi
  if [[ "${MOCK_RELEASE_EXISTS:-false}" != true ]]; then
    echo "release not found" >&2
    exit 1
  fi
  if [[ " $* " == *" --json assets "* ]]; then
    case "${MOCK_ASSET_COUNT:-1}" in
      0) printf '{"assets":[]}\n' ;;
      1) printf '{"assets":[{"name":"SaltsUtils.Native.0.1.0.nupkg"}]}\n' ;;
      2) printf '{"assets":[{"name":"SaltsUtils.Native.0.1.0.nupkg"},{"name":"SaltsUtils.Native.0.1.0.nupkg"}]}\n' ;;
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
  bash "$helper" "v0.1.000" "0.1.0" "$package" "$GITHUB_REPOSITORY" "$tmp/runner"
}
expect_success() {
  expected_state="$1"
  shift
  if ! output="$(run_guard 2>/dev/null)"; then echo "expected success: $*" >&2; exit 1; fi
  if [[ "$output" != "$expected_state" ]]; then
    echo "expected $expected_state, got $output: $*" >&2
    exit 1
  fi
}
expect_failure() {
  if run_guard; then echo "expected failure: $*" >&2; exit 1; fi
}

# A not-yet-created release is allowed to proceed.
export MOCK_RELEASE_EXISTS=false
export MOCK_RELEASE_VIEW_ERROR=false
expect_success "RELEASE_EXISTS=false" "new release"

# A transient/authentication/API error must fail closed, not masquerade as a new release.
export MOCK_RELEASE_VIEW_ERROR=true
expect_failure "release lookup error"
export MOCK_RELEASE_VIEW_ERROR=false

# An existing immutable asset is idempotent only when its bytes match.
export MOCK_RELEASE_EXISTS=true
export MOCK_ASSET_COUNT=1
export MOCK_RELEASE_PACKAGE="$package"
expect_success "RELEASE_EXISTS=true" "identical existing asset"

# Missing, duplicate, and changed assets must fail before package publication.
export MOCK_ASSET_COUNT=0
expect_failure "missing asset"
export MOCK_ASSET_COUNT=2
expect_failure "duplicate asset"
export MOCK_ASSET_COUNT=1
export MOCK_RELEASE_PACKAGE="$tmp/different.nupkg"
expect_failure "SHA-256 mismatch"

echo "release asset identity cases passed: new, identical, missing, duplicate, mismatch"

tag_helper="$repo_root/.github/scripts/release-tag.sh"
test "$(bash "$tag_helper" 0.1.0)" = "v0.1.000"
test "$(bash "$tag_helper" 0.1.12)" = "v0.1.012"
if bash "$tag_helper" 0.1.1000; then
  echo "expected an oversized release patch to fail" >&2
  exit 1
fi
if bash "$tag_helper" 0.1.0-rc.1; then
  echo "expected a prerelease package version to fail stable tag mapping" >&2
  exit 1
fi
echo "release tag mapping cases passed: padded stable tags and invalid versions"
