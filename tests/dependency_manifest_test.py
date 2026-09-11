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


if __name__ == "__main__":
    unittest.main()
