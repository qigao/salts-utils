# Salts Capability Ownership Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Crypto, CFlow filesystem integration, and CFlow process integration from `qigao/salts` to `qigao/salts-utils`, exposing only `Salts::Crypto`, `Salts::FS`, and `Salts::Process`, with public adapter headers under `include/salts`, strict fail-fast package ownership, and no CMake install-verification machinery in either repository.

**Architecture:** Perform a clean two-repository ownership cutover. First remove the three capabilities and install-verification framework from Salts so the installed Salts dependency exports only the low-level prerequisites. Then make SaltsUtils fail fast against pre-migration Salts packages and migrate Crypto, FS, and Process one capability at a time, preserving runtime C APIs while changing header paths and CMake target identities where specified.

**Tech Stack:** ISO C11, C++17 header-contract tests, CMake 3.27 in SaltsUtils / existing Salts CMake baseline, TinyTest, Salts Platform/Core/CFlow, vendored libecc, local SaltsUtils Monocypher, Windows/Linux/macOS filesystem watcher backends.

**Spec:** `docs/superpowers/specs/2026-09-16-salts-capability-ownership-migration-design.md`

## Global Constraints

- `qigao/salts-utils` becomes the only owner of `Salts::Crypto`, `Salts::FS`, and `Salts::Process`.
- `Salts::CFlowFS` and `Salts::CFlowProcess` are removed completely; do not add aliases or compatibility targets.
- Moved FS/Process public headers live only under `include/salts`; do not add forwarding headers under `include/cflow`.
- Preserve the existing C symbols: `salts_crypto_*`, `cflow_fs_*`, `cflow_fs_watch_*`, `cflow_fs_watch_publisher_*`, and `cflow_process_*`.
- `salts_fs_*` and `salts_process_*` low-level APIs remain in `qigao/salts`.
- SaltsUtils consumes only an installed Salts package selected by `SALTS_ROOT`; no source-tree dependency, default search fallback, alternate prefix fallback, or vendored Salts fallback is allowed.
- Missing/invalid `SALTS_ROOT`, missing required Salts targets, or a pre-migration Salts package that still exports any moved target must terminate configuration immediately.
- Neither repository may contain active CMake install-verification targets, `VerifyInstalled*.cmake` scripts, nested install-consumer/package-consumer fixtures, presets, or CI steps whose purpose is staging-install package verification.
- Normal `install(TARGETS ...)`, package config generation, export sets, and ordinary install presets remain supported.
- Use `rg.exe` for text searches and `fd.exe` for file searches, per repository policy.
- Do not change runtime behavior while moving ownership. Characterization tests move with the implementation and remain authoritative.
- Source baseline for copied Salts capability code is `qigao/salts@b12c3614a21c5926c79d35c59a6b535d596e8a75`.
- The approved migration design is committed in `qigao/salts-utils` on `design/move-salts-capabilities` at or after commit `6d0216f9296b866e1a6aae7565f1529ccc65a4fe`.

---

### Task 1: Cut the moved owners and install-verification framework out of Salts

**Files:**
- Modify: `qigao/salts/CMakeLists.txt`
- Modify: `qigao/salts/README.md`
- Modify: `qigao/salts/cflow/README.md`
- Delete: `qigao/salts/crypto/**`
- Delete: `qigao/salts/cflow-fs/**`
- Delete: `qigao/salts/cflow-process/**`
- Delete: `qigao/salts/cmake/VerifyInstalledPackage.cmake`
- Delete: `qigao/salts/tests/install_consumer/**`
- Delete: `qigao/salts/cstl/tests/install_consumer/**`
- Inspect/remove active references in: `qigao/salts/.github/**`, `qigao/salts/presets/**`, `qigao/salts/CMakeUserPresets.json`

**Interfaces:**
- Consumes: Salts repository baseline `b12c3614a21c5926c79d35c59a6b535d596e8a75`.
- Produces: a Salts package/build tree that still owns `Salts::Platform`, `Salts::Core`, and `Salts::CFlow`, but no longer owns `Salts::Crypto`, `Salts::CFlowFS`, or `Salts::CFlowProcess` and contains no active install-verification framework.

- [ ] **Step 1: Create the source migration branch from the pinned baseline**

```powershell
git fetch origin
git switch --detach b12c3614a21c5926c79d35c59a6b535d596e8a75
git switch -c migration/move-salts-capabilities
```

Expected: `git rev-parse HEAD` prints `b12c3614a21c5926c79d35c59a6b535d596e8a75` before the first migration commit.

- [ ] **Step 2: Capture the RED ownership/verification evidence**

```powershell
rg.exe -n "add_subdirectory\((crypto|cflow-fs|cflow-process)\)|Salts::(Crypto|CFlowFS|CFlowProcess)|verify_installed_package|VerifyInstalledPackage|install_consumer" `
  CMakeLists.txt README.md cflow crypto cflow-fs cflow-process cmake tests cstl .github presets CMakeUserPresets.json
fd.exe -HI "^(install_consumer|VerifyInstalled.*)$" .
```

Expected: matches include the three root `add_subdirectory(...)` calls, the root `verify_installed_package` target, `cmake/VerifyInstalledPackage.cmake`, `tests/install_consumer`, and `cstl/tests/install_consumer`.

- [ ] **Step 3: Remove the three moved targets and the root verification target from `CMakeLists.txt`**

Delete exactly these active ownership registrations:

```cmake
add_subdirectory(crypto)
add_subdirectory(cflow-process)
add_subdirectory(cflow-fs)
```

Delete the complete `verify_installed_dependencies` list, `add_custom_target(verify_installed_package ...)`, and its `set_target_properties(...)` call. Do not replace them with another package-smoke target or script.

- [ ] **Step 4: Delete the moved implementation trees and all live install-consumer fixtures**

```powershell
git rm -r crypto cflow-fs cflow-process
git rm cmake/VerifyInstalledPackage.cmake
git rm -r tests/install_consumer cstl/tests/install_consumer
```

If `fd.exe -HI "^(install_consumer|VerifyInstalled.*)$" .` finds another non-historical build/test fixture outside `docs/superpowers/`, delete that live fixture in the same commit and remove its build/CI caller. Do not delete historical design/plan documents solely because they describe old verification work.

- [ ] **Step 5: Remove active documentation for package verification and moved-target ownership**

In `README.md`, retain normal configure/build/test/install commands, but delete the `verify_installed_package` command, the `tests/install_consumer` link, and the final “安装包验证工程” link.

In `cflow/README.md`, replace active statements that present `Salts::CFlowFS` / `Salts::CFlowProcess` as Salts-owned adapters with a concise boundary statement that filesystem/process adapters are provided by SaltsUtils as `Salts::FS` and `Salts::Process`. Do not introduce compatibility names.

- [ ] **Step 6: Verify the source repository is clean of live ownership/verification hooks**

```powershell
rg.exe -n "Salts::CFlowFS|Salts::CFlowProcess|verify_installed_package|VerifyInstalledPackage" `
  --glob "!docs/superpowers/**" --glob "!.git/**" .
fd.exe -HI "^(install_consumer|package_consumer|VerifyInstalled.*)$" .
```

Expected: no live build/test/package-verification matches. Historical files under `docs/superpowers/` are intentionally excluded.

Then confirm the root build no longer owns Crypto:

```powershell
rg.exe -n "add_subdirectory\(crypto\)|salts_crypto|Salts::Crypto" `
  CMakeLists.txt cmake tests cstl cflow platform concurrency coroutine native-io cnet cmeta cserde cbind utils parser
```

Expected: no active Salts target ownership or consumer dependency on `Salts::Crypto`.

- [ ] **Step 7: Build and test Salts normally**

Windows Release:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user --output-on-failure
```

Linux Release when available:

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --parallel
ctest --preset linux-release-user --output-on-failure
```

Expected: normal Salts build/CTest passes without any install-verification target.

- [ ] **Step 8: Install the clean Salts dependency profile for SaltsUtils**

This is ordinary product installation, not an install-verification flow:

```powershell
cmake --build --preset install-win-release-user --parallel
```

Expected: the configured Salts install prefix is populated and later SaltsUtils configuration sees `Salts::Platform`, `Salts::Core`, and `Salts::CFlow`, but not the three moved targets.

- [ ] **Step 9: Commit the Salts cutover**

```powershell
git add -A
git diff --check
git commit -m "refactor: move utility capability ownership out of salts"
```

---

### Task 2: Make the SaltsUtils package boundary fail fast and remove its install-verification machinery

**Files:**
- Modify: `qigao/salts-utils/CMakeLists.txt`
- Modify: `qigao/salts-utils/cmake/SaltsUtilsConfig.cmake.in`
- Modify: `qigao/salts-utils/playback/tests/CMakeLists.txt`
- Modify: `qigao/salts-utils/jinja/README.md`
- Modify: `qigao/salts-utils/ARCHITECTURE.md`
- Delete: `qigao/salts-utils/playback/tests/VerifyInstalledPlayback.cmake`
- Delete: `qigao/salts-utils/playback/tests/package_consumer/**`
- Delete: `qigao/salts-utils/jinja/test/install_consumer/**`
- Inspect/remove active references in: `qigao/salts-utils/.github/**`, `qigao/salts-utils/presets/**`, `qigao/salts-utils/CMakeUserPresets.json`

**Interfaces:**
- Consumes: clean installed Salts from Task 1.
- Produces: one strict dependency gate used both by the SaltsUtils build tree and installed package config; no live install-verification fixture remains.

- [ ] **Step 1: Create the implementation branch from the approved design/plan branch**

```powershell
git fetch origin
git switch design/move-salts-capabilities
git switch -c migration/move-salts-capabilities
```

Expected: the branch contains the approved spec and this implementation plan before production edits begin.

- [ ] **Step 2: Record the current install-verification RED evidence**

```powershell
rg.exe -n "VerifyInstalled|install_consumer|package_consumer|playback_package_test|Installed Consumer Verification" `
  --glob "!docs/superpowers/**" --glob "!.git/**" .
fd.exe -HI "^(install_consumer|package_consumer|VerifyInstalled.*)$" .
```

Expected: at minimum the current Playback package test/script/consumer and Jinja standalone install consumer are found.

- [ ] **Step 3: Add one explicit build-tree dependency ownership gate**

Immediately after `find_package(Salts CONFIG REQUIRED ...)` in root `CMakeLists.txt`, add:

```cmake
foreach(required_salts_target IN ITEMS Salts::Platform Salts::Core Salts::CFlow)
  if(NOT TARGET ${required_salts_target})
    message(FATAL_ERROR
            "SALTS_ROOT package is missing required target ${required_salts_target}")
  endif()
endforeach()

foreach(pre_migration_target IN ITEMS
        Salts::Crypto
        Salts::CFlowFS
        Salts::CFlowProcess)
  if(TARGET ${pre_migration_target})
    message(FATAL_ERROR
            "SALTS_ROOT points to a pre-migration Salts package that still exports ${pre_migration_target}")
  endif()
endforeach()
```

Keep the existing `NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH` lookup. Do not add an alternate package search path.

- [ ] **Step 4: Make the installed `SaltsUtilsConfig.cmake` use the same fail-fast contract**

Replace the existing `SaltsUtils_FOUND FALSE` / `return()` handling for missing or invalid `SALTS_ROOT` with direct `message(FATAL_ERROR ...)` calls. After `find_dependency(Salts ...)` and before `include("${CMAKE_CURRENT_LIST_DIR}/SaltsUtilsTargets.cmake")`, add the same required-target and pre-migration-target loops from Step 3.

The resulting ordering must be:

```cmake
validate SALTS_ROOT with FATAL_ERROR
find_dependency(Salts ... exact SALTS_ROOT ...)
validate Salts::Platform / Salts::Core / Salts::CFlow
reject Salts::Crypto / Salts::CFlowFS / Salts::CFlowProcess
include(SaltsUtilsTargets.cmake)
```

- [ ] **Step 5: Delete all live SaltsUtils install-verification machinery**

Remove the `playback_package_test` `add_test(...)` block and its package label from `playback/tests/CMakeLists.txt`, then delete:

```powershell
git rm playback/tests/VerifyInstalledPlayback.cmake
git rm -r playback/tests/package_consumer
git rm -r jinja/test/install_consumer
```

Remove Jinja README instructions headed `Installed Consumer Verification`. In `ARCHITECTURE.md`, remove statements that promise installed-consumer verification or staging-prefix verification; retain normal install/export behavior and the strict `SALTS_ROOT` dependency rule.

For any additional non-historical match from the Step 2 audit, remove its live CMake/CI/preset caller and delete the corresponding package-consumer fixture.

- [ ] **Step 6: Verify fail-fast RED with a pre-migration Salts package**

Point `SALTS_ROOT` at an existing pre-migration Salts installation that exports at least one of `Salts::Crypto`, `Salts::CFlowFS`, or `Salts::CFlowProcess`, then run:

```powershell
cmake --fresh --preset win-release-user
```

Expected: configure stops immediately with the explicit `pre-migration Salts package` fatal message. Do not proceed into component configuration.

- [ ] **Step 7: Verify GREEN with the clean Task 1 Salts install**

Restore `SALTS_ROOT` to the normal profile installed by Task 1 and run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target playback_buffer_test playback_contract_test playback_lifecycle_test --parallel
ctest --preset win-release-user -R "^playback_(buffer|contract|lifecycle)_test$" --output-on-failure
```

Expected: configure succeeds and Playback's ordinary tests pass; no package smoke test exists.

- [ ] **Step 8: Verify install-verification files/hooks are gone and commit**

```powershell
rg.exe -n "VerifyInstalled|install_consumer|package_consumer|playback_package_test|Installed Consumer Verification" `
  --glob "!docs/superpowers/**" --glob "!.git/**" .
fd.exe -HI "^(install_consumer|package_consumer|VerifyInstalled.*)$" .
git diff --check
git add -A
git commit -m "build: enforce salts ownership boundary"
```

Expected: no active install-verification match remains.

---

### Task 3: Migrate Crypto into SaltsUtils as `Salts::Crypto`

**Files:**
- Create from pinned Salts baseline: `crypto/README.md`
- Create from pinned Salts baseline: `crypto/include/salts/crypto.h`
- Create from pinned Salts baseline: `crypto/src/crypto_ed448.c`
- Create from pinned Salts baseline: `crypto/src/crypto_sha256.c`
- Create from pinned Salts baseline: `crypto/tests/CMakeLists.txt`
- Create from pinned Salts baseline: `crypto/tests/salts_crypto_test.c`
- Create from pinned Salts baseline: `crypto/tests/salts_crypto_header_cpp_test.cpp`
- Create from pinned Salts baseline: `crypto/vendor/libecc/**`
- Modify: `crypto/CMakeLists.txt`
- Modify: root `CMakeLists.txt`

**Interfaces:**
- Consumes: `Salts::Platform` from installed Salts and local SaltsUtils `monocypher`.
- Produces: build-tree and exported `Salts::Crypto`; unchanged `<salts/crypto.h>` and `salts_crypto_*` ABI/API.

- [ ] **Step 1: Copy the complete Crypto tree from the pinned source commit**

Copy `crypto/**` exactly from `qigao/salts@b12c3614a21c5926c79d35c59a6b535d596e8a75` into the SaltsUtils repository. Preserve the libecc upstream metadata and license files byte-for-byte before changing CMake ownership.

- [ ] **Step 2: Write the destination target ownership before building**

In `crypto/CMakeLists.txt`, preserve source lists, visibility, feature definitions, include directories, and libecc compile definitions, but make the package owner SaltsUtils:

```cmake
cmake_config_target(
  salts_crypto
  ALIAS Salts::Crypto
  FOLDER "crypto"
  EXPORT_NAME Crypto)

target_link_libraries(salts_crypto PRIVATE Salts::Platform monocypher)

install(
  TARGETS salts_crypto
  EXPORT SaltsUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

Install `include/salts/*.h` as before. Move the libecc license destination to `share/licenses/SaltsUtils/libecc` so package ownership is not mislabeled as Salts.

- [ ] **Step 3: Register Crypto in the root build**

Add `add_subdirectory(crypto)` after `add_subdirectory(vendor)` and before higher-level consumers. Do not add an option or fallback copy of Crypto.

- [ ] **Step 4: Update Crypto tests to validate the public target**

Keep the two existing tests and their source bodies. In `crypto/tests/CMakeLists.txt`, link `Salts::Crypto` instead of the implementation target where practical:

```cmake
cmake_add_test(
  salts_crypto_test
  SOURCES salts_crypto_test.c
  LIBS Salts::Crypto Salts::TinyTest
  FOLDER "crypto/tests")

cmake_add_test(
  salts_crypto_header_cpp_test
  SOURCES salts_crypto_header_cpp_test.cpp
  LIBS Salts::Crypto
  FOLDER "crypto/tests")
```

Retain the C++17 target properties exactly.

- [ ] **Step 5: Run the focused Crypto GREEN tests**

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target salts_crypto salts_crypto_test salts_crypto_header_cpp_test --parallel
ctest --preset win-release-user -R "^salts_crypto(_header_cpp)?_test$" --output-on-failure
```

Expected: RFC 8032 Ed448, SHA-256, error-contract, and C++ header-contract tests pass unchanged.

- [ ] **Step 6: Check source ownership and commit**

```powershell
rg.exe -n "Salts::Crypto|SALTS_CRYPTO_BUILDING|salts_crypto_" crypto CMakeLists.txt
rg.exe -n "SaltsTargets" crypto
```

Expected: Crypto references `SaltsUtilsTargets`, never `SaltsTargets`; runtime symbol names remain unchanged.

```powershell
git diff --check
git add CMakeLists.txt crypto
git commit -m "feat: move crypto into salts-utils"
```

---

### Task 4: Migrate CFlow filesystem integration as `Salts::FS` with `include/salts`

**Files:**
- Create from pinned Salts baseline: `cflow-fs/README.md`
- Create/move: `cflow-fs/include/salts/fs.h`
- Create/move: `cflow-fs/include/salts/fs_watch.h`
- Create/move: `cflow-fs/include/salts/fs_watch_publisher.h`
- Create from pinned Salts baseline: `cflow-fs/src/fs.c`
- Create from pinned Salts baseline: `cflow-fs/src/fs_watch.c`
- Create from pinned Salts baseline: `cflow-fs/src/fs_watch_internal.h`
- Create from pinned Salts baseline: `cflow-fs/src/fs_watch_linux.c`
- Create from pinned Salts baseline: `cflow-fs/src/fs_watch_macos.c`
- Create from pinned Salts baseline: `cflow-fs/src/fs_watch_windows.c`
- Create from pinned Salts baseline: `cflow-fs/src/fs_watch_unsupported.c`
- Create from pinned Salts baseline: `cflow-fs/src/fs_watch_publisher.c`
- Create from pinned Salts baseline: `cflow-fs/tests/CMakeLists.txt`
- Create from pinned Salts baseline: `cflow-fs/tests/cflow_fs_test.c`
- Create from pinned Salts baseline: `cflow-fs/tests/cflow_fs_init_failure_test.c`
- Create from pinned Salts baseline: `cflow-fs/tests/cflow_fs_header_cpp_test.cpp`
- Create from pinned Salts baseline: `cflow-fs/tests/cflow_fs_watch_test.c`
- Create from pinned Salts baseline: `cflow-fs/tests/cflow_fs_watch_publisher_test.c`
- Modify: `cflow-fs/CMakeLists.txt`
- Modify: root `CMakeLists.txt`

**Interfaces:**
- Consumes: installed `Salts::Core`, `Salts::CFlow`, `Salts::Platform`, `Salts::TinyTest`; synchronous `<salts_fs.h>`; CFlow reactive API.
- Produces: `Salts::FS`; `<salts/fs.h>`, `<salts/fs_watch.h>`, `<salts/fs_watch_publisher.h>`; unchanged `cflow_fs_*` APIs.

- [ ] **Step 1: Copy the baseline tree, then move only the public headers**

Copy `cflow-fs/**` from the pinned Salts commit. Replace the source public-header layout:

```text
cflow-fs/include/cflow/fs.h
cflow-fs/include/cflow/fs_watch.h
cflow-fs/include/cflow/fs_watch_publisher.h
```

with:

```text
cflow-fs/include/salts/fs.h
cflow-fs/include/salts/fs_watch.h
cflow-fs/include/salts/fs_watch_publisher.h
```

Do not leave forwarding headers or an empty public compatibility directory under `cflow-fs/include/cflow`.

- [ ] **Step 2: Update every self-include to the new public path**

Apply these semantic replacements in `cflow-fs/src`, `cflow-fs/tests`, and `cflow-fs/README.md`:

```text
<cflow/fs.h>                 -> <salts/fs.h>
<cflow/fs_watch.h>           -> <salts/fs_watch.h>
<cflow/fs_watch_publisher.h> -> <salts/fs_watch_publisher.h>
```

Inside `fs_watch_publisher.h`, keep the installed CFlow dependency but change only the moved local header:

```c
#include <salts/fs_watch.h>
#include <cflow/reactive.h>
```

Keep `<salts_fs.h>` unchanged because it is the low-level Salts API.

- [ ] **Step 3: Rename only the public CMake target identity**

In `cflow-fs/CMakeLists.txt`, retain implementation target `salts_cflow_fs` and platform backend selection, but use:

```cmake
cmake_config_target(
  salts_cflow_fs
  ALIAS Salts::FS
  FOLDER "cflow-fs"
  EXPORT_NAME FS)
```

Keep dependencies:

```cmake
target_link_libraries(salts_cflow_fs
  PUBLIC Salts::Core Salts::CFlow
  PRIVATE Salts::Platform)
```

Change installation ownership and header directory:

```cmake
install(TARGETS salts_cflow_fs EXPORT SaltsUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")
```

- [ ] **Step 4: Update tests to link `Salts::FS`**

In `cflow-fs/tests/CMakeLists.txt`, replace every `Salts::CFlowFS` with `Salts::FS`. Keep test names, source lists, language standards, and TinyTest/Platform dependencies unchanged.

- [ ] **Step 5: Register the FS module in the root build**

Add:

```cmake
add_subdirectory(cflow-fs)
```

after the installed Salts package has been validated and after local vendor setup. Do not add feature flags or old-target aliases.

- [ ] **Step 6: Run focused FS tests**

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target `
  salts_cflow_fs `
  cflow_fs_test `
  cflow_fs_header_cpp_test `
  cflow_fs_watch_test `
  cflow_fs_watch_publisher_test --parallel
ctest --preset win-release-user -R "^cflow_fs" --output-on-failure
```

Expected: service lifecycle/cancellation/capacity tests, C++ header contract, watcher behavior, and publisher behavior pass.

- [ ] **Step 7: Enforce the public-path/target cutover and commit**

```powershell
fd.exe -HI "^cflow$" cflow-fs/include
rg.exe -n "Salts::CFlowFS|<cflow/fs|\"cflow/fs" cflow-fs
rg.exe -n "Salts::FS|<salts/fs" cflow-fs
```

Expected: first two commands return no compatibility surface; final command shows the new public target/header usage.

```powershell
git diff --check
git add CMakeLists.txt cflow-fs
git commit -m "feat: move cflow filesystem adapter to salts-utils"
```

---

### Task 5: Migrate CFlow process integration as `Salts::Process` with `include/salts`

**Files:**
- Create from pinned Salts baseline: `cflow-process/README.md`
- Create/move: `cflow-process/include/salts/process.h`
- Create from pinned Salts baseline: `cflow-process/src/process.c`
- Create from pinned Salts baseline: `cflow-process/tests/CMakeLists.txt`
- Create from pinned Salts baseline: `cflow-process/tests/cflow_process_test.c`
- Create from pinned Salts baseline: `cflow-process/tests/cflow_process_header_cpp_test.cpp`
- Modify: `cflow-process/CMakeLists.txt`
- Modify: root `CMakeLists.txt`

**Interfaces:**
- Consumes: installed `Salts::Core`, `Salts::CFlow`, `Salts::Platform`, `Salts::TinyTest`, `<salts_process.h>`, `<cflow/io_native.h>`.
- Produces: `Salts::Process`; `<salts/process.h>`; unchanged `cflow_process_*` API and lifecycle semantics.

- [ ] **Step 1: Copy the baseline process adapter and move its public header**

Copy `cflow-process/**` from the pinned Salts commit, then replace:

```text
cflow-process/include/cflow/process.h
```

with:

```text
cflow-process/include/salts/process.h
```

Do not leave a forwarding `<cflow/process.h>`.

- [ ] **Step 2: Update production/tests/docs to include `<salts/process.h>`**

Replace only references to the moved adapter header. Keep these dependencies unchanged inside the new header:

```c
#include <cflow/io_native.h>
#include <salts_process.h>
```

Do not rename `cflow_process_*` symbols or the `cflow_process` public type.

- [ ] **Step 3: Rename only the public CMake target identity**

In `cflow-process/CMakeLists.txt` use:

```cmake
cmake_config_target(
  salts_cflow_process
  ALIAS Salts::Process
  FOLDER "cflow-process"
  EXPORT_NAME Process)

target_link_libraries(salts_cflow_process
  PUBLIC Salts::Core Salts::CFlow
  PRIVATE Salts::Platform)
```

Install via `SaltsUtilsTargets`, and install `include/salts` rather than `include/cflow`.

- [ ] **Step 4: Update tests to link `Salts::Process`**

In `cflow-process/tests/CMakeLists.txt`, replace `Salts::CFlowProcess` with `Salts::Process`; keep existing test names and C/C++ standard properties unchanged.

- [ ] **Step 5: Register the Process module and run focused tests**

Add root:

```cmake
add_subdirectory(cflow-process)
```

Then run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target `
  salts_cflow_process `
  cflow_process_test `
  cflow_process_header_cpp_test --parallel
ctest --preset win-release-user -R "^cflow_process" --output-on-failure
```

Expected: process I/O, cancellation, termination, close/quiescence, and C++ header-contract tests pass.

- [ ] **Step 6: Enforce the public-path/target cutover and commit**

```powershell
fd.exe -HI "^cflow$" cflow-process/include
rg.exe -n "Salts::CFlowProcess|<cflow/process|\"cflow/process" cflow-process
rg.exe -n "Salts::Process|<salts/process" cflow-process
```

Expected: no compatibility target/header path remains.

```powershell
git diff --check
git add CMakeLists.txt cflow-process
git commit -m "feat: move cflow process adapter to salts-utils"
```

---

### Task 6: Publish the new ownership model in SaltsUtils documentation

**Files:**
- Modify: `README.md`
- Modify: `ARCHITECTURE.md`
- Modify: `crypto/README.md`
- Modify: `cflow-fs/README.md`
- Modify: `cflow-process/README.md`

**Interfaces:**
- Consumes: final target/header contracts from Tasks 3–5.
- Produces: active documentation that names only `Salts::Crypto`, `Salts::FS`, `Salts::Process`, and the `salts/...` adapter headers.

- [ ] **Step 1: Update the repository capability list and CMake example**

Add Crypto, FS, and Process to the SaltsUtils README capability description and CMake linking example:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)

target_link_libraries(app PRIVATE
  Salts::Crypto
  Salts::FS
  Salts::Process)
```

State explicitly that low-level filesystem/process primitives remain in Salts while SaltsUtils owns the higher-level CFlow adapters.

- [ ] **Step 2: Update `ARCHITECTURE.md` ownership and build sections**

The SaltsUtils-owned target list must include:

```text
Salts::Crypto
Salts::FS
Salts::Process
```

The Salts-owned list must continue to include Core/CFlow/Platform but must not include the moved capabilities. Remove existing promises to run installed-consumer/staging-prefix verification. Replace them with normal full CTest plus strict `SALTS_ROOT` dependency-boundary checks.

- [ ] **Step 3: Update migrated module READMEs without changing runtime semantics**

`crypto/README.md`: describe the module as SaltsUtils-owned while retaining `Salts::Crypto` and `<salts/crypto.h>`.

`cflow-fs/README.md`: replace `Salts::CFlowFS` with `Salts::FS`; replace `<cflow/fs*.h>` with `<salts/fs*.h>`.

`cflow-process/README.md`: replace `Salts::CFlowProcess` with `Salts::Process`; replace `<cflow/process.h>` with `<salts/process.h>`.

- [ ] **Step 4: Run active-document contract scans and commit**

```powershell
rg.exe -n "Salts::CFlowFS|Salts::CFlowProcess|<cflow/(fs|fs_watch|fs_watch_publisher|process)\.h>" `
  README.md ARCHITECTURE.md crypto cflow-fs cflow-process
rg.exe -n "install(ed)?[- ]consumer|staging prefix|VerifyInstalled|package_consumer" `
  README.md ARCHITECTURE.md crypto cflow-fs cflow-process jinja playback `
  --glob "!docs/superpowers/**"
```

Expected: no obsolete active contract remains.

```powershell
git diff --check
git add README.md ARCHITECTURE.md crypto/README.md cflow-fs/README.md cflow-process/README.md jinja/README.md playback/tests/CMakeLists.txt
git commit -m "docs: publish utility capability ownership"
```

---

### Task 7: Run repository-wide ownership and forbidden-verification audits

**Files:**
- No new production files.
- Modify only files discovered by the audit if they are live build/test/CI/documentation references that violate the approved spec.

**Interfaces:**
- Consumes: both migration branches after Tasks 1–6.
- Produces: repository-wide evidence that no old target/header compatibility surface or active install-verification mechanism remains.

- [ ] **Step 1: Audit Salts for moved ownership and forbidden verification**

From `qigao/salts`:

```powershell
rg.exe -n "Salts::(Crypto|CFlowFS|CFlowProcess)|add_subdirectory\((crypto|cflow-fs|cflow-process)\)|verify_installed_package|VerifyInstalled|install_consumer|package_consumer" `
  --glob "!docs/superpowers/**" --glob "!.git/**" .
fd.exe -HI "^(crypto|cflow-fs|cflow-process|install_consumer|package_consumer|VerifyInstalled.*)$" .
```

Expected: no live moved-module directory, target ownership, or install-verification fixture remains. A documentation sentence that explicitly says a target moved to SaltsUtils is acceptable only when it does not claim Salts ownership.

- [ ] **Step 2: Audit SaltsUtils for old target names, old header paths, and forbidden verification**

```powershell
rg.exe -n "Salts::CFlowFS|Salts::CFlowProcess|<cflow/(fs|fs_watch|fs_watch_publisher|process)\.h>|verify_installed_package|VerifyInstalled|install_consumer|package_consumer" `
  --glob "!docs/superpowers/**" --glob "!.git/**" .
fd.exe -HI "^(install_consumer|package_consumer|VerifyInstalled.*)$" .
fd.exe -HI "^cflow$" cflow-fs/include cflow-process/include
```

Expected: no matches.

- [ ] **Step 3: Audit the new canonical identities**

```powershell
rg.exe -n "ALIAS Salts::Crypto|EXPORT_NAME Crypto" crypto/CMakeLists.txt
rg.exe -n "ALIAS Salts::FS|EXPORT_NAME FS" cflow-fs/CMakeLists.txt
rg.exe -n "ALIAS Salts::Process|EXPORT_NAME Process" cflow-process/CMakeLists.txt
rg.exe -n "include/salts|DIRECTORY include/salts" crypto cflow-fs cflow-process
```

Expected: exactly one owner for each public target and only `include/salts` installation for moved public headers.

- [ ] **Step 4: Commit any audit-only cleanup**

If Step 1 or Step 2 found a live violating reference, remove it, rerun the exact audit until clean, then commit only those cleanup files:

```powershell
git diff --check
git add -A
git commit -m "chore: remove legacy capability migration hooks"
```

If the audits were already clean, do not create an empty commit.

---

### Task 8: Run full normal build/CTest validation on both repositories

**Files:**
- No production changes expected.

**Interfaces:**
- Consumes: final migration branches and normal Salts installation profiles.
- Produces: local/CI build and CTest evidence only; no staging consumer project and no new CMake verification script.

- [ ] **Step 1: Validate Salts Windows Release**

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user --output-on-failure
```

Expected: full normal Salts suite passes with no moved modules and no install-verification target.

- [ ] **Step 2: Refresh the ordinary Salts dependency installation**

```powershell
cmake --build --preset install-win-release-user --parallel
```

Expected: normal configured Salts install completes. Do not configure a synthetic consumer project afterward.

- [ ] **Step 3: Validate SaltsUtils Windows Release against that exact Salts profile**

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user --output-on-failure
```

Expected: all normal tests pass, including Crypto/FS/Process focused tests.

- [ ] **Step 4: Validate Linux Release where the repository CI/profile is available**

For Salts:

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --parallel
ctest --preset linux-release-user --output-on-failure
cmake --build --preset install-linux-release-user --parallel
```

For SaltsUtils using that installed Salts profile:

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --parallel
ctest --preset linux-release-user --output-on-failure
```

Expected: Linux filesystem watcher/service and process tests pass; unsupported platform fallback is not silently selected when Linux support should be available.

- [ ] **Step 5: Record exact-head CI evidence without adding a new workflow**

Push both migration branches and use the repositories' existing CI only. Do not add an install-smoke workflow. Require the existing normal build/test jobs to pass on the exact branch heads before merge readiness is claimed.

---

### Task 9: Prepare the two PRs and enforce merge order

**Files:**
- PR metadata only; no additional production changes expected.

**Interfaces:**
- Consumes: exact-head GREEN branches from Task 8.
- Produces: two reviewable PRs with explicit cross-repository dependency and merge order.

- [ ] **Step 1: Open the Salts source-cutover PR**

Title:

```text
refactor: move utility capability ownership out of salts
```

Body must state that the PR removes `Salts::Crypto`, `Salts::CFlowFS`, `Salts::CFlowProcess`, their source ownership, and all active install-verification machinery while preserving low-level Core/CFlow/Platform/filesystem/process primitives.

- [ ] **Step 2: Open the SaltsUtils destination PR**

Title:

```text
feat: own crypto fs and process capabilities
```

Body must state the exact new public contract:

```text
Salts::Crypto
Salts::FS
Salts::Process

<salts/crypto.h>
<salts/fs.h>
<salts/fs_watch.h>
<salts/fs_watch_publisher.h>
<salts/process.h>
```

It must explicitly say there are no aliases, forwarding headers, package fallbacks, or install-verification framework.

- [ ] **Step 3: Merge in dependency order**

Merge the Salts source-cutover PR first. Confirm the normal Salts CI on its merge commit is GREEN and the normal install profile used by SaltsUtils has been refreshed. Then re-run/reconfirm SaltsUtils exact-head CI and merge the SaltsUtils PR.

Do not merge SaltsUtils first: its fail-fast guard intentionally rejects a pre-migration Salts package that still exports any moved target.

- [ ] **Step 4: Final post-merge audit**

On both default branches, rerun the Task 7 scans and confirm:

```text
Salts owns:      Platform / Core / CFlow / low-level fs/process primitives
SaltsUtils owns: Crypto / FS / Process
old aliases:     none
forward headers: none
install verify:  none
fallbacks:       none
```
