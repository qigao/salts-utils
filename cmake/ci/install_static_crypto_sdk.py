#!/usr/bin/env python3
"""Build exact canonical static Crypto SDK(s) for the SaltsUtils CI target.
No source copy, compatibility target, runtime DSO, fallback or release pin update.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

CRYPTO_COMMIT = "a99f29a569af14b865c9e713360aa81d39a04bf0"
GMSSL_COMMIT = "7c9f02904ef33e59c87b4f16621cc8fd434e7579"


def run(*cmd: str) -> None:
    print("+", *cmd, flush=True)
    subprocess.run(list(map(str, cmd)), check=True)


def sha(root: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()


def native_toolchain(rid: str) -> tuple[list[str], list[str]]:
    if rid == "windows-x64":
        return ["-G", "Visual Studio 18 2026", "-A", "x64"], []
    opts: list[str] = []
    if rid.startswith("android-"):
        ndk = Path(os.environ["ANDROID_NDK_HOME"])
        toolchain = ndk / "build" / "cmake" / "android.toolchain.cmake"
        if not toolchain.is_file():
            raise FileNotFoundError(toolchain)
        arch = {"android-arm64-v8a": "arm64-v8a",
                "android-x64": "x86_64", "android-x86": "x86"}.get(rid)
        if arch is None:
            raise ValueError("unsupported Android Crypto target " + rid)
        opts.extend([f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                     f"-DANDROID_ABI={arch}", "-DANDROID_PLATFORM=android-26"])
    elif rid == "ios-arm64":
        opts.extend(["-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=iphoneos",
                     "-DCMAKE_OSX_ARCHITECTURES=arm64",
                     "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"])
    elif rid == "macos-arm64":
        opts.append("-DCMAKE_OSX_ARCHITECTURES=arm64")
    elif rid not in ("linux-x64", "linux-arm64"):
        raise ValueError("unsupported native Crypto target " + rid)
    return ["-G", "Ninja"], opts


def install_one(root: Path, rid: str, source: Path, gmssl: Path) -> Path:
    sdk = root / "stage" / "crypto" / rid
    if sdk.exists():
        raise RuntimeError("Refusing to reuse or overwrite stale Crypto SDK " + str(sdk))
    generator, cross = native_toolchain(rid)
    private = root / "build" / "ci-crypto" / rid
    gmssl_build = private / "gmssl"
    crypto_build = private / "crypto"
    common = [
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DBUILD_SHARED_LIBS=OFF",
    ]
    enable = [
        "-DENABLE_TLS=ON", "-DENABLE_SECP256R1=ON",
        "-DENABLE_SECP384R1=ON", "-DENABLE_SHA2=ON",
        "-DENABLE_AES=ON", "-DENABLE_AES_CCM=OFF", "-DENABLE_SHA1=ON",
        "-DENABLE_CHACHA20=OFF", "-DENABLE_GHASH=OFF",
        "-DENABLE_SM9=OFF", "-DENABLE_CMS=OFF", "-DENABLE_LMS=OFF",
        "-DENABLE_XMSS=OFF", "-DENABLE_SPHINCS=OFF",
        "-DENABLE_KYBER=OFF", "-DENABLE_ZUC=OFF",
        "-DENABLE_SKF=OFF", "-DENABLE_SDF=OFF",
    ]
    run("cmake", "-S", str(gmssl), "-B", str(gmssl_build),
        *generator, *cross, *common, *enable)
    run("cmake", "--build", str(gmssl_build), "--config", "Release",
        "--target", "gmssl", "--parallel", "2")
    archive = (gmssl_build / "bin" / "Release" / "gmssl.lib"
               if rid == "windows-x64" else gmssl_build / "bin" / "libgmssl.a")
    if not archive.is_file():
        raise FileNotFoundError("Pinned GmSSL archive missing " + str(archive))

    run("cmake", "-S", str(source), "-B", str(crypto_build),
        *generator, *cross, *common, "-DBUILD_TESTING=OFF",
        f"-DCRYPTO_GMSSL_SOURCE={gmssl}",
        f"-DCRYPTO_GMSSL_STATIC={archive}")
    run("cmake", "--build", str(crypto_build), "--config", "Release",
        "--parallel", "2")
    run("cmake", "--install", str(crypto_build), "--config", "Release",
        "--prefix", str(sdk))
    expected = sdk / "lib" / ("salts_crypto.lib" if rid == "windows-x64"
                               else "libsalts_crypto.a")
    cfg = sdk / "lib" / "cmake" / "Crypto" / "CryptoConfig.cmake"
    if not expected.is_file() or not cfg.is_file():
        raise RuntimeError("Incomplete installed single-owner Crypto SDK " + str(sdk))
    other = [p for p in sdk.rglob("*") if p.is_file()
             and p.suffix.lower() in (".a", ".lib", ".dll", ".so", ".dylib")
             and p != expected]
    if other:
        raise RuntimeError("Duplicate Crypto binaries in SDK: " + str(other))
    print(f"PASS: {rid} canonical static Salts::Crypto installed: {expected}", flush=True)
    return sdk.resolve(strict=True)


def main() -> int:
    root = Path(os.environ["GITHUB_WORKSPACE"]).resolve()
    source = root / "_ci_crypto"
    gmssl = root / "_ci_gmssl"
    rid = os.environ["QIGAO_SDK_RID"]
    host = os.environ.get("QIGAO_HOST_RID") or rid
    if sha(source) != CRYPTO_COMMIT or sha(gmssl) != GMSSL_COMMIT:
        raise RuntimeError("Crypto/GmSSL input revision differs from pinned source")
    run(sys.executable, str(source / "tools" / "verify_gmssl_import.py"))
    receipt = root / "build" / "ci-crypto" / "source-receipt.json"
    receipt.parent.mkdir(parents=True, exist_ok=True)
    run("cmake", f"-DGMSSL_UPSTREAM_SOURCE={gmssl}",
        f"-DGMSSL_ASSEMBLY_RECEIPT={receipt}",
        "-P", str(source / "tools" / "assemble_gmssl.cmake"))
    if not receipt.is_file():
        raise RuntimeError("GmSSL immutable provenance receipt missing")
    host_sdk = install_one(root, host, source, gmssl)
    target_sdk = (host_sdk if rid == host else install_one(root, rid, source, gmssl))
    values = {"CRYPTO_HOST_ROOT": str(host_sdk), "CRYPTO_ROOT": str(target_sdk),
              "CRYPTO_SOURCE_SHA": CRYPTO_COMMIT}
    env_file = os.environ.get("GITHUB_ENV")
    if not env_file:
        raise RuntimeError("GITHUB_ENV is required for all downstream owner builds")
    with Path(env_file).open("a", encoding="utf-8") as stream:
        for k, v in values.items():
            stream.write(k + "=" + v + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
