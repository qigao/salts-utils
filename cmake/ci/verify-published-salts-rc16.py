#!/usr/bin/env python3
"""Fail-closed preflight for official Salts.Native rc.16 across target and host SDKs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from xml.etree import ElementTree as ET
import zipfile

VERSION = "2.3.0-rc.16"
SOURCE = "6cf093dc053db98bb21b1788eb2fc1ada4df195e"
SHA256 = "0b8592cb0b27e91fb5a3ee2c5a888ca53c7593cf9eb8cd5adfbc38fac43659b7"
RIDS = ("linux-x64", "linux-arm64", "windows-x64", "macos-arm64",
        "android-arm64-v8a", "ios-arm64")
HOSTS = {"linux-x64": "linux-x64", "linux-arm64": "linux-arm64",
         "windows-x64": "windows-x64", "macos-arm64": "macos-arm64",
         "android-arm64-v8a": "linux-x64", "ios-arm64": "macos-arm64"}

# Salts 2.3.0-rc.16 canonical package format uses os/arch/abi for most
# platforms; only some cross builds include an explicit rid manifest field.
PROFILES = {
    "linux-x64": {"os": "linux", "arch": "x64"},
    "linux-arm64": {"os": "linux", "arch": "arm64", "rid": "linux-arm64"},
    "windows-x64": {"os": "windows", "arch": "x64"},
    "macos-arm64": {"os": "macos", "arch": "ARM64"},
    "android-arm64-v8a": {"os": "android", "abi": "arm64-v8a", "api": "android-24"},
    "ios-arm64": {"os": "ios", "rid": "ios-arm64"},
}


def verify_release(release, commit):
    tag = "v" + VERSION
    if (release.get("tag_name") != tag or release.get("draft") is not False
            or release.get("prerelease") is not True
            or commit.get("sha") != SOURCE):
        raise ValueError("Salts release tag/source/channel mismatch")
    expected = "Salts.Native." + VERSION + ".nupkg"
    assets = [a for a in release.get("assets", []) if a.get("name") == expected]
    if len(assets) != 1:
        raise ValueError("Expected exactly one official NuGet Release asset")
    asset = assets[0]
    if (asset.get("digest") != "sha256:" + SHA256
            or asset.get("state") != "uploaded"
            or not isinstance(asset.get("size"), int) or asset["size"] <= 0):
        raise ValueError("Published Salts Release asset digest/state mismatch")


def verify_archive(package, target, host, expected_digest=SHA256):
    if target not in HOSTS or HOSTS[target] != host:
        raise ValueError("Invalid target/host RID pair")
    path = Path(package)
    candidates = [x for x in path.glob("*.nupkg")
                  if x.name.lower() == ("Salts.Native." + VERSION + ".nupkg").lower()]
    if len(candidates) != 1:
        raise ValueError("Missing exact restored Salts.Native NuGet archive")
    actual = hashlib.sha256(candidates[0].read_bytes()).hexdigest()
    if actual != expected_digest:
        raise ValueError("Restored NuGet archive digest differs from Release asset")
    with zipfile.ZipFile(candidates[0]) as z:
        names = set(z.namelist())
        expected = {f"sdk/{rid}/salts-sdk-manifest.txt" for rid in RIDS}
        found = {n for n in names if n.endswith("/salts-sdk-manifest.txt")}
        if found != expected:
            raise ValueError("Incomplete six-RID Salts.Native release archive")
        specs = [n for n in names if n.endswith(".nuspec")]
        if len(specs) != 1:
            raise ValueError("Missing or duplicate NuGet specification")
        meta = {el.tag.split("}")[-1]: el.text
                for el in ET.fromstring(z.read(specs[0])).iter()
                if el.tag.split("}")[-1] in {"id", "version"}}
        if (meta.get("id"), meta.get("version")) != ("Salts.Native", VERSION):
            raise ValueError("Wrong published NuGet identity")
        for rid in RIDS:
            prefix = f"sdk/{rid}/"
            fields = dict(line.split("=", 1) for line in
                          z.read(prefix + "salts-sdk-manifest.txt")
                          .decode("utf-8-sig").splitlines() if "=" in line)
            required = {"version": VERSION, "commit": SOURCE,
                        "profile": "release", **PROFILES[rid]}
            for key, want in required.items():
                if fields.get(key) != want:
                    raise ValueError(f"{rid}: {key} provenance mismatch")
            if "rid" not in PROFILES[rid] and fields.get("rid") not in (None, rid):
                raise ValueError(f"{rid}: unexpected mismatched RID")
            header = z.read(prefix + "include/salts/thread.h")
            if rid == "windows-x64":
                if b"volatile int state;" not in header or b"SALTS_ONCE_INIT {0}" not in header:
                    raise ValueError("Wrong Windows native once ABI")
            elif b"pthread_once_t native;" not in header:
                raise ValueError(f"{rid}: wrong POSIX pthread_once ABI")
    for rid in {target, host}:
        root = path / "sdk" / rid
        if not (root / "lib/cmake/Salts/SaltsConfig.cmake").is_file():
            raise ValueError(f"{rid}: missing restored installed Salts CMake SDK")
        if not (root / "include/salts/thread.h").is_file():
            raise ValueError(f"{rid}: missing restored installed once ABI header")
    return actual


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--package", required=True)
    p.add_argument("--target-rid", required=True)
    p.add_argument("--host-rid", required=True)
    args = p.parse_args()
    if any(__import__("os").environ.get(name) for name in
           ("SALTS_CANDIDATE_ROOT", "SALTS_UTILS_CANDIDATE_ROOT")):
        raise ValueError("Draft source package may not be mixed with official rc.16")
    release = json.loads(subprocess.check_output(
        ["gh", "api", "repos/qigao/salts/releases/tags/v" + VERSION],
        text=True, encoding="utf-8"))
    commit = json.loads(subprocess.check_output(
        ["gh", "api", "repos/qigao/salts/commits/v" + VERSION],
        text=True, encoding="utf-8"))
    verify_release(release, commit)
    digest = verify_archive(args.package, args.target_rid, args.host_rid)
    print("QUALIFIED official Salts.Native", VERSION, SOURCE, digest,
          args.target_rid, args.host_rid)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError,
            zipfile.BadZipFile, KeyError) as e:
        print("Published Salts rc.16 SDK rejected:", e, file=sys.stderr)
        sys.exit(1)
