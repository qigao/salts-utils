# Parser Capability Ownership Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move QueryVM and all format/protocol parser capabilities except UriParser from `qigao/salts` into `qigao/salts-utils`, preserving existing `Salts::*` target identities while making ownership single-source and fail-fast.

**Architecture:** Keep `Salts::UriParser` in Salts because `Salts::CNet` depends on it; physically separate it to `salts/uri/`. Copy the remaining parser/query source tree to `salts-utils/parser/`, switch its install/export set to `SaltsUtilsTargets`, make SaltsUtils reject any installed Salts package that still exports the moved targets, then delete the moved source/export ownership from Salts. Cross-repository dependencies use only the installed Salts package selected by `SALTS_ROOT`.

**Tech Stack:** C11, C++17, CMake 3.20+ / 3.27, re2c, lemon, TinyTest, Salts Core/CSTL/CSerde/UriParser, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-16-parser-capability-ownership-migration.md`

## Global Constraints

- `Salts::UriParser` remains owned and exported by `qigao/salts`.
- QueryVM and every other current `salts/parser` target move to `qigao/salts-utils` with the same installed `Salts::*` target names.
- Salts must never depend on SaltsUtils.
- SaltsUtils consumes only the installed Salts profile selected by `SALTS_ROOT`; preserve `NO_DEFAULT_PATH` and `NO_CMAKE_FIND_ROOT_PATH`.
- No compatibility aliases, forwarding headers, duplicate source copies, source-tree fallback, or implicit alternate package roots.
- Do not add CMake install-verification targets, nested package-consumer fixtures, or staging-install smoke frameworks.
- Preserve runtime parser behavior, C headers, ownership semantics, diagnostics, and error codes.
- Preserve explicit JSON/CSerde composition: `Salts::JsonCSerdeAdapter` depends on `Salts::JsonParser` and installed `Salts::CSerde`; `Salts::JsonParser` does not gain a CSerde dependency.
- `TLVParser`, `LtvParser`, and `SoaParser` consume installed `Salts::UriParser` after migration.
- Remove stale `${PROJECT_SOURCE_DIR}/parser` includes from Salts once the aggregate parser tree is gone.
- Source baseline is `qigao/salts@7ca4981854276854974696600791cf0b017fcfa4`; destination baseline is `qigao/salts-utils@33b1de479cd95dc3eaa1b7995fc34f4d73e058ce`.

---

### Task 1: Establish destination ownership and pre-migration RED gate

**Files:**
- Modify: `qigao/salts-utils/CMakeLists.txt`
- Modify: `qigao/salts-utils/cmake/SaltsUtilsConfig.cmake.in`
- Create: `qigao/salts-utils/parser/**` copied from the pinned Salts baseline except `parser/uri_parser/**`
- Modify: `qigao/salts-utils/parser/CMakeLists.txt`

**Interfaces:**
- Consumes: installed Salts low-level targets, including `Salts::Core`, `Salts::CSTL`, `Salts::CSerde`, and `Salts::UriParser` where required.
- Produces: SaltsUtils-owned `Salts::QueryVM` and parser targets with their existing installed names.

- [ ] **Step 1: Capture RED ownership evidence against the current Salts SDK**

Search the current Salts package/source for the moved targets:

```powershell
rg.exe -n "Salts::(QueryVM|IniParser|JsonParser|JsonCSerdeAdapter|XmlParser|CsvParser|TLVParser|LtvParser|ModbusParser|SoaParser|DotEnvParser|CmdParser|Toon|TomlParser|DateTimeParser|CYaml|CYamlJsonAdapter|Selector)" parser CMakeLists.txt ARCHITECTURE.md README.md
```

Expected: the current Salts tree/export graph still owns these targets, proving the package is pre-migration.

- [ ] **Step 2: Extend the build-tree fail-fast list in SaltsUtils**

In root `CMakeLists.txt`, extend `pre_migration_target` to include every moved target:

```cmake
foreach(pre_migration_target IN ITEMS
        Salts::Crypto
        Salts::CFlowFS
        Salts::CFlowProcess
        Salts::QueryVM
        Salts::IniParser
        Salts::JsonParser
        Salts::JsonCSerdeAdapter
        Salts::XmlParser
        Salts::CsvParser
        Salts::TLVParser
        Salts::LtvParser
        Salts::ModbusParser
        Salts::SoaParser
        Salts::DotEnvParser
        Salts::CmdParser
        Salts::Toon
        Salts::TomlParser
        Salts::DateTimeParser
        Salts::CYaml
        Salts::CYamlJsonAdapter
        Salts::Selector)
```

Do not add `Salts::UriParser` to this list.

- [ ] **Step 3: Mirror the same ownership rejection in the installed package config**

In `cmake/SaltsUtilsConfig.cmake.in`, after loading the exact `SALTS_ROOT` Salts package and before including `SaltsUtilsTargets.cmake`, reject the same moved target list. The installed config must never silently coexist with a pre-migration Salts package.

- [ ] **Step 4: Verify RED against the current pre-migration Salts installation**

```powershell
cmake --fresh --preset win-release-user
```

Expected: configure fails immediately with `pre-migration Salts package` naming the first moved parser/query target found. This failure is intentional until Task 3 produces the clean Salts package.

- [ ] **Step 5: Copy the parser/query source tree to SaltsUtils without UriParser**

Copy from the pinned source commit:

```text
parser/CMakeLists.txt
parser/QUERY_VM_DESIGN.md
parser/simd_scan.h
parser/query_vm/**
parser/ini_parser/**
parser/json_parser/**
parser/xml_parser/**
parser/csv_parser/**
parser/tlv_parser/**
parser/ltv_parser/**
parser/modbus_parser/**
parser/soa_parser/**
parser/dotenv_parser/**
parser/cmd_parser/**
parser/toon/**
parser/toml/**
parser/datetime_parser/**
parser/cyaml/**
parser/selector/**
```

Do not copy `parser/uri_parser/**`.

- [ ] **Step 6: Convert parser aggregate installation to SaltsUtils ownership**

In `parser/CMakeLists.txt`:

- remove `add_subdirectory(uri_parser)` if present;
- keep the remaining component order intact;
- remove `uri_parser` from `SALTS_PARSER_TARGETS`;
- change `EXPORT SaltsTargets` to `EXPORT SaltsUtilsTargets`;
- remove `uri_parser/include` from flat install include directories;
- keep existing target aliases/export names unchanged.

All component CMake files continue to name aliases such as `Salts::QueryVM`, `Salts::JsonParser`, and `Salts::XmlParser`.

- [ ] **Step 7: Register the destination parser tree**

Add `add_subdirectory(parser)` after `vendor`/tool setup and before high-level modules that consume parser targets. `DataBind`, Cron/TBE, Mustache, and other consumers must resolve the local SaltsUtils-owned parser targets rather than the imported pre-migration ones.

- [ ] **Step 8: Commit the destination ownership scaffold**

```powershell
git diff --check
git add CMakeLists.txt cmake/SaltsUtilsConfig.cmake.in parser docs/superpowers
git commit -m "feat: move parser capability ownership to salts-utils"
```

---

### Task 2: Preserve parser dependency boundaries inside SaltsUtils

**Files:**
- Inspect/modify: `qigao/salts-utils/parser/**/CMakeLists.txt`
- Modify only where source-tree assumptions or Salts-owned export-set references remain.

**Interfaces:**
- Consumes: installed `Salts::Core`, `Salts::CSTL`, `Salts::CSerde`, `Salts::UriParser`; local moved parser/query targets.
- Produces: an acyclic destination graph whose public dependencies are valid from an installed SaltsUtils package.

- [ ] **Step 1: Audit all component link dependencies**

```powershell
rg.exe -n "target_link_libraries|PROJECT_SOURCE_DIR|CMAKE_SOURCE_DIR|SaltsTargets|parser/uri_parser" parser
```

Expected: `TLVParser`, `LtvParser`, and `SoaParser` link `Salts::UriParser`; JSON CSerde adapter links `Salts::JsonParser` and `Salts::CSerde`; Toon links local `Salts::JsonParser`; XML/other components link local `Salts::QueryVM` as before.

- [ ] **Step 2: Reject source-tree coupling**

For every moved component, replace any include/link reference that reaches into the Salts source tree with either:

```cmake
Salts::<installed-target>
```

or a path inside the moved component itself. No `${SALTS_SOURCE_DIR}`, `${PROJECT_SOURCE_DIR}/../salts`, or equivalent fallback may remain.

- [ ] **Step 3: Preserve UriParser as imported dependency only**

`parser/tlv_parser/CMakeLists.txt`, `parser/ltv_parser/CMakeLists.txt`, and `parser/soa_parser/CMakeLists.txt` retain `Salts::UriParser` links; no UriParser source is added to SaltsUtils.

- [ ] **Step 4: Verify no component tries to export through Salts**

```powershell
rg.exe -n "EXPORT SaltsTargets|add_subdirectory\(uri_parser\)|uri_parser/include" parser
```

Expected: no matches.

- [ ] **Step 5: Commit dependency-boundary fixes**

```powershell
git diff --check
git add parser
git commit -m "build: isolate moved parsers behind installed salts targets"
```

---

### Task 3: Cut parser/query ownership out of Salts and isolate UriParser

**Files:**
- Modify: `qigao/salts/CMakeLists.txt`
- Create: `qigao/salts/uri/**` from `parser/uri_parser/**`
- Delete: `qigao/salts/parser/**`
- Modify: `qigao/salts/utils/parser/CMakeLists.txt`

**Interfaces:**
- Consumes: current Salts UriParser implementation.
- Produces: Salts package exporting `Salts::UriParser` but no moved parser/query target; CNet continues to consume UriParser locally.

- [ ] **Step 1: Capture Salts RED ownership evidence**

```powershell
rg.exe -n "add_subdirectory\(parser|SALTS_PARSER_TARGETS|Salts::(QueryVM|JsonParser|XmlParser|CsvParser|IniParser)" CMakeLists.txt parser cnet utils
```

Expected: root owns both `parser/uri_parser` and aggregate `parser`, and moved targets are still present.

- [ ] **Step 2: Move UriParser to the standalone `uri/` module**

Copy the complete `parser/uri_parser/**` tree to `uri/**` without changing the public target identity `Salts::UriParser` or installed header path. Update its CMake folder labels from `parser/uri_parser` to `uri` where they are presentation-only.

- [ ] **Step 3: Update root module order**

Replace:

```cmake
add_subdirectory(parser/uri_parser)
...
add_subdirectory(parser)
```

with:

```cmake
add_subdirectory(uri)
```

at the existing early UriParser position. Do not add a SaltsUtils dependency.

- [ ] **Step 4: Remove moved parser/query source ownership**

Delete the complete original `parser/**` tree after `uri/**` exists. There must be no duplicate UriParser copy.

- [ ] **Step 5: Remove stale Salts source include path**

In `utils/parser/CMakeLists.txt`, remove `${PROJECT_SOURCE_DIR}/parser` from both lexer target include lists. Preserve the real `utils/include` and SIMDE dependencies already used by those lexers.

- [ ] **Step 6: Verify the Salts ownership cut**

```powershell
rg.exe -n "Salts::(QueryVM|IniParser|JsonParser|JsonCSerdeAdapter|XmlParser|CsvParser|TLVParser|LtvParser|ModbusParser|SoaParser|DotEnvParser|CmdParser|Toon|TomlParser|DateTimeParser|CYaml|CYamlJsonAdapter|Selector)" `
  --glob "!docs/superpowers/**" --glob "!book/**" .
```

Expected: no active build/source target ownership remains. References in historical docs/book are not part of the executable graph.

Then verify UriParser remains:

```powershell
rg.exe -n "Salts::UriParser|add_subdirectory\(uri\)" CMakeLists.txt uri cnet
```

Expected: UriParser target and CNet dependency remain.

- [ ] **Step 7: Commit Salts cutover**

```powershell
git diff --check
git add -A
git commit -m "refactor: move parser capability ownership out of salts"
```

---

### Task 4: Reconcile architecture/docs and package requirements

**Files:**
- Modify: `qigao/salts/ARCHITECTURE.md`
- Modify: `qigao/salts/README.md`
- Modify: `qigao/salts-utils/ARCHITECTURE.md`
- Modify: `qigao/salts-utils/README.md`
- Modify: `qigao/salts-utils/CMakeLists.txt` required target gate if necessary
- Modify: `qigao/salts-utils/cmake/SaltsUtilsConfig.cmake.in` required target gate if necessary

**Interfaces:**
- Consumes: final target ownership from Tasks 1–3.
- Produces: documentation and package diagnostics that describe one canonical owner per target.

- [ ] **Step 1: Update Salts architecture**

Document UriParser as the low-level URI primitive used by CNet. Remove current statements that Salts owns QueryVM and all format parsers. Do not rewrite historical `docs/superpowers/**` records.

- [ ] **Step 2: Update Salts README capability table**

Replace the aggregate “Parser engines” entry with an UriParser entry or equivalent low-level URI capability statement.

- [ ] **Step 3: Update SaltsUtils architecture and README**

State that SaltsUtils owns QueryVM and the moved parser targets, while UriParser remains in installed Salts. Update the capability list and dependency diagram accordingly.

- [ ] **Step 4: Require UriParser when the moved parser graph needs it**

If root parser configuration is unconditional, include `Salts::UriParser` in SaltsUtils's required Salts target gate in both root and installed config. The exact required set must be identical in both locations.

- [ ] **Step 5: Commit documentation/package contract**

```powershell
git diff --check
git add ARCHITECTURE.md README.md CMakeLists.txt cmake/SaltsUtilsConfig.cmake.in
git commit -m "docs: document parser ownership boundary"
```

---

### Task 5: Verify both repositories and final single-owner package graph

**Files:**
- Modify only if verification exposes a migration defect.
- CI workflow edits are allowed only to point SaltsUtils at the exact Salts cutover commit/cache; do not add a new install-verification framework.

**Interfaces:**
- Consumes: Salts cutover branch and SaltsUtils destination branch.
- Produces: exact-head evidence that both normal test graphs pass and the package target ownership is single-source.

- [ ] **Step 1: Build/test Salts**

Run the normal configured profile(s):

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user --output-on-failure
```

Expected: CNet and UriParser tests pass with no SaltsUtils dependency.

- [ ] **Step 2: Install the post-migration Salts profile**

Use the repository's normal install preset. This is an ordinary product install, not a synthetic consumer test.

Expected exported package properties:

```text
present: Salts::UriParser
absent:  Salts::QueryVM and every moved parser/query target
```

- [ ] **Step 3: Configure SaltsUtils against the post-migration profile**

```powershell
cmake --fresh --preset win-release-user
```

Expected: configuration succeeds. Pointing `SALTS_ROOT` at the old pre-migration package must still fail fast because it exports a moved target.

- [ ] **Step 4: Build/test moved parser graph and full SaltsUtils test graph**

```powershell
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user --output-on-failure
```

Expected: all existing parser/query characterization tests that moved with the source remain green, followed by the normal SaltsUtils suite.

- [ ] **Step 5: Audit final ownership**

Salts:

```powershell
rg.exe -n "Salts::(QueryVM|IniParser|JsonParser|JsonCSerdeAdapter|XmlParser|CsvParser|TLVParser|LtvParser|ModbusParser|SoaParser|DotEnvParser|CmdParser|Toon|TomlParser|DateTimeParser|CYaml|CYamlJsonAdapter|Selector)" `
  --glob "!docs/superpowers/**" --glob "!book/**" .
```

Expected: no active ownership.

SaltsUtils:

```powershell
rg.exe -n "Salts::UriParser" parser CMakeLists.txt cmake/SaltsUtilsConfig.cmake.in
rg.exe -n "EXPORT SaltsTargets|source-tree|add_subdirectory\(.*salts" parser CMakeLists.txt cmake
```

Expected: UriParser appears only as an installed Salts dependency; no Salts export-set ownership or source-tree fallback exists.

- [ ] **Step 6: Run exact-head CI and review failures as migration defects**

Open coordinated PRs only after both branches contain their complete half of the cutover. If SaltsUtils CI needs a Salts package cache key/ref, pin it to the exact Salts cutover commit as in the previous capability migration. Do not claim merge readiness until exact-head CI is green in both repositories.
