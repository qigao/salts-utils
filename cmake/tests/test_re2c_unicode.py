"""Exercise the actual CMake data validator without downloading a toolchain."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / "VerifyRe2cUnicode.cmake"
REFERENCE = b"/*!re2c\nL = [a-z];\n*/\n"
REFERENCE_SHA256 = hashlib.sha256(REFERENCE).hexdigest()


class UnicodeDataValidation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="re2c-unicode-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.input = self.root / "unicode.re"
        self.runner = self.root / "check.cmake"
        self.runner.write_text(
            'include("${VALIDATION_MODULE}")\n'
            'salts_utils_verify_re2c_unicode_file("${INPUT_FILE}" "${EXPECTED_SHA256}")\n',
            encoding="utf-8",
        )

    def check_data(self, data, expected_success):
        if data is not None:
            self.input.write_bytes(data)
        result = subprocess.run(
            ["cmake", f"-DVALIDATION_MODULE={MODULE}", f"-DINPUT_FILE={self.input}",
             f"-DEXPECTED_SHA256={REFERENCE_SHA256}", "-P", str(self.runner)],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode == 0, expected_success, result.stdout + result.stderr)
        if not expected_success:
            self.assertIn("re2c Unicode", result.stderr)
        if data is not None:
            self.assertEqual(self.input.read_bytes(), data, "validator must not rewrite source data")

    def test_lf(self):
        self.check_data(REFERENCE, True)

    def test_crlf(self):
        self.check_data(REFERENCE.replace(b"\n", b"\r\n"), True)

    def test_mixed_newlines(self):
        self.check_data(REFERENCE.replace(b"\n", b"\r\n", 1), True)

    def test_changed_character_range(self):
        self.check_data(REFERENCE.replace(b"a-z", b"a-y"), False)

    def test_appended_content(self):
        self.check_data(REFERENCE + b"changed\n", False)

    def test_missing_file(self):
        self.check_data(None, False)

    def test_missing_final_newline(self):
        self.check_data(REFERENCE[:-1], False)

    def test_bare_carriage_return(self):
        self.check_data(REFERENCE.replace(b"\n", b"\r"), False)

    def test_bom(self):
        self.check_data(b"\xef\xbb\xbf" + REFERENCE, False)

    def test_nul_suffix(self):
        self.check_data(REFERENCE + b"\0changed", False)

    def test_cli_requires_directory(self):
        result = subprocess.run(["cmake", "-P", str(MODULE)],
                                capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("RE2C_STDLIB_DIR", result.stderr)


if __name__ == "__main__":
    if shutil.which("cmake") is None:
        raise SystemExit("cmake is required")
    unittest.main()
