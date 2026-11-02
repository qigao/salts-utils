#!/usr/bin/env python3
"""Pack the installed native SDK offline, preserving Unix executable modes.

The payload is an OPC ZIP with a nuspec manifest, without managed build tools.
https://learn.microsoft.com/nuget/reference/nuspec
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile
import xml.etree.ElementTree as ET
import zipfile

from github_packages import VERSION, archive_members, package_metadata, read_manifest

PACKAGE = "SaltsUtils.Native"
RIDS = {"linux-x64", "linux-arm64", "windows-x64", "macos-arm64", "android-arm64"}


def pack(root, destination, version, commit):
    root, destination = Path(root), Path(destination)
    if not VERSION.fullmatch(version) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("an exact package version and source commit are required")
    sdk = root / "sdk"
    if {path.name for path in sdk.iterdir()} != RIDS:
        raise ValueError("release requires exactly five SDK RIDs")
    for rid in RIDS:
        prefix = sdk / rid
        manifest = read_manifest(prefix / "salts-utils-sdk-manifest.txt")
        if any(manifest.get(key) != value for key, value in (
                ("version", version), ("commit", commit), ("rid", rid), ("profile", "release"))):
            raise ValueError(f"{rid}: SDK provenance mismatch")
        for name in ("lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake", "lib/cmake/SaltsUtils/IDL.cmake",
                     "include/data_bind_binding_plan.h", "include/data_bind_native.h"):
            if not (prefix / name).is_file():
                raise ValueError(f"{rid}: incomplete SDK payload: {name}")
        if not rid.startswith("android-"):
            executable = prefix / "bin" / ("salts-idlc.exe" if rid.startswith("windows-") else "salts-idlc")
            if not executable.is_file():
                raise ValueError(f"{rid}: missing host IDL compiler")
    files = []
    extensions = {"rels", "nuspec"}
    for path in sorted(sdk.rglob("*")):
        if path.is_symlink():
            raise ValueError("SDK package must contain regular files, not symlinks")
        if path.is_file():
            files.append(path)
            if path.suffix:
                extensions.add(path.suffix[1:])
    package = ET.Element("package", xmlns="http://schemas.microsoft.com/packaging/2010/07/nuspec.xsd")
    metadata = ET.SubElement(package, "metadata")
    for name, value in (("id", PACKAGE), ("version", version), ("authors", "qigao"),
                        ("description", "SaltsUtils native C11 SDK"), ("tags", "native C11 SDK")):
        ET.SubElement(metadata, name).text = value
    ET.SubElement(metadata, "license", type="expression").text = "Apache-2.0"
    ET.SubElement(metadata, "repository", type="git", url="https://github.com/qigao/salts-utils", commit=commit)
    content_types = ET.Element("Types", xmlns="http://schemas.openxmlformats.org/package/2006/content-types")
    for extension in sorted(extensions):
        ET.SubElement(content_types, "Default", Extension=extension,
                      ContentType=("application/vnd.openxmlformats-package.relationships+xml"
                                   if extension == "rels" else "application/octet"))
    for path in files:
        if not path.suffix:
            ET.SubElement(content_types, "Override", PartName="/" + path.relative_to(root).as_posix(),
                          ContentType="application/octet")
    ET.SubElement(content_types, "Override", PartName=f"/{PACKAGE}.nuspec", ContentType="application/octet")
    relationships = ET.Element("Relationships", xmlns="http://schemas.openxmlformats.org/package/2006/relationships")
    ET.SubElement(relationships, "Relationship", Id="manifest", Target=f"/{PACKAGE}.nuspec",
                  Type="http://schemas.microsoft.com/packaging/2010/07/manifest")
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, staged = tempfile.mkstemp(prefix="native-package-", suffix=".nupkg", dir=destination.parent)
    os.close(descriptor)
    staged = Path(staged)
    try:
        with zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, element in ((f"{PACKAGE}.nuspec", package), ("[Content_Types].xml", content_types),
                                  ("_rels/.rels", relationships)):
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                archive.writestr(info, ET.tostring(element, encoding="utf-8", xml_declaration=True))
            for path in files:
                name = path.relative_to(root).as_posix()
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.compress_type = zipfile.ZIP_DEFLATED
                mode = path.stat().st_mode
                # GitHub artifact ZIPs normalize file modes. Restore the known
                # Unix host-tool contract before sealing the native package.
                if name in {f"sdk/{rid}/bin/salts-idlc" for rid in RIDS if rid != "windows-x64" and not rid.startswith("android-")}:
                    mode |= 0o111
                info.external_attr = mode << 16
                with path.open("rb") as source, archive.open(info, "w") as target:
                    shutil.copyfileobj(source, target)
        with zipfile.ZipFile(staged) as archive:
            list(archive_members(archive))
            package_metadata(archive, PACKAGE, version)
        staged.replace(destination)
    finally:
        staged.unlink(missing_ok=True)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    print(pack(args.root, args.output, args.version, args.commit))


if __name__ == "__main__":
    main()
