# Salts DataBind Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep DataBind 2.5.1 / ABI 8 available for RulesForge as `Salts::DataBind` while the newer CBind stack remains incomplete.

**Architecture:** Salts owns the concrete parser engines. SaltsUtils restores DataBind as a compatibility component and links a private legacy facade to the concrete Salts parser targets; it does not re-export an aggregate Parser target. DataBind's public functions, values, ownership, schema, query, serialization, and streaming behavior remain unchanged.

**Tech Stack:** C11, CMake presets, MSVC, CTest/TinyTest, installed Salts component packages.

**Spec:** User direction in this session and `docs/superpowers/plans/2026-09-03-salts-utils-direct-parsers.md`.

## Global Constraints

- Preserve `DATA_BIND_VERSION == 20501`, `DATA_BIND_ABI_VERSION == 8`, public function signatures, ownership, errors, and supported JSON/YAML/XML/CSV/binary behavior.
- Export only `Salts::DataBind`; do not restore `TurboParser::Parser` or a new aggregate `Salts::Parser` target.
- Consume installed Salts through the existing exact `SALTS_ROOT` package boundary.
- Keep the parser facade private to the DataBind implementation.
- Verify Debug and Release builds, tests, installs, and an installed package consumer before migrating RulesForge.

---

### Task 1: Restore the DataBind build and contract tests

**Files:**
- Modify: `tbe/CMakeLists.txt`
- Create: `tbe/data_bind/CMakeLists.txt`
- Restore privately: `tbe/data_bind/parser_compat/include/*.h`
- Restore privately: `tbe/data_bind/parser_compat/src/parser_compat.c`
- Modify: `tbe/data_bind/data_bind.h`
- Modify: `tbe/data_bind/data_bind.c`

**Interfaces:**
- Consumes: concrete installed `Salts::*Parser`, `Salts::CYaml`, `Salts::QueryVM`, `Salts::Core`, and `Salts::CSTL` targets.
- Produces: build-tree and installed `Salts::DataBind` with DataBind 2.5.1 / ABI 8.

- [x] Re-enable the existing DataBind tests first and configure to record the expected missing-target failure.
- [x] Restore the private parser compatibility implementation without exporting an aggregate parser target.
- [x] Update DataBind's Core/STL/thread/filesystem/UUID dependencies to Salts public headers and symbols.
- [x] Build the DataBind target and run all existing DataBind, record, pool, typed, and public-API tests.
- [x] Scan active CMake for `TurboParser::`, `Rocida::`, `ROCIDA_ROOT`, and `TURBOPARSER_ROOT`; require zero matches outside historical plans.

### Task 2: Validate and install both SaltsUtils profiles

**Files:**
- Verify: `CMakeUserPresets.json`
- Verify: `cmake/SaltsUtilsConfig.cmake.in`
- Verify: installed `SaltsUtilsTargets.cmake` files.

**Interfaces:**
- Consumes: installed Salts Debug/Release profiles.
- Produces: installed SaltsUtils Debug/Release profiles exporting `Salts::DataBind`.

- [x] Configure, build, and test `win-dev-user`; install with `install-win-dev-user`.
- [x] Configure, build, and test `win-release-user`; install with `install-win-release-user`.
- [x] Verify installed headers and targets contain no source-tree paths and expose `Salts::DataBind`.

### Task 3: Migrate RulesForge without changing its DataBind ABI

**Files:**
- Modify: RulesForge root/module/test/example `CMakeLists.txt` files.
- Modify: RulesForge `CMakeUserPresets.json` and Android dependency presets.
- Modify: direct parser includes and calls in RulesForge sources/examples.
- Preserve: RulesForge `include/rules_forge.h` DataBind API.

**Interfaces:**
- Consumes: `Salts::Core`, `Salts::CSTL`, concrete parser targets, `Salts::DataBind`, and `Salts::Mustache`.
- Produces: RulesForge with no `Rocida::*` or `TurboParser::*` build dependency and unchanged DataBind behavior.

- [x] Move the existing dirty RulesForge migration onto a feature branch without discarding changes.
- [x] Replace package roots/targets and direct JSON/CSV facade calls with Salts component APIs.
- [x] Keep DataBind compile-time version/ABI checks, changing only the linked target to `Salts::DataBind`.
- [x] Configure, build, test, and install RulesForge Debug and Release.
- [x] Review the complete diff, commit only in-scope changes, and report push/merge status separately.
