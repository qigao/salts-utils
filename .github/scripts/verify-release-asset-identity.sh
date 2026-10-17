#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 5 ]; then
  echo "usage: $0 <tag> <version> <package-path> <repository> <runner-temp>" >&2
  exit 2
fi

TAG="$1"
VERSION="$2"
package="$3"
GITHUB_REPOSITORY="$4"
RUNNER_TEMP="$5"
test -f "$package"
prepared_sha="$(sha256sum "$package" | awk '{print $1}')"
asset_name="SaltsUtils.Native.$VERSION.nupkg"

if gh release view "$TAG" --repo "$GITHUB_REPOSITORY" >/dev/null 2>&1; then
  asset_count="$(
    gh release view "$TAG" --repo "$GITHUB_REPOSITORY" --json assets |
      jq --arg asset_name "$asset_name" '[.assets[] | select(.name == $asset_name)] | length'
  )"
  if [ "$asset_count" != "1" ]; then
    echo "release $TAG must contain exactly one $asset_name asset; got $asset_count" >&2
    echo "bump the SaltsUtils project/package version before publishing a changed SDK" >&2
    exit 1
  fi

  existing_dir="$RUNNER_TEMP/existing-release"
  rm -rf "$existing_dir"
  mkdir -p "$existing_dir"
  gh release download "$TAG" --repo "$GITHUB_REPOSITORY" \
    --pattern "$asset_name" --dir "$existing_dir"
  existing_package="$existing_dir/$asset_name"
  test -f "$existing_package"
  existing_sha="$(sha256sum "$existing_package" | awk '{print $1}')"
  if [ "$existing_sha" != "$prepared_sha" ]; then
    echo "immutable release asset mismatch for $TAG/$asset_name" >&2
    echo "prepared sha256: $prepared_sha" >&2
    echo "existing sha256: $existing_sha" >&2
    echo "do not overwrite the existing release; bump the SaltsUtils project/package version" >&2
    exit 1
  fi
  echo "existing release asset matches prepared package: $prepared_sha"
fi
