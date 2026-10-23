#!/usr/bin/env python3
"""Offline curl/package/cache regressions for the SaltsUtils native CI helper."""
import argparse
import base64
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
    BinaryCache, LoopbackServer, PackageMissing, Packages,
    cache_server, restore_native_sdk, version_key,
)


class Feed:
    def __init__(self):
        self.documents = {}
        self.requests = []
        feed = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                feed.requests.append(self.path)
                expected = "Basic " + base64.b64encode(b"qigao:test-token").decode()
                if self.headers.get("Authorization") != expected:
                    self.send_error(401)
                    return
                data = feed.documents.get(self.path)
                if data is None:
                    self.send_error(404)
                    return
                if not isinstance(data, bytes):
                    data = json.dumps(data).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

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
            }]
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

    def test_exact_salts_and_re2c_environment_is_fail_closed(self):
        self.feed.add("Salts.Native", "2.3.0-rc.17", {
            "sdk/linux-x64/lib/cmake/Salts/SaltsConfig.cmake": "",
            "sdk/linux-x64/lib/cmake/Salts/SaltsTargets.cmake": "Salts::Unicode",
            "sdk/linux-x64/include/cmeta/function.h": "",
            "sdk/linux-x64/include/unicode/salts_unicode.h": "",
            "sdk/linux-x64/salts-sdk-manifest.txt": "version=2.3.0-rc.17\n",
        })
        self.feed.add("Qigao.Re2c.Binary", "1.0.0", {
            "tools/linux-x64/bin/re2c": "#!/bin/sh\nexit 0\n",
            "tools/linux-x64/share/re2c/stdlib/unicode_categories.re": "",
            "tools/linux-x64/share/re2c/stdlib/unicode_properties.re": "",
        })
        args = argparse.Namespace(
            packages=self.root / "packages", salts_rid="linux-x64",
            host_rid="linux-x64", with_turbowasm=False,
            salts_version=None, require_component_runtime=False,
            result=self.root / "result.json")
        env = self.root / "env"
        path = self.root / "path"
        with patch.dict(os.environ, {"GITHUB_ENV": str(env), "GITHUB_PATH": str(path)}):
            restore_native_sdk(args, self.packages)
        values = env.read_text()
        self.assertIn("SALTS_VERSION=2.3.0-rc.17", values)
        self.assertIn("SALTS_ROOT=", values)
        self.assertIn("RE2C_ROOT=", values)
        self.assertTrue(args.result.is_file())

    def test_explicit_published_component_runtime_version(self):
        version = "2.3.0-rc.18"
        files = {}
        for rid in ("linux-x64", "linux-arm64", "windows-x64",
                    "macos-arm64", "android-arm64-v8a", "ios-arm64"):
            prefix = "sdk/" + rid + "/"
            files.update({
                prefix + "lib/cmake/Salts/SaltsConfig.cmake": "",
                prefix + "lib/cmake/Salts/SaltsTargets.cmake":
                    "Salts::Unicode Salts::ComponentProviderABI Salts::Configurator Salts::ServiceHost",
                prefix + "include/cmeta/function.h": "",
                prefix + "include/unicode/salts_unicode.h": "",
                prefix + "include/salts/component_provider_abi.h": "",
                prefix + "include/salts/component_runtime_abi.h": "",
                prefix + "include/salts/configurator.h": "",
                prefix + "include/salts/service_host.h": "",
                prefix + "salts-sdk-manifest.txt": "version=2.3.0-rc.18\\n",
            })
        self.feed.add("Salts.Native", version, files)
        self.feed.add("Qigao.Re2c.Binary", "1.0.0", {
            "tools/linux-x64/bin/re2c": "#!/bin/sh\nexit 0\n",
            "tools/linux-x64/share/re2c/stdlib/unicode_categories.re": "",
            "tools/linux-x64/share/re2c/stdlib/unicode_properties.re": "",
        })
        args = argparse.Namespace(
            packages=self.root / "published-packages", salts_rid="linux-x64",
            host_rid="linux-x64", with_turbowasm=False,
            salts_version=version, require_component_runtime=True,
            result=self.root / "published-result.json")
        env = self.root / "published-env"
        path = self.root / "published-path"
        with patch.dict(os.environ, {"GITHUB_ENV": str(env), "GITHUB_PATH": str(path)}):
            resolved = restore_native_sdk(args, self.packages)
        self.assertEqual(resolved["SALTS_VERSION"], version)
        self.assertIn("SALTS_VERSION=2.3.0-rc.18", env.read_text())

    def test_explicit_published_component_runtime_rejects_missing_export(self):
        version = "2.3.0-rc.18"
        self.feed.add("Salts.Native", version, {
            "sdk/linux-x64/lib/cmake/Salts/SaltsConfig.cmake": "",
            "sdk/linux-x64/lib/cmake/Salts/SaltsTargets.cmake": "Salts::Unicode",
            "sdk/linux-x64/include/cmeta/function.h": "",
            "sdk/linux-x64/include/unicode/salts_unicode.h": "",
            "sdk/linux-x64/salts-sdk-manifest.txt": "version=2.3.0-rc.18\n",
        })
        args = argparse.Namespace(
            packages=self.root / "missing-runtime", salts_rid="linux-x64",
            host_rid="linux-x64", with_turbowasm=False,
            salts_version=version, require_component_runtime=True,
            result=None)
        with self.assertRaisesRegex(ValueError, "Component Runtime SDK"):
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


if __name__ == "__main__":
    unittest.main()
