#!/usr/bin/env python3
"""Fail-closed source contract for DataBind release candidates.

This check is deliberately narrower than compilation or native sanitizer
qualification. It prevents a published RC from silently omitting the reviewed
private Monocypher / typed-RPC error-discriminator changes.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

PATHS = (
    "vendor/monocypher/CMakeLists.txt",
    "vendor/monocypher/databind_monocypher.c",
    "vendor/monocypher/databind_monocypher_namespace.h",
    "databind/runtime/data_bind_contract_fingerprint.c",
    "databind/runtime/data_bind_method_plan.c",
    "databind/compiler/tests/native/test_service_native_lowering.c",
    "CMakeLists.txt",
    "cmake/SaltsUtilsConfig.cmake.in",
    "jinja/CMakeLists.txt",
    "cmake/ci/restore-native-sdk.sh",
    "cmake/ci/restore-native-sdk.ps1",
    "cmake/ci/github_packages.py",
    ".github/actions/setup-native-ci/action.yml",
    ".github/workflows/native-build.yml",
    ".github/workflows/native-tests.yml",
    ".github/workflows/idl-component-source-gate.yml",
    ".github/workflows/databind-monocypher-asan.yml",
    ".github/workflows/sdk-package.yml",
    "CMakeUserPresets.json",
)

def read_source(root: Path, name: str, commit: str | None) -> str:
    if commit is None:
        return (root / name).read_text(encoding="utf-8")
    return subprocess.check_output(
        ["git", "show", f"{commit}:{name}"], cwd=root, text=True
    )

def violations(source: dict[str, str]) -> list[str]:
    errors: list[str] = []
    cmake = source["vendor/monocypher/CMakeLists.txt"]
    wrapper = source["vendor/monocypher/databind_monocypher.c"]
    namespace = source["vendor/monocypher/databind_monocypher_namespace.h"]
    fingerprint = source["databind/runtime/data_bind_contract_fingerprint.c"]
    method = source["databind/runtime/data_bind_method_plan.c"]
    fixture = source["databind/compiler/tests/native/test_service_native_lowering.c"]
    root_cmake = source["CMakeLists.txt"]
    installed_config = source["cmake/SaltsUtilsConfig.cmake.in"]
    jinja_cmake = source["jinja/CMakeLists.txt"]
    restore_unix = source["cmake/ci/restore-native-sdk.sh"]
    restore_windows = source["cmake/ci/restore-native-sdk.ps1"]
    github_packages = source["cmake/ci/github_packages.py"]
    native_setup = source[".github/actions/setup-native-ci/action.yml"]
    native_build = source[".github/workflows/native-build.yml"]
    native_tests = source[".github/workflows/native-tests.yml"]
    component_gate = source[".github/workflows/idl-component-source-gate.yml"]
    monocypher_gate = source[".github/workflows/databind-monocypher-asan.yml"]
    sdk_package = source[".github/workflows/sdk-package.yml"]
    user_presets = source["CMakeUserPresets.json"]

    if not re.search(
        r"add_library\(\s*databind_monocypher\s+STATIC\s+databind_monocypher\.c\b",
        cmake, re.S,
    ) or re.search(r"add_library\(\s*databind_monocypher\s+STATIC\s+monocypher\.c\b", cmake, re.S):
        errors.append("DataBind must compile the private Monocypher wrapper, not the raw vendor unit")
    if not re.search(
        r'#include\s+"databind_monocypher_namespace\.h"\s*'
        r'#include\s+"monocypher\.c"', wrapper
    ):
        errors.append("Monocypher wrapper must namespace symbols before including vendor source")

    mappings = re.findall(
        r"^#define (crypto_[A-Za-z0-9_]+) (databind_private_crypto_[A-Za-z0-9_]+)$",
        namespace, re.M,
    )
    if len(mappings) != 53 or len({name for name, _ in mappings}) != 53 or any(
        private != "databind_private_" + name for name, private in mappings
    ):
        errors.append("Expected all 53 unique, exact DataBind-private Monocypher mappings")
    if not any(name == "crypto_argon2_no_extras" for name, _ in mappings):
        errors.append("ODR-sensitive crypto_argon2_no_extras must be privately mapped")

    if not re.search(
        r'#include\s+"databind_monocypher_namespace\.h"\s*'
        r'#include\s+"monocypher\.h"', fingerprint
    ):
        errors.append("Contract fingerprint must use the private Monocypher namespace")

    if "left->code == config->errors[j].code" not in method:
        errors.append("Explicit RPC typed-error numeric-code collision guard is absent")
    if "plan->errors[i].code == plan->errors[j].code" not in method:
        errors.append("Post-default RPC typed-error numeric-code collision guard is absent")
    if "RPC typed-error numeric code collision" not in method:
        errors.append("RPC typed-error collision must have a specific SCHEMA diagnostic")
    if "rejects ambiguous RPC typed-error numeric codes before native decoding" not in fixture:
        errors.append("Generated native RPC typed-error collision regression is absent")

    # Paired installed NuGet acceptance exposed the previous partial target-set
    # crash: official Salts already exports Salts::Unicode, while an older
    # SaltsUtils package independently exported the same CMake target.
    if re.search(r"add_subdirectory\(\s*unicode\s*\)", root_cmake):
        errors.append("Local Unicode producer must not be built or exported from SaltsUtils")
    for label, value in (("source", root_cmake), ("installed", installed_config)):
        if "Salts::Unicode" not in value or (
            "get_target_property(_salts_utils_unicode_imported Salts::Unicode IMPORTED)"
            not in value
        ):
            errors.append(f"Canonical Unicode imported target missing from {label} SDK contract")
    if "PRIVATE Salts::Unicode Salts::CSTL" not in jinja_cmake:
        errors.append("Jinja must depend directly on the canonical Salts Unicode owner")
    if 'SALTS_NATIVE_VERSION = "2.3.0-rc.17"' not in github_packages:
        errors.append("curl SDK restore must pin the exact official Salts.Native 2.3 release")
    if "Salts::Unicode" not in github_packages or "salts-sdk-manifest.txt" not in github_packages:
        errors.append("curl SDK restore lacks canonical Unicode/provenance admission")
    if '"curl"' not in github_packages or "dotnet" in github_packages.lower() or "mono-complete" in github_packages.lower():
        errors.append("native package restore must use curl without .NET/Mono")
    for label, value in (("Unix", restore_unix), ("Windows", restore_windows)):
        if "github_packages.py" not in value or "dotnet restore" in value or "mono" in value.lower():
            errors.append(f"{label} SDK restore must delegate to the curl package helper")

    native_ci_files = (
        ("native setup action", native_setup),
        ("native build workflow", native_build),
        ("native test workflow", native_tests),
        ("ComponentProvider source gate", component_gate),
        ("Monocypher sanitizer gate", monocypher_gate),
    )
    forbidden_ci = (
        "setup-vcpkg-cache@",
        "setup-dotnet@",
        "dotnet restore",
        "mono-complete",
        "brew install mono",
    )
    for label, value in native_ci_files:
        for token in forbidden_ci:
            if token.lower() in value.lower():
                errors.append(f"{label} must not reintroduce managed package/cache setup: {token}")
    for label, value in (
        ("native build workflow", native_build),
        ("native test workflow", native_tests),
        ("ComponentProvider source gate", component_gate),
        ("Monocypher sanitizer gate", monocypher_gate),
    ):
        if "./.github/actions/setup-native-ci" not in value:
            errors.append(f"{label} must consume the curl-only native CI action")
    if "cmake/ci/setup_native_ci.py" not in native_setup or "github_packages.py" not in native_setup:
        errors.append("native setup action must bootstrap the curl cache and curl re2c restore")
    if "nugetconfig" in user_presets.lower() or "nugettimeout" in user_presets.lower():
        errors.append("root user presets must not configure a NuGet-backed vcpkg binary source")
    if "dotnet restore" not in sdk_package or "<clear />" not in sdk_package:
        errors.append("release pack restore must be explicit and source-empty")
    if "nuget.pkg.github.com" in sdk_package or "dotnet nuget add source" in sdk_package:
        errors.append("release pack preparation must not read GitHub Packages through dotnet")
    return errors

def exercise_negative_cases(source: dict[str, str]) -> None:
    if violations(source):
        raise AssertionError("Cannot test negative cases against an invalid base source")
    mutations = (
        ("vendor/monocypher/databind_monocypher_namespace.h",
         "#define crypto_argon2_no_extras databind_private_crypto_argon2_no_extras"),
        ("databind/runtime/data_bind_method_plan.c",
         "left->code == config->errors[j].code"),
        ("databind/runtime/data_bind_method_plan.c",
         "plan->errors[i].code == plan->errors[j].code"),
        ("databind/compiler/tests/native/test_service_native_lowering.c",
         "rejects ambiguous RPC typed-error numeric codes before native decoding"),
    )
    for path, token in mutations:
        if token not in source[path]:
            raise AssertionError(f"Missing negative-fixture token in {path}")
        broken = source.copy()
        broken[path] = source[path].replace(token, "", 1)
        if not violations(broken):
            raise AssertionError(f"Source gate failed open after dropping {token}")
    # Negative producer-owner/source-pin regression: never accept a revived
    # local implementation or a floating/stable-2.2 Salts restore.
    for path, old, replacement in (
        ("CMakeLists.txt", "add_subdirectory(jinja)",
         "add_subdirectory(unicode)\nadd_subdirectory(jinja)"),
        ("cmake/ci/github_packages.py", 'SALTS_NATIVE_VERSION = "2.3.0-rc.17"',
         'SALTS_NATIVE_VERSION = "*"'),
    ):
        if old not in source[path]:
            raise AssertionError(f"Missing fail-closed Unicode fixture in {path}")
        broken = source.copy()
        broken[path] = source[path].replace(old, replacement, 1)
        if not violations(broken):
            raise AssertionError(f"Unicode owner gate failed open in {path}")

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", help="Verify files from this exact git commit, not the checkout")
    parser.add_argument("--self-test", action="store_true", help="Also test fail-closed mutations")
    args = parser.parse_args()
    if args.commit and not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        parser.error("--commit requires a full immutable lowercase git SHA")
    root = Path(__file__).resolve().parents[2]
    try:
        source = {name: read_source(root, name, args.commit) for name in PATHS}
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: cannot read exact DataBind release source: {exc}", file=sys.stderr)
        return 1
    errors = violations(source)
    obsolete_nuget_config = "cmake/vcpkg-cache.nuget.config"
    if args.commit:
        present = subprocess.run(
            ["git", "cat-file", "-e", f"{args.commit}:{obsolete_nuget_config}"],
            cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        ).returncode == 0
    else:
        present = (root / obsolete_nuget_config).exists()
    if present:
        errors.append("obsolete tracked NuGet vcpkg cache config must remain deleted")
    for problem in errors:
        print(f"FAIL: {problem}", file=sys.stderr)
    if errors:
        return 1
    if args.self_test:
        exercise_negative_cases(source)
    print("PASS: source contains private Monocypher, RPC typed-error and canonical Unicode SDK contracts"
          + (f" at {args.commit}" if args.commit else " in current checkout"))
    return 0

if __name__ == "__main__":
    sys.exit(main())
