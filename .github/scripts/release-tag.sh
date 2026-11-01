#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
  echo "usage: $0 <semantic-version>" >&2
  exit 2
fi

version="$1"
if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
  echo "release package version must be a canonical stable semantic version: $version" >&2
  exit 1
fi

major="${BASH_REMATCH[1]}"
minor="${BASH_REMATCH[2]}"
patch="${BASH_REMATCH[3]}"
if ((10#$patch > 999)); then
  echo "release patch version exceeds the three-digit tag field: $patch" >&2
  exit 1
fi

printf 'v%s.%s.%03d\n' "$major" "$minor" "$((10#$patch))"
