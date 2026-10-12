"""Offline negative/positive validation of the exact published Salts once ABI gate."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
import json

source = Path(__file__).with_name("verify-published-salts-rc15.py")
spec = importlib.util.spec_from_file_location("salts_rc15_verify", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SaltsOfficialRc15Test(unittest.TestCase):
    def release(self):
        return {"tag_name": "v" + module.VERSION, "draft": False,
                "prerelease": True,
                "assets": [{"name": "Salts.Native." + module.VERSION + ".nupkg",
                            "digest": "sha256:" + module.SHA256,
                            "size": 500, "state": "uploaded"}]}

    def test_official_release_tag_source_digest_fail_closed(self):
        release = self.release()
        module.verify_release(release, {"sha": module.SOURCE})
        for field, invalid in (("digest", "sha256:" + "0" * 64),
                               ("state", "new"), ("size", 0)):
            bad = self.release()
            bad["assets"][0][field] = invalid
            with self.assertRaises(ValueError):
                module.verify_release(bad, {"sha": module.SOURCE})
        for changes in ({"draft": True}, {"prerelease": False},
                        {"tag_name": "v2.3.0-rc.14"}):
            bad = self.release()
            bad.update(changes)
            with self.assertRaises(ValueError):
                module.verify_release(bad, {"sha": module.SOURCE})
        with self.assertRaises(ValueError):
            module.verify_release(release, {"sha": "f" * 40})
        bad = self.release()
        bad["assets"].append(dict(bad["assets"][0]))
        with self.assertRaises(ValueError):
            module.verify_release(bad, {"sha": module.SOURCE})

    def fixture(self, path, bad_rid=None, bad_header=None):
        package = path / ("Salts.Native." + module.VERSION + ".nupkg")
        with zipfile.ZipFile(package, "w") as z:
            for rid in module.RIDS:
                prefix = f"sdk/{rid}/"
                props = {"version": module.VERSION, "commit": module.SOURCE,
                         "profile": "release", **module.PROFILES[rid]}
                if rid == bad_rid:
                    first = next(iter(module.PROFILES[rid]))
                    props[first] = "wrong"
                z.writestr(prefix + "salts-sdk-manifest.txt",
                           "".join(f"{k}={value}\\n" for k, value in props.items()).replace("\\n", "\n"))
                header = ("volatile int state; SALTS_ONCE_INIT {0}"
                          if rid == "windows-x64" else "pthread_once_t native;")
                if bad_header == rid:
                    header = "legacy CMeta atomic spin"
                z.writestr(prefix + "include/salts/thread.h", header)
                root = path / "sdk" / rid
                (root / "lib/cmake/Salts").mkdir(parents=True)
                (root / "include/salts").mkdir(parents=True)
                (root / "lib/cmake/Salts/SaltsConfig.cmake").write_text("set(ok yes)")
                (root / "include/salts/thread.h").write_text(header)
            z.writestr("Salts.Native.nuspec",
                       f"<package><metadata><id>Salts.Native</id><version>{module.VERSION}</version></metadata></package>")
        return hashlib.sha256(package.read_bytes()).hexdigest()

    def test_real_six_rid_archive_and_cross_host_attestation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            digest = self.fixture(path)
            for rid, host in module.HOSTS.items():
                self.assertEqual(module.verify_archive(path, rid, host, digest), digest)
            for rid, host in module.HOSTS.items():
                wrong = next(h for h in module.RIDS if h != host)
                with self.assertRaises(ValueError):
                    module.verify_archive(path, rid, wrong, digest)
            with self.assertRaises(ValueError):
                module.verify_archive(path, "linux-x64", "linux-x64", "0" * 64)

    def test_old_once_and_wrong_manifest_rejected_each_platform(self):
        for rid in module.RIDS:
            with self.subTest(rid=rid), tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp)
                digest = self.fixture(path, bad_header=rid)
                with self.assertRaises(ValueError):
                    module.verify_archive(path, "linux-x64", "linux-x64", digest)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            digest = self.fixture(path, bad_rid="android-arm64-v8a")
            with self.assertRaises(ValueError):
                module.verify_archive(path, "linux-x64", "linux-x64", digest)


if __name__ == "__main__":
    unittest.main()
