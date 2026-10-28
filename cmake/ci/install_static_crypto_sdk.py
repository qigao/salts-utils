#!/usr/bin/env python3
"""Install the one SaltsCore-owned ASN1 + Crypto static SDK for SaltsUtils CI.

The exact SaltsCore source commit and pinned GmSSL vcpkg port are immutable
inputs. No separate qigao/crypto or guanzhi/GmSSL source checkout, fallback
provider, alternate Crypto target, Mono/NuGet client or raw runtime state.
This is a downstream migration candidate, not a production TLS cutover.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

SALTSCORE_SOURCE_SHA = "eec19ab953c257a034a8d2075b84f2ba22328406"
VCPKG_PORT_SHA = "65c12e71d02b9b7dacfafc3d188a3bda18184296"
GMSSL_PORT_VERSION = 14
PR43_SOURCE_SHA = "0b4ee094ce8002e6169ce231028107eba63588c6"


def run(*argv: str, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(map(str, argv)), flush=True)
    subprocess.run([str(a) for a in argv], env=env, check=True)


def stage_tarball(repository: str, commit: str, target: Path, work: Path) -> None:
    if target.exists():
        raise RuntimeError("Refusing stale or second source checkout: " + str(target))
    target.mkdir(parents=True)
    archive = work / (repository.split("/")[-1] + ".tar.gz")
    run("curl", "-fLsS", f"https://api.github.com/repos/{repository}/tarball/{commit}",
        "-o", str(archive))
    run("tar", "-xzf", str(archive), "--strip-components=1", "-C", str(target))


def native_toolchain(rid: str) -> tuple[list[str], list[str]]:
    if rid == "windows-x64":
        return ["-G", "Visual Studio 18 2026", "-A", "x64"], []
    cross: list[str] = []
    if rid.startswith("android-"):
        ndk = Path(os.environ["ANDROID_NDK_HOME"])
        toolchain = ndk / "build" / "cmake" / "android.toolchain.cmake"
        if not toolchain.is_file():
            raise FileNotFoundError(toolchain)
        arch = {"android-arm64-v8a": "arm64-v8a", "android-x64": "x86_64",
                "android-x86": "x86"}.get(rid)
        if arch is None:
            raise ValueError("Unsupported Android Crypto RID: " + rid)
        cross.extend([f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
                      f"-DANDROID_ABI={arch}", "-DANDROID_PLATFORM=android-26"])
    elif rid == "ios-arm64":
        cross.extend(["-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=iphoneos",
                      "-DCMAKE_OSX_ARCHITECTURES=arm64",
                      "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"])
    elif rid == "macos-arm64":
        cross.append("-DCMAKE_OSX_ARCHITECTURES=arm64")
    elif rid not in ("linux-x64", "linux-arm64"):
        raise ValueError("Unsupported native Crypto RID: " + rid)
    return ["-G", "Ninja"], cross


def vcpkg_executable() -> Path:
    value = os.environ.get("VCPKG_ROOT") or os.environ.get("QIGAO_VCPKG_ROOT")
    if not value:
        raise RuntimeError("Exact bootstrapped VCPKG_ROOT is required")
    exe = Path(value) / ("vcpkg.exe" if os.name == "nt" else "vcpkg")
    if not exe.is_file():
        raise FileNotFoundError(exe)
    return exe


def host_for_local_tests() -> str:
    """Only run native CTest when host and installed target are identical."""
    return os.environ.get("QIGAO_HOST_RID") or os.environ["QIGAO_SDK_RID"]


def install_one(root: Path, work: Path, source: Path,
                ports: Path, triplet: str, rid: str) -> Path:
    if not triplet:
        raise RuntimeError("Native triplet must be explicit for " + rid)
    sdk = root / "stage" / "crypto" / rid
    if sdk.exists():
        raise RuntimeError("Refusing stale Crypto SDK: " + str(sdk))
    private = work / "build" / rid
    generator, cross = native_toolchain(rid)
    install_root = work / "vcpkg_installed"
    native_env = dict(os.environ, VCPKG_OVERLAY_PORTS=str(ports / "ports"))
    run(str(vcpkg_executable()), "install",
        f"--x-manifest-root={source / 'crypto'}",
        f"--x-install-root={install_root}", f"--triplet={triplet}",
        f"--overlay-ports={ports / 'ports'}", env=native_env)
    gmssl_root = install_root / triplet
    archive = gmssl_root / "lib" / (
        "gmssl.lib" if rid == "windows-x64" else "libgmssl.a")
    if not archive.is_file():
        raise FileNotFoundError("Pinned static GmSSL archive missing: " + str(archive))
    if not (gmssl_root / "share" / "gmssl" / "GmSSLConfig.cmake").is_file():
        raise RuntimeError("Pinned GmSSL package config missing from target triplet")

    common = [*generator, *cross, "-DCMAKE_BUILD_TYPE=Release",
              "-DBUILD_TESTING=OFF", "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
              f"-DCMAKE_INSTALL_PREFIX={sdk}"]
    asn1_build = private / "asn1"
    run("cmake", "-S", str(source / "asn1"), "-B", str(asn1_build), *common)
    run("cmake", "--build", str(asn1_build), "--config", "Release", "--parallel", "2")
    run("cmake", "--install", str(asn1_build), "--config", "Release")

    crypto_build = private / "crypto"
    # Cross compilers can root find_package(ASN1) into an iOS/Android
    # sysroot even though this exact target archive was just installed.
    # This is a separate configure: select only the generated package,
    # never an ambient/host ASN1 implementation or a fallback provider.
    asn1_config = sdk / "lib" / "cmake" / "ASN1" / "ASN1Config.cmake"
    if not asn1_config.is_file():
        raise FileNotFoundError(asn1_config)
    run("cmake", "-S", str(source / "crypto"), "-B", str(crypto_build),
        *common, f"-DCRYPTO_GMSSL_ROOT={gmssl_root}",
        f"-DASN1_DIR={asn1_config.parent}",
        "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=NEVER",
        f"-DCMAKE_PREFIX_PATH={sdk}")
    run("cmake", "--build", str(crypto_build), "--config", "Release",
        "--parallel", "2")
    run("cmake", "--install", str(crypto_build), "--config", "Release")

    ext = ".lib" if rid == "windows-x64" else ".a"
    expected = {sdk / "lib" / ("salts_asn1" + ext if ext == ".lib"
                               else "libsalts_asn1.a"),
                sdk / "lib" / ("salts_crypto" + ext if ext == ".lib"
                               else "libsalts_crypto.a")}
    found = {p for p in sdk.rglob("*") if p.is_file() and
             p.suffix.lower() in (".a", ".lib", ".dll", ".so", ".dylib")}
    if found != expected:
        raise RuntimeError("SDK must contain precisely one ASN1 and one Crypto static "
                           f"archive; missing={expected - found}, extra={found - expected}")
    for package in ("ASN1", "Crypto"):
        cfg = sdk / "lib" / "cmake" / package / (package + "Config.cmake")
        if not cfg.is_file():
            raise FileNotFoundError(cfg)
    if not (sdk / "include" / "crypto" / "tls_auth.h").is_file():
        raise RuntimeError("Pinned mTLS v1.3 API header missing")
    receipt = private / "static-archive-audit.json"
    run(sys.executable, str(source / "crypto" / "tools" / "audit_static_crypto.py"),
        "--prefix", str(sdk), "--report", str(receipt))
    # Installed consumers are configured as independent projects: they see
    # ONLY the installed ASN1/Crypto packages, never a source-tree alias.
    # Exact paths are passed even for Android/iOS, whose sysroots otherwise
    # hide the freshly installed target package during find_package().
    installed_args = [
        *generator, *cross, "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DBUILD_TESTING=ON",
        "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=NEVER",
        f"-DCMAKE_PREFIX_PATH={sdk}",
        f"-DASN1_DIR={sdk / 'lib' / 'cmake' / 'ASN1'}",
        f"-DCrypto_DIR={sdk / 'lib' / 'cmake' / 'Crypto'}",
    ]
    for name, project in (
        ("asn1", source / "asn1" / "tests" / "installed"),
        ("crypto", source / "crypto" / "tests" / "owner"),
    ):
        consumer_build = private / ("installed-" + name)
        run("cmake", "-S", str(project), "-B", str(consumer_build),
            *installed_args)
        run("cmake", "--build", str(consumer_build),
            "--config", "Release", "--parallel", "2")
        if rid == host_for_local_tests():
            run("ctest", "--test-dir", str(consumer_build), "-C", "Release",
                "--output-on-failure", "--no-tests=error")
        else:
            # Cross builds link to real target archives; never run host
            # executables in place of Android/iOS binaries.
            print(f"PASS: {rid} {name} C11/C++17 installed target compile/link; "
                  "cross-target execution NOT tested", flush=True)
    print(f"PASS: {rid} one SaltsCore ASN1/Crypto static SDK", flush=True)
    return sdk.resolve(strict=True)


def main() -> int:
    root = Path(os.environ["GITHUB_WORKSPACE"]).resolve()
    rid = os.environ["QIGAO_SDK_RID"]
    host = os.environ.get("QIGAO_HOST_RID") or rid
    target_triplet = os.environ.get("QIGAO_TARGET_TRIPLET", "")
    host_triplet = os.environ.get("QIGAO_HOST_TRIPLET", "") or target_triplet
    if not target_triplet or not host_triplet:
        raise RuntimeError("Both target/host vcpkg triplets are required")
    if (root / "_ci_crypto").exists() or (root / "_ci_gmssl").exists():
        raise RuntimeError("Legacy independent Crypto/GmSSL source checkout is forbidden")

    work = root / "build" / "ci-crypto"
    work.mkdir(parents=True, exist_ok=True)
    source = work / "salts-core-source"
    ports = work / "vcpkg-cache"
    stage_tarball("qigao/salts-core", SALTSCORE_SOURCE_SHA, source, work)
    stage_tarball("qigao/vcpkg-cache", VCPKG_PORT_SHA, ports, work)
    manifest = json.loads((source / "tools" /
                           "crypto_pr43_retirement_manifest.json").read_text(encoding="utf-8"))
    if manifest["baseline"]["head"] != PR43_SOURCE_SHA:
        raise RuntimeError("Original mTLS source provenance differs from exact PR43")
    run(sys.executable, str(source / "tools" / "check_crypto_module_ownership.py"))
    run(sys.executable, str(source / "tools" / "check_crypto_pr43_handoff.py"))
    port_manifest = json.loads((ports / "ports" / "gmssl" /
                                "vcpkg.json").read_text(encoding="utf-8"))
    if (port_manifest.get("version"), port_manifest.get("port-version")) != (
            "3.2.0", GMSSL_PORT_VERSION):
        raise RuntimeError("GmSSL must be pinned to vcpkg-cache 3.2.0#14")
    run(sys.executable, str(source / "crypto" / "tools" /
                           "stage_mtls_gmssl_overlay.py"), str(ports / "ports" / "gmssl"))

    host_sdk = install_one(root, work, source, ports, host_triplet, host)
    target_sdk = host_sdk if host == rid else install_one(
        root, work, source, ports, target_triplet, rid)
    env_file = os.environ.get("GITHUB_ENV")
    if not env_file:
        raise RuntimeError("GITHUB_ENV is required")
    with Path(env_file).open("a", encoding="utf-8") as stream:
        for k, v in {
            "CRYPTO_HOST_ROOT": str(host_sdk),
            "CRYPTO_ROOT": str(target_sdk),
            "SALTSCORE_CRYPTO_SOURCE_SHA": SALTSCORE_SOURCE_SHA,
        }.items():
            stream.write(k + "=" + v + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
