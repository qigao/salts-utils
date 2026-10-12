#!/usr/bin/env python3
"""Fail-closed source and link evidence for #637 (Salts P3.4, Linux only).

This is an independent Salts source build, NOT proof that the SaltsUtils IDL
tests have stopped consuming the prebuilt Salts.Native SDK. Keep that later
cross-project migration gate separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

EXACT_SALTS = "540e1da7c2448f15f04aeaaee1106f73a2fbcc7e"
MODULES = {
    "plugin": (
        "plugin/src/plugin.c",
        "plugin/src/plugin_registry.c",
        "plugin/src/plugin_posix.c",
    ),
    "component-plugin": (
        "component-plugin/src/component_plugin.c",
        "component-plugin/src/component_plugin_runtime.c",
    ),
    "component-runtime": (
        "component-runtime/src/configurator.c",
        "component-runtime/src/service_host.c",
        "component-runtime/src/service_host_runtime.c",
    ),
    "cmeta": (
        "cmeta/src/abi.c",
        "cmeta/src/cmeta.c",
        "cmeta/src/manifest_view.c",
        "cmeta/src/function.c",
    ),
    "platform": (
        "platform/src/clock.c",
        "platform/src/fastpath.c",
        "platform/src/readiness.c",
    ),
    "cnet": (
        "cnet/src/cnet_client.c",
        "cnet/src/cnet_owner.c",
        "cnet/src/cnet_tls.c",
        "cnet/src/cnet_manager.c",
    ),
}
MINIMUM_SOURCE_UNITS = {
    "plugin": 3,
    "component-plugin": 2,
    "component-runtime": 3,
    "cmeta": 18,
    "platform": 5,
    "cnet": 30,
}
LINKED_TARGETS = {
    "cmeta_native_service_host_candidate_generation_test": (
        "libsalts_service_host_candidate.a",
        "libcmeta_plugin.a",
        "libcmeta_cmeta.a",
        "libcmeta_platform.a",
    ),
    "cmeta_native_component_plugin_generation_test": (
        "libsalts_component_plugin.a",
        "libcmeta_plugin.a",
        "libcmeta_cmeta.a",
        "libcmeta_platform.a",
    ),
    "cmeta_native_component_plugin_dso_test": ("libcmeta_plugin.a",),
    "salts_service_host_candidate_provider_fixture": (),
    "cmeta_component_plugin_provider_fixture": (),
    "cnet": (),
}


def fail(message):
    raise SystemExit("P3.4 source evidence failed: " + message)


def sanitizer_present(args, name):
    return any(
        arg.startswith("-fsanitize=") and name in arg.split("=", 1)[1].split(",")
        for arg in args
    )


def check_flags(words, mode, description):
    expected = ("address", "undefined") if mode == "asan-ubsan" else ("thread",)
    for name in expected:
        if not sanitizer_present(words, name):
            fail(f"{description}: missing -fsanitize={name}")


def codemodel(build):
    reply = build / ".cmake/api/v1/reply"
    indexes = sorted(reply.glob("index-*.json"))
    if len(indexes) != 1:
        fail("expected one CMake File API index")
    index = json.loads(indexes[0].read_text())
    model_ref = index.get("reply", {}).get("codemodel-v2")
    if not model_ref:
        model_ref = next(
            (item for item in index.get("objects", [])
             if item.get("kind") == "codemodel"), None
        )
    if not model_ref:
        fail("CMake File API codemodel-v2 query missing")
    model = json.loads((reply / model_ref["jsonFile"]).read_text())
    configs = model.get("configurations", [])
    if len(configs) != 1:
        fail("expected one Ninja configuration")
    return reply, {t["name"]: t for t in configs[0]["targets"]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--salts-root", required=True, type=Path)
    ap.add_argument("--build-dir", required=True, type=Path)
    ap.add_argument("--mode", choices=("asan-ubsan", "tsan"), required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    root, build = args.salts_root.resolve(), args.build_dir.resolve()
    actual = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != EXACT_SALTS:
        fail(f"unexpected Salts source SHA {actual}")
    db = build / "compile_commands.json"
    if not db.is_file():
        fail(f"missing {db}")
    entries = json.loads(db.read_text())
    units = {module: {} for module in MODULES}
    for entry in entries:
        src = (Path(entry["directory"]) / entry["file"]).resolve()
        try:
            rel = src.relative_to(root).as_posix()
        except ValueError:
            continue
        module = rel.split("/", 1)[0]
        if module not in MODULES or not rel.startswith(module + "/src/"):
            continue
        cmd = entry.get("arguments") or shlex.split(entry["command"])
        check_flags(cmd, args.mode, f"compile {rel}")
        units[module].setdefault(rel, []).append(
            hashlib.sha256(" ".join(cmd).encode()).hexdigest()
        )
    for module, mandatory in MODULES.items():
        missing = sorted(set(mandatory) - set(units[module]))
        if missing:
            fail(f"{module}: missing actual compilation of {missing}")
        if len(units[module]) < MINIMUM_SOURCE_UNITS[module]:
            fail(f"{module}: compiled only {len(units[module])} distinct units")
        print(f"source instrumented: {module} {len(units[module])} sources ({args.mode})")

    reply, targets = codemodel(build)
    linked = {}
    for name, libs in LINKED_TARGETS.items():
        target = targets.get(name)
        if not target:
            fail(f"missing actual CMake target {name}")
        detail = json.loads((reply / target["jsonFile"]).read_text())
        link = detail.get("link")
        if not link:
            fail(f"{name}: target has no linker command")
        fragment = " ".join(f.get("fragment", "")
                            for f in link.get("commandFragments", []))
        check_flags(shlex.split(fragment), args.mode, f"link {name}")
        for lib in libs:
            if lib not in fragment:
                fail(f"link {name}: missing first-party {lib}")
        linked[name] = hashlib.sha256(fragment.encode()).hexdigest()
        print(f"link instrumented: {name} ({args.mode})")

    # A File API view describes intended compilation/linking. The CI job
    # independently builds every listed target and executes real CTests.
    output = {
        "source_repository": "qigao/salts",
        "source_sha": actual,
        "mode": args.mode,
        "evidence_scope": "independent first-party Salts build; NOT IDL full-source link",
        "module_sources": {m: dict(sorted(v.items())) for m, v in units.items()},
        "linked_targets": linked,
        "limits": [
            "SaltsUtils IDL candidate tests still use the pinned prebuilt SDK",
            "CNet compiles as first-party source but is not in ServiceHost linked closure",
            "no full-source SaltsUtils Provider/Host runtime acceptance claimed",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, sort_keys=True, indent=2) + "\n")


if __name__ == "__main__":
    main()
