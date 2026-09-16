import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DependencyManifestTest(unittest.TestCase):
    def test_boringssl_replaces_openssl(self):
        manifest = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))
        names = {
            dep if isinstance(dep, str) else dep["name"]
            for dep in manifest["dependencies"]
        }

        self.assertIn("boringssl", names)
        self.assertNotIn("openssl", names)

    def test_crypto_target_uses_boringssl_for_common_primitives(self):
        cmake = (ROOT / "crypto" / "CMakeLists.txt").read_text(encoding="utf-8")
        sha256 = (ROOT / "crypto" / "src" / "crypto_sha256.c").read_text(encoding="utf-8")
        ed448 = (ROOT / "crypto" / "src" / "crypto_ed448.c").read_text(encoding="utf-8")

        self.assertIn("OpenSSL::Crypto", cmake)
        self.assertNotIn(" monocypher", cmake)
        self.assertIn("<openssl/sha.h>", sha256)
        self.assertNotIn("<libecc/hash/sha256.h>", sha256)
        self.assertIn("OPENSSL_cleanse", sha256)
        self.assertIn("<openssl/mem.h>", ed448)
        self.assertNotIn("<monocypher.h>", ed448)
        self.assertIn("OPENSSL_cleanse", ed448)


if __name__ == "__main__":
    unittest.main()
