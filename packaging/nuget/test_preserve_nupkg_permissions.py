#!/usr/bin/env python3

from __future__ import annotations

import stat
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HOST_EXECUTABLES = (
    "sdk/linux-x64/bin/salts-idlc",
    "sdk/linux-arm64/bin/salts-idlc",
    "sdk/macos-arm64/bin/salts-idlc",
)


def mode(info: zipfile.ZipInfo) -> int:
    return (info.external_attr >> 16) & 0o7777


def entry(name: str, permissions: int) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name)
    info.create_system = 3
    info.external_attr = ((stat.S_IFREG | permissions) << 16)
    return info


def main() -> int:
    script = Path(__file__).with_name("preserve_nupkg_permissions.py")
    with tempfile.TemporaryDirectory() as temp_dir:
        package = Path(temp_dir) / "fixture.nupkg"
        payloads = {
            HOST_EXECUTABLES[0]: b"linux-x64",
            HOST_EXECUTABLES[1]: b"linux-arm64",
            HOST_EXECUTABLES[2]: b"macos-arm64",
            "sdk/linux-x64/include/control.h": b"control",
        }

        with zipfile.ZipFile(package, "w") as archive:
            for name in HOST_EXECUTABLES:
                archive.writestr(entry(name, 0o644), payloads[name])
            archive.writestr(
                entry("sdk/linux-x64/include/control.h", 0o640),
                payloads["sdk/linux-x64/include/control.h"],
            )

        subprocess.run([sys.executable, str(script), str(package)], check=True)

        with zipfile.ZipFile(package, "r") as archive:
            if set(archive.namelist()) != set(payloads):
                raise SystemExit("permission rewrite changed package entry set")
            for name, expected in payloads.items():
                if archive.read(name) != expected:
                    raise SystemExit(f"{name}: payload changed")
            for name in HOST_EXECUTABLES:
                if mode(archive.getinfo(name)) != 0o755:
                    raise SystemExit(
                        f"{name}: mode {oct(mode(archive.getinfo(name)))} != 0o755"
                    )
            control = archive.getinfo("sdk/linux-x64/include/control.h")
            if mode(control) != 0o640:
                raise SystemExit(
                    f"control entry mode changed: {oct(mode(control))}"
                )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
