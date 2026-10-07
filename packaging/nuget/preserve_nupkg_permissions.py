#!/usr/bin/env python3

from __future__ import annotations

import os
import stat
import sys
import tempfile
import zipfile
from pathlib import Path

HOST_EXECUTABLES = (
    "sdk/linux-x64/bin/salts-idlc",
    "sdk/linux-arm64/bin/salts-idlc",
    "sdk/macos-arm64/bin/salts-idlc",
)


def unix_mode(info: zipfile.ZipInfo) -> int:
    return (info.external_attr >> 16) & 0o7777


def executable_info(source: zipfile.ZipInfo) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(source.filename, source.date_time)
    info.compress_type = source.compress_type
    info.comment = source.comment
    info.extra = source.extra
    info.internal_attr = source.internal_attr
    info.create_system = 3
    info.create_version = source.create_version
    info.extract_version = source.extract_version
    info.flag_bits = source.flag_bits
    info.volume = source.volume
    info.external_attr = ((stat.S_IFREG | 0o755) << 16)
    return info


def rewrite(package: Path) -> None:
    if not package.is_file():
        raise SystemExit(f"missing nupkg: {package}")

    with zipfile.ZipFile(package, "r") as source:
        names = set(source.namelist())
        missing = [name for name in HOST_EXECUTABLES if name not in names]
        if missing:
            raise SystemExit(f"missing packaged host compiler(s): {missing}")

        fd, temp_name = tempfile.mkstemp(
            prefix=package.name + ".",
            suffix=".tmp",
            dir=package.parent,
        )
        os.close(fd)
        temp = Path(temp_name)

        try:
            with zipfile.ZipFile(temp, "w", allowZip64=True) as target:
                target.comment = source.comment
                for item in source.infolist():
                    data = source.read(item.filename)
                    out = (
                        executable_info(item)
                        if item.filename in HOST_EXECUTABLES
                        else item
                    )
                    target.writestr(out, data)

            with zipfile.ZipFile(temp, "r") as checked:
                for name in HOST_EXECUTABLES:
                    mode = unix_mode(checked.getinfo(name))
                    if mode & 0o111 != 0o111:
                        raise SystemExit(
                            f"{name}: executable mode not preserved: {oct(mode)}"
                        )

            os.replace(temp, package)
        finally:
            if temp.exists():
                temp.unlink()


def main() -> int:
    if len(sys.argv) != 2:
        print(
            "usage: preserve_nupkg_permissions.py <SaltsUtils.Native.nupkg>",
            file=sys.stderr,
        )
        return 2

    rewrite(Path(sys.argv[1]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
