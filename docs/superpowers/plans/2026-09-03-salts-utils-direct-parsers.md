# Salts Utils Direct Parsers Implementation Plan

> Historical plan (2026-09-03). The direction to retire DataBind in favor of CBind has been withdrawn. DataBind is part of SaltsUtils and is consumed through `Salts::Databind`; the checked items below retain the historical record.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Rename the TurboParser package to SaltsUtils, remove the aggregate `turbo_parser` facade, and make every remaining utility consume installed `Salts::*` parser components directly.

**Architecture:** Salts remains the sole owner of parser engines, query execution, CSerde, and CBind. SaltsUtils contains higher-level utilities (Cron, Mustache, TBE, Serial, Capture, and optional adapters), exports them in the `Salts::` namespace, and discovers the installed Salts SDK through an exact `SALTS_ROOT`. Legacy DataBind source is retained only for staged retirement and is absent from the active package surface.

**Tech Stack:** C11/C++17, CMake presets, MSVC, CTest, installed Salts CMake package.

**Spec:** User request in this session plus `C:/projects/cpp/turbonet/salts/docs/superpowers/specs/2026-08-28-parser-ownership-migration.md`.

## Global Constraints

- Remove the `turbo_parser/` target and sources; do not replace it with an aggregate `Salts::Parser` compatibility target.
- Replace remaining `TurboParser::*` build/install targets with capability-specific `Salts::*` targets.
- Use only installed Salts parser targets and public headers; no source-tree fallback.
- Validate `SALTS_ROOT` and use `find_package(Salts CONFIG REQUIRED PATHS "$ENV{SALTS_ROOT}" NO_DEFAULT_PATH)`.
- Keep Debug and Release build/install trees isolated under `$ENV{PKG_ROOT}/salts-utils/{debug,release}`.
- Preserve utility runtime behavior while changing package identity and dependency ownership.

---

### Task 1: Package identity and dependency boundary

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `CMakeUserPresets.json`
- Rename/modify: `cmake/TurboParserConfig.cmake.in` to `cmake/SaltsUtilsConfig.cmake.in`
- Modify: `cmake/VerifyInstalledDeviceComponents.cmake`
- Modify: `vcpkg.json`

**Interfaces:**
- Consumes: installed `SaltsConfig.cmake` rooted by `SALTS_ROOT`.
- Produces: `find_package(SaltsUtils CONFIG)` and installed `SaltsUtilsConfig.cmake` with `Salts::*` utility targets.

- [x] Record the current failing identity scan with `rg.exe` for `ROCIDA_ROOT`, `Rocida::`, `TurboParser::`, `TurboParserConfig`, and `turboparser`.
- [x] Change the project/package identity to SaltsUtils and add exact-root Salts discovery.
- [x] Change all presets to `SALTS_ROOT` and `salts-utils/{debug,release}`, including runtime paths and a Release install preset.
- [x] Update installed-package verification to load SaltsUtils and inspect `Salts::*` utility exports.
- [x] Re-run the identity scan and require zero build-system occurrences of old dependency/package markers outside historical plans.

### Task 2: Remove the aggregate facade and retire legacy DataBind

**Files:**
- Delete: `turbo_parser/`
- Modify: `mustache/src/mustache_xml.c`
- Modify: `mustache/include/mustache_xml.h`
- Modify: `mustache/test/test_xml_integration.c`
- Modify: `mustache/examples/xml_example.c`
- Preserve but stop building/exporting: `tbe/data_bind/`
- Modify: `tbe/tbe_compiler/main.c`
- Modify: relevant CMake target link lists and tests.

**Interfaces:**
- Consumes: `Salts::JsonParser`, `Salts::XmlParser`, and `Salts::CBind` public APIs.
- Produces: active utilities with no include or link dependency on `turbo_parser.h`, `TurboParser::Parser`, or legacy DataBind.

- [x] Map each facade type/function used outside `turbo_parser/` to the corresponding Salts public type/function using the facade implementation as the behavioral reference.
- [x] Migrate Mustache XML integration to `xml_parser/xml_parser.h` and `salts_xml_*` ownership/view APIs.
- [x] Migrate `tbe_compiler` parsing to the direct Salts JSON API.
- [x] Remove legacy DataBind and its generated-binding tests from the default build and installed export, preserve its source for staged retirement, and document `Salts::CBind` as the replacement.
- [x] Remove `add_subdirectory(turbo_parser)` and delete the facade directory.
- [x] Require `rg.exe` to find no active production include of `turbo_parser.h`, no active `TurboParser::Parser`, and no `add_subdirectory(turbo_parser)`; legacy DataBind source is excluded from this active-code check.

### Task 3: Utility target namespace and documentation

**Files:**
- Modify: all active `CMakeLists.txt` files defining or consuming utility aliases.
- Modify: `ARCHITECTURE.md`
- Modify: `README.md`
- Modify: active module READMEs and package-consumer examples.

**Interfaces:**
- Consumes: direct Salts parser targets from Task 2.
- Produces: `Salts::Cron`, `Salts::Mustache`, `Salts::TbeSchema`, `Salts::Serial`, `Salts::Capture`, and optional Salts utility targets. `Salts::CBind` comes from the base Salts package; SaltsUtils does not re-export DataBind.

- [x] Replace build-tree aliases and installed export namespace `TurboParser::` with `Salts::`.
- [x] Update active examples and package consumers to `find_package(SaltsUtils CONFIG)` and link the new capability targets.
- [x] Update architecture and README ownership text to distinguish Salts parser engines from SaltsUtils higher-level utilities.
- [x] Run an active-source `rg.exe` scan and require zero `TurboParser::`, `TurboParserConfig`, `TURBOPARSER_ROOT`, and `turbo-parser.git` references outside historical plans.

### Task 4: Configure, build, test, and install both profiles

**Files:**
- Verify: `CMakePresets.json`
- Verify: `CMakeUserPresets.json`
- Verify: installed trees under `$ENV{PKG_ROOT}/salts-utils/debug` and `$ENV{PKG_ROOT}/salts-utils/release`.

**Interfaces:**
- Consumes: `$ENV{PKG_ROOT}/salts/debug` and `$ENV{PKG_ROOT}/salts/release`.
- Produces: tested Debug and Release SaltsUtils SDKs.

- [x] Configure Debug with `cmake --preset win-dev-user --fresh` in the MSVC developer environment.
- [x] Build and test Debug through public build/test presets.
- [x] Install Debug through `install-win-dev-user` and configure an external package consumer.
- [x] Configure Release with `cmake --preset win-release-user --fresh` in the MSVC developer environment.
- [x] Build and test Release through public build/test presets.
- [x] Install Release through `install-win-release-user` and configure an external package consumer.
- [x] Inspect both installed `SaltsUtilsTargets.cmake` files for source-tree paths, `TurboParser::*`, `Rocida::*`, and `TurboParser::Parser`.
