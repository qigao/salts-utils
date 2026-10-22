#!/usr/bin/env python3
"""Fail-closed evidence for the canonical Salts ServiceHost + IDL Provider DSO sanitizer gate."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

EXACT_SALTS = "a51947d241a60bc92e92efa8fd6de1d0bb2fbac2"
EXPECTED_UNITS = {
    "plugin/src": 3, "component-runtime/src": 5, "cmeta/src": 19,
    "platform/src": 6, "utils/src": 20, "concurrency/src": 6,
    "coroutine/src": 3, "cflow/src": 50, "native-io/src": 8,
    "cstl/src": 18, "vendor/sds": 1,
}
REQUIRED_CONSUMER_SOURCES = (
    "databind/compiler/tests/plugin/test_component_provider_candidate_dso.c",
    "databind/compiler/tests/plugin/test_component_provider_candidate_codegen_tool.c",
    "databind/compiler/package_config/databind_target/contract_plugin_business.c",
    "databind/compiler/native_source_ir.c",
    "databind/compiler/component_provider_candidate.c",
    "databind/runtime/data_bind.c",
    "databind/runtime/data_bind_method_plan.c",
)


def fail(message):
    raise SystemExit("canonical Provider sanitizer gate failed: " + message)


def sanitizer_set(mode):
    return {"address", "undefined"} if mode == "asan-ubsan" else {"thread"}


def check_flags(words, mode, description):
    present = set()
    for word in words:
        if word.startswith("-fsanitize="):
            present.update(word.split("=", 1)[1].split(","))
    missing = sanitizer_set(mode) - present
    if missing:
        fail(f"{description}: missing sanitizer flags {sorted(missing)}")


def words_for(entry):
    return entry.get("arguments") or shlex.split(entry["command"])


def source_for(entry):
    path = Path(entry["file"])
    if not path.is_absolute():
        path = Path(entry["directory"]) / path
    return path.resolve()


def object_for(entry, words):
    for i, word in enumerate(words[:-1]):
        if word == "-o":
            path = Path(words[i + 1])
            if not path.is_absolute():
                path = Path(entry["directory"]) / path
            return path.resolve()
    return None


def is_relative(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def audit_compiles(build_dir, roots, mode, required_relpaths=(), expected_counts=None):
    db = build_dir / "compile_commands.json"
    if not db.is_file():
        fail(f"missing compile database {db}")
    entries = json.loads(db.read_text())
    compiled = {}
    checked = 0
    for entry in entries:
        source = source_for(entry)
        words = words_for(entry)
        obj = object_for(entry, words)
        if obj is None or not obj.is_file() or obj.stat().st_size == 0:
            continue
        root = next((r for r in roots if is_relative(source, r)), None)
        if root is None:
            continue
        check_flags(words, mode, f"compile {source}")
        rel = source.relative_to(root).as_posix()
        compiled.setdefault(root, {})[rel] = {
            "object": str(obj),
            "command_sha256": hashlib.sha256(" ".join(words).encode()).hexdigest(),
        }
        checked += 1
    if checked == 0:
        fail(f"no built objects audited under {[str(r) for r in roots]}")
    if expected_counts:
        for root, minimum in expected_counts.items():
            count = len(compiled.get(root, {}))
            if count < minimum:
                fail(f"{root}: only {count} built instrumented source objects; need >= {minimum}")
    for rel in required_relpaths:
        source = (roots[0] / rel).resolve()
        if rel not in compiled.get(roots[0], {}):
            fail(f"required source did not produce an instrumented object: {source}")
    return {"checked_object_count": checked,
            "compiled_sources": {str(root): sorted(values) for root, values in compiled.items()}}


def codemodel(build_dir):
    reply = build_dir / ".cmake/api/v1/reply"
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        fail(f"missing CMake File API index under {reply}")
    index = json.loads(indexes[-1].read_text())
    ref = index.get("reply", {}).get("codemodel-v2")
    if not ref:
        fail("CMake File API codemodel-v2 response missing")
    model = json.loads((reply / ref["jsonFile"]).read_text())
    configs = model.get("configurations", [])
    if len(configs) != 1:
        fail("expected one active CMake configuration")
    return reply, {t["name"]: t for t in configs[0]["targets"]}


def target_detail(build_dir, target_name):
    reply, targets = codemodel(build_dir)
    if target_name not in targets:
        fail(f"missing built target {target_name}")
    target = targets[target_name]
    detail = json.loads((reply / target["jsonFile"]).read_text())
    artifacts = [build_dir / item["path"] for item in detail.get("artifacts", [])]
    if not artifacts or not any(p.is_file() and p.stat().st_size for p in artifacts):
        fail(f"{target_name}: no physical target artifact")
    fragments = detail.get("link", {}).get("commandFragments", [])
    link_text = " ".join(f.get("fragment", "") for f in fragments)
    return detail, artifacts, link_text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", choices=("salts", "consumer"), required=True)
    ap.add_argument("--mode", choices=("asan-ubsan", "tsan"), required=True)
    ap.add_argument("--source-root", required=True, type=Path)
    ap.add_argument("--build-dir", required=True, type=Path)
    ap.add_argument("--evidence", required=True, type=Path)
    ap.add_argument("--consumer-root", type=Path)
    ap.add_argument("--consumer-build", type=Path)
    ap.add_argument("--salts-prefix", type=Path)
    args = ap.parse_args()
    source_root = args.source_root.resolve()
    build_dir = args.build_dir.resolve()
    sha = subprocess.check_output(["git", "-C", str(source_root), "rev-parse", "HEAD"], text=True).strip()
    if sha != EXACT_SALTS:
        fail(f"unexpected Salts SHA {sha}; expected {EXACT_SALTS}")
    if args.phase == "salts":
        report = audit_compiles(build_dir, [source_root], args.mode, expected_counts={
            (source_root / prefix).resolve(): minimum for prefix, minimum in EXPECTED_UNITS.items()
        })
        for target in ("salts", "salts_service_host", "salts_configurator", "cmeta_plugin", "cmeta_cmeta", "cmeta_platform"):
            detail, artifacts, link_text = target_detail(build_dir, target)
            if target == "salts":
                check_flags(shlex.split(link_text), args.mode, "link Salts::Core")
            report.setdefault("targets", {})[target] = {
                "artifacts": [str(p) for p in artifacts],
                "link_sha256": hashlib.sha256(link_text.encode()).hexdigest() if link_text else None,
            }
        required = (
            build_dir / "lib/libsalts.so",
            build_dir / "lib/libsalts_service_host.a",
            build_dir / "lib/libsalts_configurator.a",
        )
        for path in required:
            if not path.is_file():
                fail(f"required source-built runtime artifact missing: {path}")
        report.update({"phase": "salts", "mode": args.mode, "salts_sha": sha,
                       "runtime_artifacts": [str(p) for p in required]})
    else:
        if not all((args.consumer_root, args.consumer_build, args.salts_prefix)):
            fail("consumer phase requires --consumer-root, --consumer-build and --salts-prefix")
        consumer_root = args.consumer_root.resolve()
        consumer_build = args.consumer_build.resolve()
        salts_prefix = args.salts_prefix.resolve()
        report = audit_compiles(
            consumer_build, [consumer_root], args.mode,
            required_relpaths=REQUIRED_CONSUMER_SOURCES)
        for target in ("databind_component_candidate_provider",
                       "test_databind_component_candidate_dso_c11",
                       "test_databind_component_candidate_header_cpp",
                       "databind_component_candidate_codegen_tool"):
            detail, artifacts, link_text = target_detail(consumer_build, target)
            check_flags(shlex.split(link_text), args.mode, f"link {target}")
            if target == "test_databind_component_candidate_dso_c11":
                for needle in ("libsalts_service_host.a", "libsalts_configurator.a", "libsalts.so"):
                    if needle not in link_text:
                        fail(f"consumer link does not use source-built runtime artifact {needle}")
                if str(salts_prefix) not in link_text:
                    fail("consumer link does not point to the per-mode source-built Salts prefix")
            report.setdefault("targets", {})[target] = {
                "artifacts": [str(p) for p in artifacts],
                "link_sha256": hashlib.sha256(link_text.encode()).hexdigest(),
                "link_contains_sanitizer": all(
                    f"-fsanitize={name}" in link_text for name in sanitizer_set(args.mode)),
            }
        report.update({"phase": "consumer", "mode": args.mode, "salts_sha": sha,
                       "salts_prefix": str(salts_prefix),
                       "scope": "source-built Salts runtime plus source-instrumented DataBind/Provider test"})
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(f"sanitizer audit passed: phase={args.phase} mode={args.mode} "
          f"built_objects={report['checked_object_count']}")
    print(f"evidence: {args.evidence}")


if __name__ == "__main__":
    main()
