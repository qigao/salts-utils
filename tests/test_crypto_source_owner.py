"""Contract tests for exactly one SaltsCore-authored ASN1/Crypto SDK."""
from pathlib import Path
import importlib.util
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "cmake" / "ci" / "install_static_crypto_sdk.py"
spec = importlib.util.spec_from_file_location("first_party_crypto_install", INSTALLER)
assert spec and spec.loader
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class SourceOwnerTest(unittest.TestCase):
    def test_pins_are_exact_git_commit_ids(self):
        for name in ("SALTSCORE_SOURCE_SHA", "VCPKG_PORT_SHA", "PR43_SOURCE_SHA"):
            self.assertRegex(getattr(installer, name), r"^[0-9a-f]{40}$")
        self.assertEqual(installer.GMSSL_PORT_VERSION, 14)

    def test_old_crypto_checkout_is_absent(self):
        workflow = (ROOT / ".github" / "workflows" / "native-build.yml").read_text()
        self.assertNotIn("repository: qigao/crypto", workflow)
        self.assertNotIn("repository: guanzhi/GmSSL", workflow)
        self.assertNotIn("path: _ci_crypto", workflow)
        self.assertIn("python3 cmake/ci/install_static_crypto_sdk.py", workflow)

    def test_source_and_port_are_exact_and_first_party(self):
        source = INSTALLER.read_text()
        self.assertIn('stage_tarball("qigao/salts-core", SALTSCORE_SOURCE_SHA', source)
        self.assertIn('stage_tarball("qigao/vcpkg-cache", VCPKG_PORT_SHA', source)
        self.assertIn('check_crypto_pr43_handoff.py', source)
        self.assertIn('check_crypto_module_ownership.py', source)
        self.assertIn('stage_mtls_gmssl_overlay.py', source)
        self.assertIn('audit_static_crypto.py', source)
        self.assertNotIn('stage_tarball("qigao/crypto"', source)
        self.assertNotIn("CRYPTO_GMSSL_SOURCE=", source)
        self.assertNotIn("CRYPTO_GMSSL_STATIC=", source)

    def test_does_not_reuse_stale_source_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "src"
            target.mkdir()
            with self.assertRaisesRegex(RuntimeError, "Refusing stale"):
                installer.stage_tarball("qigao/salts-core",
                                        installer.SALTSCORE_SOURCE_SHA,
                                        target, Path(directory))

    def test_cross_platform_targets_remain_explicit(self):
        for rid in ("linux-x64", "linux-arm64", "macos-arm64", "windows-x64"):
            generator, cross = installer.native_toolchain(rid)
            self.assertTrue(generator)
            self.assertIsInstance(cross, list)
        with self.assertRaisesRegex(ValueError, "Unsupported native Crypto RID"):
            installer.native_toolchain("unknown-rid")

    def test_exact_abi_import_without_global_fallback(self):
        for path in ("CMakeLists.txt", "cmake/SaltsUtilsConfig.cmake.in"):
            cmake = (ROOT / path).read_text()
            self.assertIn("CRYPTO_ROOT", cmake)
            self.assertIn("Salts::ASN1", cmake)
            self.assertIn("Salts::Crypto", cmake)
            self.assertIn("NO_DEFAULT_PATH", cmake)
            self.assertIn("STATIC_LIBRARY", cmake)
            self.assertIn("CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER", cmake)
            self.assertNotRegex(
                cmake, r"(?m)^\s*find_package\(\s*Crypto\s+0\.1\s+CONFIG\s+REQUIRED\s*\)")
        self.assertIn("find_dependency(Crypto 0.1 CONFIG",
                      (ROOT / "cmake/SaltsUtilsConfig.cmake.in").read_text())

    def test_real_installed_c11_cpp17_consumer_is_required(self):
        source = INSTALLER.read_text()
        self.assertIn('"asn1" / "tests" / "installed"', source)
        self.assertIn('"crypto" / "tests" / "owner"', source)
        self.assertIn('"--no-tests=error"', source)
        self.assertIn('"--output-on-failure"', source)
        self.assertIn('"-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=NEVER"', source)
        self.assertIn('f"-DASN1_DIR=', source)
        self.assertIn('f"-DCrypto_DIR=', source)
        self.assertIn('"--config", "Release"', source)
        self.assertIn("cross-target execution NOT tested", source)

    def test_host_only_executions_are_never_mislabeled(self):
        import unittest.mock
        with unittest.mock.patch.dict("os.environ",
                                      {"QIGAO_HOST_RID": "linux-x64",
                                       "QIGAO_SDK_RID": "android-arm64-v8a"}):
            self.assertEqual(installer.host_for_local_tests(), "linux-x64")
        with unittest.mock.patch.dict("os.environ",
                                      {"QIGAO_HOST_RID": "", "QIGAO_SDK_RID": "macos-arm64"}):
            self.assertEqual(installer.host_for_local_tests(), "macos-arm64")

    def test_two_static_archives_and_mtls_are_required(self):
        source = INSTALLER.read_text()
        self.assertIn("libsalts_asn1.a", source)
        self.assertIn("libsalts_crypto.a", source)
        self.assertIn("static-archive-audit.json", source)
        self.assertIn("ASN1Config.cmake", source)
        self.assertIn("-DASN1_DIR=", source)
        self.assertIn("-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=NEVER", source)
        self.assertIn('tls_auth.h', source)
        self.assertIn('CRYPTO_ROOT', source)
        self.assertIn('CRYPTO_HOST_ROOT', source)


if __name__ == "__main__":
    unittest.main()
