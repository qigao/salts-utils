#!/usr/bin/env python3
"""Offline curl/package/cache regressions for the SaltsUtils native CI helper."""
import argparse
import base64
import email
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler
import unittest
from unittest.mock import patch
import zipfile

from github_packages import (
    BinaryCache, Curl, LoopbackServer, PackageMissing, Packages,
    cache_server, restore_native_sdk, version_key,
)
from github_files import Artifacts
from pack_native_sdk import pack, RIDS


def saltscore_files(rids, missing_target=None):
    files = {}
    targets = ["Salts::Core", "Salts::FS", "Salts::Process",
               "Salts::USB", "Salts::Unicode"]
    crypto_targets = ["Salts::Crypto"]
    if missing_target in targets:
        targets.remove(missing_target)
    if missing_target in crypto_targets:
        crypto_targets.remove(missing_target)
    for rid in rids:
        prefix = f"sdk/{rid}/"
        files.update({
            prefix + "lib/cmake/SaltsCore/SaltsCoreConfig.cmake": "",
            prefix + "lib/cmake/SaltsCore/SaltsCoreTargets.cmake":
                " ".join(targets),
            prefix + "lib/cmake/Crypto/CryptoTargets.cmake":
                " ".join(crypto_targets),
            prefix + "include/salts_core/runtime_abi.h": "",
            prefix + "include/salts/fs.h": "",
            prefix + "include/salts/process.h": "",
            prefix + "include/cflow/usb.h": "",
            prefix + "include/salts_unicode.h": "",
        })
    return files


def cmeta_files(rids, version="0.1.0", missing_target=None):
    files = {}
    targets = ["CMeta::CMeta", "CMeta::CSTL", "CMeta::CFlow",
               "CMeta::PluginABI", "CMeta::Plugin", "CMeta::Configurator",
               "CMeta::ServiceHost"]
    if missing_target:
        targets.remove(missing_target)
    for rid in rids:
        prefix = f"sdk/{rid}/"
        files.update({
            prefix + "lib/cmake/CMeta/CMetaConfig.cmake": "",
            prefix + "lib/cmake/CMeta/CMetaTargets.cmake": " ".join(targets),
            prefix + "include/cmeta/cmeta.h": "",
            prefix + "include/cflow/cflow.h": "",
            prefix + "cmeta-sdk-manifest.txt":
                f"release=0.1.000\npackage={version}\nrid={rid}\n",
        })
    return files


class Feed:
    def __init__(self, auth="Basic " + base64.b64encode(b"qigao:test-token").decode()):
        self.documents = {}
        self.requests = []
        self.auth = auth
        self.uploads = []
        self.publish_status = 201
        feed = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                feed.requests.append(self.path)
                if self.headers.get("Authorization") != feed.auth:
                    self.send_error(401)
                    return
                data = feed.documents.get(self.path)
                if data is None:
                    self.send_error(404)
                    return
                if isinstance(data, tuple):
                    self.send_response(302)
                    self.send_header("Location", data[1])
                    self.end_headers()
                    return
                if not isinstance(data, bytes):
                    data = json.dumps(data).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_PUT(self):
                if (self.headers.get("Authorization") != feed.auth
                        or self.headers.get("X-NuGet-ApiKey") != "test-token"):
                    self.send_error(401)
                    return
                body = self.rfile.read(int(self.headers["Content-Length"]))
                message = email.message_from_bytes(
                    ("Content-Type: " + self.headers["Content-Type"] + "\r\n\r\n").encode() + body)
                feed.uploads.append(message.get_payload()[0].get_payload(decode=True))
                self.send_response(feed.publish_status)
                self.send_header("Content-Length", "0")
                self.end_headers()

            def log_message(self, *_):
                pass

        self.server = LoopbackServer(("127.0.0.1", 0), Handler)
        self.base = f"http://127.0.0.1:{self.server.server_port}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.documents["/index.json"] = {
            "resources": [{
                "@id": self.base + "/flat/",
                "@type": "PackageBaseAddress/3.0.0",
            }, {"@id": self.base + "/publish", "@type": "PackagePublish/2.0.0"}]
        }

    def close(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()

    def add(self, package, version, files, spec_id=None):
        identity = package.lower()
        index = self.documents.setdefault(
            f"/flat/{identity}/index.json", {"versions": []})
        index["versions"].append(version)
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr(
                identity + ".nuspec",
                f'<package xmlns="urn:nuget"><metadata><id>{spec_id or package}</id>'
                f"<version>{version}</version></metadata></package>")
            archive.writestr("[Content_Types].xml", "NuGet metadata")
            for name, value in files.items():
                archive.writestr(name, value)
        path = f"/flat/{identity}/{version.lower()}/{identity}.{version.lower()}.nupkg"
        self.documents[path] = data.getvalue()


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.feed = Feed()
        self.packages = Packages(self.feed.base + "/index.json", "test-token")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.feed.close()
        self.tmp.cleanup()

    def test_version_selection_and_safe_extraction(self):
        for version in ["1.0.0", "1.1.0-rc.2", "1.1.0-rc.11"]:
            self.feed.add("SDK.Native", version, {"sdk/file": version})
        self.assertEqual(self.packages.select("SDK.Native", stable=True), "1.0.0")
        self.assertEqual(self.packages.select("SDK.Native"), "1.1.0-rc.11")
        self.assertLess(version_key("1.1.0-rc.2"), version_key("1.1.0-rc.11"))
        target = self.packages.extract("SDK.Native", "1.0.0", self.root)
        self.assertEqual((target / "sdk/file").read_text(), "1.0.0")

    def test_rejects_wrong_identity_and_path_escape(self):
        self.feed.add("wrong.native", "1.0.0", {"safe": "x"}, spec_id="other")
        with self.assertRaisesRegex(ValueError, "identity"):
            self.packages.extract("wrong.native", "1.0.0", self.root)
        self.feed.add("escape.native", "1.0.0", {"../outside": "x"})
        with self.assertRaisesRegex(ValueError, "unsafe"):
            self.packages.extract("escape.native", "1.0.0", self.root)
        self.assertFalse((self.root / "outside").exists())

    def test_exact_split_sdks_and_re2c_environment_are_fail_closed(self):
        self.feed.add("SaltsCore.Native", "0.1.0",
                      saltscore_files(("linux-x64",)))
        self.feed.add("CMeta.Native", "0.1.2",
                      cmeta_files(("linux-x64",), "0.1.2"))
        self.feed.add("Qigao.Re2c.Binary", "1.0.0", {
            "tools/linux-x64/bin/re2c": "#!/bin/sh\nexit 0\n",
            "tools/linux-x64/share/re2c/stdlib/unicode_categories.re": "",
            "tools/linux-x64/share/re2c/stdlib/unicode_properties.re": "",
        })
        args = argparse.Namespace(
            packages=self.root / "packages", salts_rid="linux-x64",
            host_rid="linux-x64", with_turbowasm=False,
            saltscore_version=None, cmeta_version=None,
            require_component_runtime=False,
            result=self.root / "result.json")
        env = self.root / "env"
        path = self.root / "path"
        with patch.dict(os.environ, {"GITHUB_ENV": str(env), "GITHUB_PATH": str(path)}), \
                patch("github_packages.restore_re2c_for_rid",
                      return_value=("1.0.0", self.root / "re2c")):
            restore_native_sdk(args, self.packages)
        values = env.read_text()
        self.assertIn("SALTSCORE_VERSION=0.1.0", values)
        self.assertIn("CMETA_VERSION=0.1.2", values)
        self.assertIn("SALTSCORE_ROOT=", values)
        self.assertIn("CMETA_ROOT=", values)
        self.assertIn("RE2C_ROOT=", values)
        self.assertTrue(args.result.is_file())

    def test_explicit_published_component_runtime_version(self):
        version = "0.1.0"
        rids = ("linux-x64", "linux-arm64", "windows-x64", "windows-arm64",
                "macos-x64", "macos-arm64", "android-x64", "android-arm64")
        self.feed.add("SaltsCore.Native", version, saltscore_files(rids))
        self.feed.add("CMeta.Native", version, cmeta_files(rids, version))
        self.feed.add("Qigao.Re2c.Binary", "1.0.0", {
            "tools/linux-x64/bin/re2c": "#!/bin/sh\nexit 0\n",
            "tools/linux-x64/share/re2c/stdlib/unicode_categories.re": "",
            "tools/linux-x64/share/re2c/stdlib/unicode_properties.re": "",
        })
        args = argparse.Namespace(
            packages=self.root / "published-packages", salts_rid="linux-x64",
            host_rid="linux-x64", with_turbowasm=False,
            saltscore_version=version, cmeta_version=version,
            require_component_runtime=True,
            result=self.root / "published-result.json")
        env = self.root / "published-env"
        path = self.root / "published-path"
        with patch.dict(os.environ, {"GITHUB_ENV": str(env), "GITHUB_PATH": str(path)}), \
                patch("github_packages.restore_re2c_for_rid",
                      return_value=("1.0.0", self.root / "re2c")):
            resolved = restore_native_sdk(args, self.packages)
        self.assertEqual(resolved["SALTSCORE_VERSION"], version)
        self.assertEqual(resolved["CMETA_VERSION"], version)
        self.assertIn("SALTSCORE_VERSION=0.1.0", env.read_text())

    def test_explicit_published_component_runtime_rejects_missing_export(self):
        version = "0.1.0"
        self.feed.add("SaltsCore.Native", version,
                      saltscore_files(("linux-x64",)))
        self.feed.add("CMeta.Native", version, cmeta_files(
            ("linux-x64",), version, missing_target="CMeta::ServiceHost"))
        args = argparse.Namespace(
            packages=self.root / "missing-runtime", salts_rid="linux-x64",
            host_rid="linux-x64", with_turbowasm=False,
            saltscore_version=version, cmeta_version=version,
            require_component_runtime=True,
            result=None)
        with self.assertRaisesRegex(ValueError, "missing target CMeta::ServiceHost"):
            restore_native_sdk(args, self.packages)

    def test_split_saltscore_rejects_missing_sibling_crypto_export(self):
        version = "0.1.0"
        self.feed.add("SaltsCore.Native", version, saltscore_files(
            ("linux-x64",), missing_target="Salts::Crypto"))
        self.feed.add("CMeta.Native", "0.1.2",
                      cmeta_files(("linux-x64",), "0.1.2"))
        args = argparse.Namespace(
            packages=self.root / "missing-crypto", salts_rid="linux-x64",
            host_rid="linux-x64", with_turbowasm=False,
            saltscore_version=version, cmeta_version="0.1.2",
            require_component_runtime=False, result=None)
        with self.assertRaisesRegex(ValueError, "missing target Salts::Crypto"):
            restore_native_sdk(args, self.packages)

    def test_exact_abi_cache_conversion_and_prefix(self):
        evidence = b"exact ABI evidence"
        abi = hashlib.sha256(evidence).hexdigest()
        self.feed.add("vcpkg-cache_test_x64-linux", "1.0.0-vcpkg" + abi, {
            "share/test/vcpkg_abi_info.txt": evidence,
            "lib/test.a": b"archive",
            "_rels/.rels": "metadata",
        })
        cache = BinaryCache(self.packages, self.root / "cache")
        with patch.dict(os.environ, {"X_VCPKG_NUGET_ID_PREFIX": "vcpkg-cache"}):
            result = cache.get("test", "x64-linux", abi)
        with zipfile.ZipFile(result) as archive:
            self.assertEqual(archive.read("lib/test.a"), b"archive")
            self.assertFalse(any(name.startswith("_rels/") for name in archive.namelist()))

    def test_http_adapter_serves_native_zip(self):
        evidence = b"cache ABI"
        abi = hashlib.sha256(evidence).hexdigest()
        self.feed.add("test_x64-linux", "1.0.0-vcpkg" + abi, {
            "share/test/vcpkg_abi_info.txt": evidence,
            "lib/test.a": b"archive",
        })
        server = cache_server(BinaryCache(self.packages, self.root / "cache"))
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            url = f"http://127.0.0.1:{server.server_port}/test/x64-linux/1.0.0/{abi}"
            subprocess.run(["curl", "--fail", "--silent", "--head", url],
                           check=True, capture_output=True)
        finally:
            server.shutdown()
            thread.join()
            server.server_close()

    def test_authentication_failure_does_not_expose_token(self):
        with self.assertRaises(PackageMissing) as error:
            Packages(self.feed.base + "/index.json", "wrong-secret")
        self.assertNotIn("wrong-secret", str(error.exception))

    def test_curl_publication_and_immutable_duplicate(self):
        self.feed.add("SDK.Native", "1.0.0", {"sdk/file": "original"})
        source = self.feed.documents["/flat/sdk.native/1.0.0/sdk.native.1.0.0.nupkg"]
        package = self.root / "package with space.nupkg"
        package.write_bytes(source)
        self.packages.publish("SDK.Native", "1.0.0", package)
        self.assertEqual(self.feed.uploads, [source])
        self.feed.publish_status = 409
        self.packages.publish("SDK.Native", "1.0.0", package)
        self.feed.add("SDK.Native", "1.0.0", {"sdk/file": "changed"})
        with self.assertRaisesRegex(ValueError, "immutable package differs"):
            self.packages.publish("SDK.Native", "1.0.0", package)

    def test_bearer_token_is_not_forwarded_to_artifact_storage(self):
        storage = Feed(auth=None)
        try:
            storage.documents["/payload"] = b"artifact"
            self.feed.auth = "Bearer test-token"
            self.feed.documents["/redirect"] = (302, storage.base + "/payload")
            Curl("test-token", self.feed.base, bearer=True).download(
                self.feed.base + "/redirect", self.root / "payload")
            self.assertEqual((self.root / "payload").read_bytes(), b"artifact")
        finally:
            storage.close()


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.feed = Feed(auth="Bearer test-token")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.commit = "a" * 40
        self.client = Artifacts("qigao/salts-utils", "test-token", api=self.feed.base)
        self.run = "/repos/qigao/salts-utils/actions/runs/7"
        self.feed.documents[self.run] = {"head_sha": self.commit,
                                         "head_repository": {"full_name": "qigao/salts-utils"}}
        self.catalog = {"artifacts": []}
        self.feed.documents[self.run + "/artifacts?per_page=100&page=1"] = self.catalog

    def tearDown(self):
        self.feed.close()
        self.tmp.cleanup()

    def artifact(self, name="native-linux-release", files=None):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            for key, value in (files or {"native-build.tar": b"compiled graph"}).items():
                archive.writestr(key, value)
        payload = data.getvalue()
        number = len(self.catalog["artifacts"]) + 1
        item = {"id": number, "name": name, "expired": False,
                "digest": "sha256:" + hashlib.sha256(payload).hexdigest()}
        self.catalog["artifacts"].append(item)
        self.feed.documents[f"/repos/qigao/salts-utils/actions/artifacts/{number}/zip"] = payload
        return item

    def test_downloaded_digest_and_source_are_verified(self):
        self.artifact()
        self.client.download(7, "native-linux-release", self.commit, self.root / "compiled")
        self.assertEqual((self.root / "compiled/native-build.tar").read_bytes(), b"compiled graph")
        with self.assertRaisesRegex(ValueError, "commit mismatch"):
            self.client.download(7, "native-linux-release", "b" * 40, self.root / "wrong")

    def test_bad_digest_or_payload_does_not_publish_partial_files(self):
        item = self.artifact()
        item["digest"] = "sha256:" + "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            self.client.download(7, "native-*", self.commit, self.root / "bad")
        self.assertFalse((self.root / "bad").exists())
        self.catalog["artifacts"].clear()
        self.artifact(files={"../escape": b"invalid"})
        with self.assertRaisesRegex(ValueError, "unsafe"):
            self.client.download(7, "native-*", self.commit, self.root / "unsafe")
        self.assertFalse((self.root / "escape").exists())

    def test_overlapping_or_missing_artifacts_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing"):
            self.client.download(7, "missing-*", self.commit, self.root / "missing")
        self.artifact("sdk-linux", {"shared/file": b"first"})
        self.artifact("sdk-macos", {"shared/file": b"second"})
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.client.download(7, "sdk-*", self.commit, self.root / "overlap")
        self.assertFalse((self.root / "overlap").exists())


class NativePackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.commit = "c" * 40
        for rid in RIDS:
            prefix = self.root / "payload/sdk" / rid
            for name in ("lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake", "lib/cmake/SaltsUtils/IDL.cmake",
                         "include/data_bind_binding_plan.h", "include/data_bind_native.h",
                         "salts-utils-sdk-manifest.txt"):
                path = prefix / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("SDK bytes")
            (prefix / "salts-utils-sdk-manifest.txt").write_text(
                f"version=0.1.0\ncommit={self.commit}\nrid={rid}\nprofile=release\n")
            if not rid.startswith("android-"):
                tool = prefix / "bin" / ("salts-idlc.exe" if rid.startswith("windows-") else "salts-idlc")
                tool.parent.mkdir()
                tool.write_bytes(b"host IDL compiler")
                tool.chmod(0o644)

    def tearDown(self):
        self.tmp.cleanup()

    def test_offline_package_is_reproducible_and_preserves_tool_permissions(self):
        first = pack(self.root / "payload", self.root / "first.nupkg", "0.1.0", self.commit)
        second = pack(self.root / "payload", self.root / "second.nupkg", "0.1.0", self.commit)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as archive:
            for rid in ("linux-x64", "linux-arm64", "macos-arm64"):
                self.assertEqual(archive.getinfo(f"sdk/{rid}/bin/salts-idlc").external_attr >> 16 & 0o111, 0o111)
            self.assertNotIn(b"<dependencies", archive.read("SaltsUtils.Native.nuspec"))

    def test_bad_provenance_does_not_replace_existing_package(self):
        output = self.root / "existing.nupkg"
        output.write_bytes(b"previous immutable package")
        with self.assertRaisesRegex(ValueError, "provenance"):
            pack(self.root / "payload", output, "0.1.0", "d" * 40)
        self.assertEqual(output.read_bytes(), b"previous immutable package")


if __name__ == "__main__":
    unittest.main()
