#!/usr/bin/env python3
"""GitHub Packages restore with curl only; no NuGet client, .NET or Mono."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import socketserver
import stat
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET
import zipfile

FEED = "https://nuget.pkg.github.com/qigao/index.json"
SALTS_NATIVE_VERSION = "2.3.0-rc.17"
IDENTIFIER = re.compile(r"^[A-Za-z0-9_.-]+$")
VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?(?:-([0-9A-Za-z.-]+))?$")
RID = re.compile(r"^(linux-(?:x64|arm64)|windows-x64|macos-arm64|android-arm64-v8a|ios-arm64)$")


class PackageMissing(Exception):
    pass


def version_key(version):
    match = VERSION.fullmatch(version)
    if not match:
        raise ValueError("unsupported package version: " + version)
    major, minor, patch, revision, prerelease = match.groups()
    suffix = tuple((0, int(part)) if part.isdigit() else (1, part.lower())
                   for part in (prerelease or "").split("."))
    return (int(major), int(minor), int(patch), int(revision or 0),
            prerelease is None, suffix)


class Curl:
    def __init__(self, token=None, feed=FEED):
        self.token = token if token is not None else os.environ.get("GITHUB_TOKEN", "")
        self.feed = feed
        if not self.token or any(c in self.token for c in '\r\n"\\'):
            raise ValueError("GITHUB_TOKEN with read:packages permission is required")

    def download(self, url, destination):
        parsed = urlsplit(url)
        origin = urlsplit(self.feed)
        if (parsed.scheme, parsed.netloc) != (origin.scheme, origin.netloc):
            raise ValueError("package URL does not belong to configured GitHub Packages feed")
        if parsed.scheme != "https" and parsed.hostname != "127.0.0.1":
            raise ValueError("package feed requires HTTPS")
        destination = Path(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        config = f'user = "qigao:{self.token}"\n'
        protocols = "=http,https" if parsed.hostname == "127.0.0.1" else "=https"
        result = subprocess.run([
            "curl", "--disable", "--config", "-", "--fail", "--silent", "--show-error",
            "--location", "--proto", protocols, "--proto-redir", protocols,
            "--retry", "3", "--connect-timeout", "20", "--max-time", "600",
            "--output", str(destination), url,
        ], input=config, text=True, capture_output=True)
        if result.returncode == 22:
            destination.unlink(missing_ok=True)
            raise PackageMissing("package request failed: " + parsed.path)
        if result.returncode:
            destination.unlink(missing_ok=True)
            raise RuntimeError("curl package download failed with exit " + str(result.returncode))

    def json(self, url):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "response.json"
            self.download(url, path)
            return json.loads(path.read_text(encoding="utf-8-sig"))


def archive_members(archive):
    seen = set()
    for entry in archive.infolist():
        name = entry.filename
        path = PurePosixPath(name)
        folded = name.casefold().rstrip("/")
        if ("\\" in name or ":" in name or path.is_absolute()
                or any(part in ("", ".", "..") for part in name.rstrip("/").split("/"))
                or folded in seen or stat.S_ISLNK(entry.external_attr >> 16)):
            raise ValueError("unsafe or duplicate package member: " + name)
        seen.add(folded)
        yield entry


def package_metadata(archive, package_id, version):
    specs = [name for name in archive.namelist()
             if "/" not in name and name.lower().endswith(".nuspec")]
    if len(specs) != 1:
        raise ValueError("package must contain one root nuspec")
    root = ET.fromstring(archive.read(specs[0]))
    values = {element.tag.rsplit("}", 1)[-1]: element.text for element in root.iter()}
    if (values.get("id", "").lower() != package_id.lower()
            or values.get("version", "").lower() != version.lower()):
        raise ValueError(f"NuGet identity mismatch: expected {package_id}/{version}")


class Packages:
    def __init__(self, feed=FEED, token=None):
        self.feed = feed
        self.curl = Curl(token, feed)
        resources = self.curl.json(feed)["resources"]
        bases = [item["@id"] for item in resources
                 if item.get("@type") == "PackageBaseAddress/3.0.0"]
        if len(bases) != 1:
            raise ValueError("GitHub feed must advertise one PackageBaseAddress/3.0.0")
        base = bases[0].rstrip("/")
        parsed = urlsplit(base)
        production = parsed.scheme == "https" and parsed.hostname == "nuget.pkg.github.com"
        local_test = parsed.scheme == "http" and parsed.hostname == "127.0.0.1"
        if not production and not local_test:
            raise ValueError("unexpected GitHub Packages content endpoint")
        self.base = base

    def versions(self, package_id):
        if not IDENTIFIER.fullmatch(package_id):
            raise ValueError("invalid package ID")
        identity = package_id.lower()
        data = self.curl.json(f"{self.base}/{identity}/index.json")
        versions = data.get("versions")
        if not isinstance(versions, list):
            raise ValueError("package version catalog is invalid")
        return versions

    def select(self, package_id, exact=None, stable=False):
        versions = self.versions(package_id)
        if exact is not None:
            matches = [value for value in versions if value.lower() == exact.lower()]
        else:
            matches = [value for value in versions if not stable or "-" not in value]
        if not matches:
            raise PackageMissing("no matching version for " + package_id)
        return max(matches, key=version_key)

    def package(self, package_id, version, destination):
        identity = package_id.lower()
        selected = version.lower()
        if not IDENTIFIER.fullmatch(package_id) or not VERSION.fullmatch(version):
            raise ValueError("invalid package identity")
        url = f"{self.base}/{identity}/{selected}/{identity}.{selected}.nupkg"
        self.curl.download(url, destination)
        with zipfile.ZipFile(destination) as archive:
            list(archive_members(archive))
            package_metadata(archive, package_id, version)
        return Path(destination)

    def extract(self, package_id, version, root):
        root = Path(root)
        target = root / package_id.lower() / version
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=target.parent) as temporary:
            temporary = Path(temporary)
            source = self.package(package_id, version, temporary / "package.nupkg")
            unpacked = temporary / "unpacked"
            unpacked.mkdir()
            with zipfile.ZipFile(source) as archive:
                members = list(archive_members(archive))
                for entry in members:
                    archive.extract(entry, unpacked)
                    mode = (entry.external_attr >> 16) & 0o777
                    if mode and not entry.is_dir():
                        (unpacked / entry.filename).chmod(mode)
            if target.exists():
                shutil.rmtree(target)
            unpacked.rename(target)
        print(f"curl restored {package_id}/{version}", flush=True)
        return target


def export(values, path=None):
    for key, value in values.items():
        value = str(value)
        if "\n" in value or "\r" in value:
            raise ValueError("invalid environment value")
        print(f"{key}={value}", flush=True)
        destination = os.environ.get("GITHUB_ENV")
        if destination:
            with open(destination, "a", encoding="utf-8") as output:
                output.write(f"{key}={value}\n")
    if path and os.environ.get("GITHUB_PATH"):
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
            output.write(str(path) + "\n")


def read_manifest(path):
    values = {}
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def validate_salts(root, rid):
    root = Path(root)
    required = [
        root / "lib/cmake/Salts/SaltsConfig.cmake",
        root / "lib/cmake/Salts/SaltsTargets.cmake",
        root / "include/cmeta/function.h",
        root / "include/unicode/salts_unicode.h",
        root / "salts-sdk-manifest.txt",
    ]
    for path in required:
        if not path.is_file():
            raise ValueError(f"incomplete Salts.Native SDK for {rid}: {path}")
    targets = (root / "lib/cmake/Salts/SaltsTargets.cmake").read_text(
        encoding="utf-8", errors="replace")
    if "Salts::Unicode" not in targets:
        raise ValueError("published Salts.Native must export canonical Salts::Unicode")
    if read_manifest(root / "salts-sdk-manifest.txt").get("version") != SALTS_NATIVE_VERSION:
        raise ValueError("Salts.Native manifest version/provenance mismatch")


def validate_component_runtime(root, rid):
    root = Path(root)
    required = [
        root / "include/salts/component_provider_abi.h",
        root / "include/salts/component_runtime_abi.h",
        root / "include/salts/configurator.h",
        root / "include/salts/service_host.h",
        root / "lib/cmake/Salts/SaltsTargets.cmake",
    ]
    for path in required:
        if not path.is_file():
            raise ValueError(f"incomplete published Component Runtime SDK for {rid}: {path}")
    targets = required[-1].read_text(encoding="utf-8", errors="replace")
    for target in ("Salts::ComponentProviderABI", "Salts::Configurator", "Salts::ServiceHost"):
        if target not in targets:
            raise ValueError(f"published Salts.Native SDK is missing target {target}")


def restore_re2c_for_rid(packages, package_root, rid):
    version = packages.select("Qigao.Re2c.Binary", stable=True)
    package = packages.extract("Qigao.Re2c.Binary", version, package_root)
    root = package / "tools" / rid
    exe = root / "bin" / ("re2c.exe" if rid.startswith("windows-") else "re2c")
    for required in [
        exe,
        root / "share/re2c/stdlib/unicode_categories.re",
        root / "share/re2c/stdlib/unicode_properties.re",
    ]:
        if not required.is_file():
            raise ValueError("incomplete re2c package: " + str(required))
    exe.chmod(exe.stat().st_mode | 0o111)
    subprocess.run([str(exe), "--version"], check=True)
    return version, root.resolve()


def restore_native_sdk(args, packages):
    if not RID.fullmatch(args.salts_rid) or not RID.fullmatch(args.host_rid):
        raise ValueError("unsupported target/host RID")
    requested_version = args.salts_version or SALTS_NATIVE_VERSION
    if not VERSION.fullmatch(requested_version):
        raise ValueError("an exact Salts.Native semantic version is required")
    version = packages.select("Salts.Native", exact=requested_version)
    if version != requested_version:
        raise ValueError("wrong canonical Salts.Native release: " + version)
    package = packages.extract("Salts.Native", version, args.packages)
    salts_root = package / "sdk" / args.salts_rid
    salts_host_root = package / "sdk" / args.host_rid
    validate_salts(salts_root, args.salts_rid)
    validate_salts(salts_host_root, args.host_rid)
    if args.require_component_runtime:
        validate_component_runtime(salts_root, args.salts_rid)
        validate_component_runtime(salts_host_root, args.host_rid)
    re2c_version, re2c_root = restore_re2c_for_rid(packages, args.packages, args.host_rid)
    values = {
        "SALTS_ROOT": salts_root.resolve(),
        "SALTS_HOST_ROOT": salts_host_root.resolve(),
        "SALTS_VERSION": version,
        "RE2C_ROOT": re2c_root,
        "RE2C_PACKAGE_VERSION": re2c_version,
        "QIGAO_NUGET_PACKAGES": args.packages.resolve(),
    }
    if args.with_turbowasm:
        tw_version = packages.select("TurboWasm.Native", stable=True)
        tw_package = packages.extract("TurboWasm.Native", tw_version, args.packages)
        tw_root = tw_package / "sdk" / args.salts_rid
        required = [
            tw_root / "lib/cmake/TurboWasm/TurboWasmConfig.cmake",
            tw_root / "include/turbowasm/component.h",
            tw_root / "lib/cmake/TurboWasm/TurboWasmTargets.cmake",
        ]
        for path in required:
            if not path.is_file():
                raise ValueError("incomplete TurboWasm.Native SDK: " + str(path))
        targets = required[-1].read_text(encoding="utf-8", errors="replace")
        if "TurboWasm::Component" not in targets:
            raise ValueError("released TurboWasm package does not export TurboWasm::Component")
        values["TURBOWASM_ROOT"] = tw_root.resolve()
        values["TURBOWASM_VERSION"] = tw_version
    export(values, re2c_root / "bin")
    result = {key: str(value) for key, value in values.items()}
    if args.result:
        args.result.parent.mkdir(parents=True, exist_ok=True)
        args.result.write_text(json.dumps(result, sort_keys=True), encoding="utf-8")
    return result


def restore_re2c(args, packages):
    hosts = {
        ("Linux", "X64"): "linux-x64",
        ("Linux", "ARM64"): "linux-arm64",
        ("macOS", "X64"): "macos-x64",
        ("macOS", "ARM64"): "macos-arm64",
        ("Windows", "X64"): "windows-x64",
    }
    rid = hosts[(os.environ["RUNNER_OS"], os.environ["RUNNER_ARCH"])]
    version, root = restore_re2c_for_rid(packages, args.packages, rid)
    export({"RE2C_ROOT": root, "RE2C_PACKAGE_VERSION": version}, root / "bin")


class BinaryCache:
    def __init__(self, packages, cache):
        self.packages = packages
        self.cache = Path(cache)
        self.lock = threading.Lock()

    def get(self, name, triplet, abi):
        if not IDENTIFIER.fullmatch(name) or not IDENTIFIER.fullmatch(triplet):
            raise ValueError("invalid vcpkg cache identity")
        if not re.fullmatch(r"[0-9a-f]{64}", abi):
            raise ValueError("invalid vcpkg ABI")
        destination = self.cache / abi[:2] / (abi + ".zip")
        with self.lock:
            if destination.is_file():
                return destination
            package_ids = []
            prefix = os.environ.get("X_VCPKG_NUGET_ID_PREFIX", "").strip()
            if prefix:
                if not IDENTIFIER.fullmatch(prefix):
                    raise ValueError("invalid vcpkg package prefix")
                package_ids.append(f"{prefix}_{name}_{triplet}")
            package_ids.append(f"{name}_{triplet}")
            matches = []
            for package_id in package_ids:
                try:
                    matches.extend((package_id, version)
                                   for version in self.packages.versions(package_id)
                                   if version.endswith("-vcpkg" + abi))
                except PackageMissing:
                    pass
            if len(matches) != 1:
                raise PackageMissing("no unique binary package for exact ABI")
            package_id, version = matches[0]
            with tempfile.TemporaryDirectory() as temporary:
                nupkg = self.packages.package(
                    package_id, version, Path(temporary) / "binary.nupkg")
                destination.parent.mkdir(parents=True, exist_ok=True)
                output = destination.with_suffix(".tmp")
                try:
                    with zipfile.ZipFile(nupkg) as source, zipfile.ZipFile(
                            output, "w", zipfile.ZIP_DEFLATED) as target:
                        evidence = source.read(f"share/{name}/vcpkg_abi_info.txt")
                        if hashlib.sha256(evidence).hexdigest() != abi:
                            raise ValueError("binary package ABI evidence mismatch")
                        for entry in archive_members(source):
                            member = entry.filename
                            if (member == "[Content_Types].xml" or member == ".signature.p7s"
                                    or (member.endswith(".nuspec") and "/" not in member)
                                    or member.startswith(("_rels/", "package/"))):
                                continue
                            target.writestr(entry, source.read(entry))
                    output.replace(destination)
                finally:
                    output.unlink(missing_ok=True)
            print(f"curl restored {package_id} ABI {abi}", flush=True)
            return destination


class LoopbackServer(ThreadingHTTPServer):
    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name = "127.0.0.1"
        self.server_port = self.server_address[1]


def cache_server(cache):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            self.respond(True)

        def do_HEAD(self):
            self.respond(False)

        def respond(self, body):
            parts = self.path.strip("/").split("/")
            try:
                if len(parts) != 4:
                    raise ValueError("invalid cache request")
                name, triplet, _version, abi = parts
                path = cache.get(name, triplet, abi)
                self.send_response(200)
                self.send_header("Content-Length", str(path.stat().st_size))
                self.end_headers()
                if body:
                    with path.open("rb") as source:
                        shutil.copyfileobj(source, self.wfile)
            except PackageMissing:
                self.send_error(404, "exact ABI is not cached")
            except Exception as error:
                print(type(error).__name__ + ": binary cache request failed", flush=True)
                self.send_error(502, "binary cache download failed")

        def log_message(self, *_):
            pass
    return LoopbackServer(("127.0.0.1", 0), Handler)


def serve(args, packages):
    server = cache_server(BinaryCache(packages, args.packages))
    ready = args.ready.with_suffix(".tmp")
    ready.write_text(json.dumps({"port": server.server_port, "pid": os.getpid()}))
    ready.replace(args.ready)
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    native = commands.add_parser("native-sdk")
    native.add_argument("--packages", type=Path, required=True)
    native.add_argument("--salts-rid", required=True)
    native.add_argument("--host-rid", required=True)
    native.add_argument("--with-turbowasm", action="store_true")
    native.add_argument("--salts-version")
    native.add_argument("--require-component-runtime", action="store_true")
    native.add_argument("--result", type=Path)

    re2c = commands.add_parser("re2c")
    re2c.add_argument("--packages", type=Path, required=True)

    server = commands.add_parser("serve-cache")
    server.add_argument("--packages", type=Path, required=True)
    server.add_argument("--ready", type=Path, required=True)

    args = parser.parse_args()
    args.packages = args.packages.resolve()
    packages = Packages()
    if args.command == "native-sdk":
        restore_native_sdk(args, packages)
    elif args.command == "re2c":
        restore_re2c(args, packages)
    else:
        serve(args, packages)


if __name__ == "__main__":
    main()
