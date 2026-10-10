#!/usr/bin/env python3
"""Select the highest immutable public Salts release in a target X.Y.Z cycle.

Read GitHub Releases JSON on stdin; reject internal SHA-qualified NuGet
candidates that float higher than rc.N but contain only a Linux SDK.
Fails closed if no complete public release is present.
"""
import json
import re
import sys

if len(sys.argv) != 2 or re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", sys.argv[1]) is None:
    raise SystemExit("Usage: select-salts-release.py X.Y.Z")
target = sys.argv[1]
candidate_pattern = re.compile(r"v" + re.escape(target) + r"-rc\.([1-9][0-9]*)")
releases = json.load(sys.stdin)
if not isinstance(releases, list):
    raise SystemExit("Expected GitHub Releases list")

eligible = []
for release in releases:
    if release.get("draft"):
        continue
    tag = release.get("tag_name", "")
    if tag == "v" + target and release.get("prerelease") is False:
        rank = (1, 0)
    elif (match := candidate_pattern.fullmatch(tag)) and release.get("prerelease") is True:
        rank = (0, int(match.group(1)))
    else:
        continue
    version = tag[1:]
    if not any(
        a.get("name") == "Salts.Native." + version + ".nupkg"
        and a.get("size", 0) > 0
        for a in release.get("assets", [])
    ):
        continue
    eligible.append((rank, version))

if not eligible:
    raise SystemExit("No published full Salts.Native " + target + " RC/stable release")
print(max(eligible)[1])
