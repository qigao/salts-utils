#!/usr/bin/env python3
"""Exercise the real NuGet pack/rewrite/extract path with synthetic SDK payloads.

This is intentionally not a substitute for the six-RID release build. It gives
packaging-only PRs a fast, offline contract test while release preparation still
requires all real SDK artifacts and full native qualification.
"""
from __future__ import annotations

import os
import stat
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

RIDS = (
    "linux-x64",
    "linux-arm64",
    "windows-x64",
    "macos-arm64",
    "android-arm64-v8a",
    "ios-arm64",
)
VERSION = "4.3.0"
SOURCE_SHA = "0123456789abcdef0123456789abcdef01234567"
SALTS_VERSION = "2.3.0-rc.17"
CORE_FILES = (
    "lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake",
    "lib/cmake/SaltsUtils/IDL.cmake",
    "include/data_bind_native.h",
    "include/data_bind_cmeta_adapter_plan.h",
    "include/data_bind_plugin_catalog.h",
)
HOST_TOOLS = {
    "linux-x64": "bin/salts-idlc",
    "linux-arm64": "bin/salts-idlc",
    "windows-x64": "bin/salts-idlc.exe",
    "macos-arm64": "bin/salts-idlc",
}
UNIX_HOST_TOOLS = {
    "sdk/linux-x64/bin/salts-idlc",
    "sdk/linux-arm64/bin/salts-idlc",
    "sdk/macos-arm64/bin/salts-idlc",
}


def write(path: Path, payload: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    path.chmod(mode)


def run(command: list[str], cwd: Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def prepare_fixture(root: Path) -> dict[str, bytes]:
    expected: dict[str, bytes] = {}
    for rid in RIDS:
        prefix = root / "sdk" / rid
        for relative in CORE_FILES:
            payload = f"{rid}:{relative}\n".encode()
            write(prefix / relative, payload)
            expected[f"sdk/{rid}/{relative}"] = payload

        manifest = (
            f"version={VERSION}\n"
            f"commit={SOURCE_SHA}\n"
            f"rid={rid}\n"
            "profile=release\n"
            f"salts_version={SALTS_VERSION}\n"
        ).encode()
        write(prefix / "salts-utils-sdk-manifest.txt", manifest)
        expected[f"sdk/{rid}/salts-utils-sdk-manifest.txt"] = manifest

        tool = HOST_TOOLS.get(rid)
        if tool:
            payload = f"synthetic-host-tool:{rid}\n".encode()
            mode = 0o755 if rid != "windows-x64" else 0o644
            write(prefix / tool, payload, mode)
            expected[f"sdk/{rid}/{tool}"] = payload

    # Linux arm64 is the focused DataBind profile and must not claim capture.
    for path in (
        root / "sdk/linux-arm64/include/salts_capture.h",
        root / "sdk/linux-arm64/lib/libsalts_capture.so",
    ):
        if path.exists():
            raise AssertionError(f"fixture unexpectedly contains {path}")

    return expected


def verify_package(package: Path, expected: dict[str, bytes], extract: Path) -> None:
    with zipfile.ZipFile(package) as archive:
        names_list = archive.namelist()
        names = set(names_list)
        if len(names_list) != len(names):
            raise AssertionError("package contains duplicate ZIP entries")

        manifest_names = {
            name for name in names if name.endswith("/salts-utils-sdk-manifest.txt")
        }
        expected_manifests = {
            f"sdk/{rid}/salts-utils-sdk-manifest.txt" for rid in RIDS
        }
        if manifest_names != expected_manifests:
            raise AssertionError(
                f"six-RID manifest set mismatch: {manifest_names ^ expected_manifests}"
            )

        for name, payload in expected.items():
            if name not in names:
                raise AssertionError(f"missing package payload: {name}")
            if archive.read(name) != payload:
                raise AssertionError(f"payload identity changed: {name}")

        for rid in RIDS:
            manifest = f"sdk/{rid}/salts-utils-sdk-manifest.txt"
            values = dict(
                line.split("=", 1)
                for line in archive.read(manifest).decode("utf-8-sig").splitlines()
                if "=" in line
            )
            for key, wanted in (
                ("version", VERSION),
                ("commit", SOURCE_SHA),
                ("rid", rid),
                ("profile", "release"),
                ("salts_version", SALTS_VERSION),
            ):
                if values.get(key) != wanted:
                    raise AssertionError(
                        f"{manifest}: {key}={values.get(key)!r}, expected {wanted!r}"
                    )

        for name in UNIX_HOST_TOOLS:
            mode = (archive.getinfo(name).external_attr >> 16) & 0o7777
            if mode != 0o755:
                raise AssertionError(f"{name}: mode {oct(mode)} != 0o755")

        if "sdk/ios-arm64/bin/salts-idlc" in names:
            raise AssertionError("target iOS SDK must not package host salts-idlc")
        if "README.md" not in names:
            raise AssertionError("package is missing README.md")

        nuspecs = [name for name in names if name.endswith(".nuspec")]
        if len(nuspecs) != 1:
            raise AssertionError(f"expected one nuspec, found {nuspecs}")
        root = ET.fromstring(archive.read(nuspecs[0]))
        identity = {
            node.tag.split("}")[-1]: node.text
            for node in root.iter()
            if node.tag.split("}")[-1] in {"id", "version"}
        }
        if identity.get("id") != "SaltsUtils.Native":
            raise AssertionError(f"wrong NuGet package ID: {identity}")
        if identity.get("version") != VERSION:
            raise AssertionError(f"wrong NuGet version: {identity}")
        if any(node.tag.split("}")[-1] == "dependencies" for node in root.iter()):
            raise AssertionError("SaltsUtils.Native must not publish dependencies")

    extract.mkdir(parents=True, exist_ok=True)
    run(["unzip", "-q", str(package), "-d", str(extract)], package.parent)
    for name, payload in expected.items():
        extracted = extract / name
        if not extracted.is_file() or extracted.read_bytes() != payload:
            raise AssertionError(f"extracted payload mismatch: {name}")
    if os.name == "posix":
        for name in UNIX_HOST_TOOLS:
            mode = stat.S_IMODE((extract / name).stat().st_mode)
            if mode != 0o755:
                raise AssertionError(f"extracted {name}: mode {oct(mode)} != 0o755")


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    project = repo_root / "packaging/nuget/SaltsUtils.Native.csproj"
    permission_rewriter = repo_root / "packaging/nuget/preserve_nupkg_permissions.py"

    with tempfile.TemporaryDirectory(prefix="salts-utils-nupkg-contract-") as temp:
        root = Path(temp)
        package_root = root / "fixture-package"
        expected = prepare_fixture(package_root)
        config = root / "nuget.config"
        config.write_text(
            '<?xml version="1.0" encoding="utf-8"?>'
            "<configuration><packageSources><clear /></packageSources></configuration>\n",
            encoding="utf-8",
        )
        packages = root / "restore-packages"
        output = root / "dist"
        output.mkdir()
        run(
            [
                "dotnet", "restore", str(project),
                "--configfile", str(config),
                "--packages", str(packages),
            ],
            repo_root,
        )
        run(
            [
                "dotnet", "pack", str(project),
                "--no-restore",
                "--configuration", "Release",
                "--output", str(output),
                f"-p:PackageVersion={VERSION}",
                f"-p:PackageRoot={package_root}",
                f"-p:RestorePackagesPath={packages}",
            ],
            repo_root,
        )
        package = output / f"SaltsUtils.Native.{VERSION}.nupkg"
        if not package.is_file():
            raise AssertionError(f"dotnet pack did not create {package}")
        run(["python3", str(permission_rewriter), str(package)], repo_root)
        verify_package(package, expected, root / "extracted")
        print("PASS: real NuGet pack, six-RID manifests, payload identity, no dependencies, executable modes, extraction")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
