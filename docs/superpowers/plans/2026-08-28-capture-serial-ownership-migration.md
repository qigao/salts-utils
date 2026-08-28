# Capture And Serial Ownership Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move native capture and serial-port ownership from TurboUtils to TurboParser without changing either C ABI or runtime protocol.

**Architecture:** TurboParser installs `TurboParser::Capture` optionally and `TurboParser::Serial` by default, both consuming installed TurboUtils primitives. TurboUtils then removes its source copies and exports so each component has one package owner.

**Tech Stack:** C11, CMake 3.20+, CMake Presets, vcpkg manifest features, TinyTest, Windows Media Foundation/libyuv/miniaudio, TurboUtils Core/STL.

**Spec:** `docs/superpowers/specs/2026-08-28-capture-serial-ownership-migration.md`

## Global Constraints

- Preserve `turbo_capture.h`, `turbo_serial.h`, every public C symbol, enum value, structure layout, ownership rule, and error result.
- Export only `TurboParser::Capture` and `TurboParser::Serial`; do not create TurboUtils compatibility aliases.
- Keep capture callback views borrowed until callback return and keep serial RX/TX topology SPSC with usable capacity `configured_size - 1`.
- TurboParser uses installed `TurboUtils::Core`, `TurboUtils::STL`, and `TurboUtils::TinyTest`; it must not include TurboUtils source paths.
- Merge TurboParser addition before TurboUtils removal.

---

### Task 1: Add package-contract tests in TurboParser

**Files:**
- Create: `tests/install_consumer/CMakeLists.txt`
- Create: `tests/install_consumer/consumer.c`
- Create: `cmake/VerifyInstalledDeviceComponents.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: installed `TurboUtils_DIR` and the TurboParser build/install tree.
- Produces: configure-time checks and runnable consumers for `TurboParser::Capture` and `TurboParser::Serial`.

- [ ] **Step 1: Add consumers that name the new targets**

```cmake
find_package(TurboParser CONFIG REQUIRED)
add_executable(consume_serial consumer.c)
target_compile_definitions(consume_serial PRIVATE CONSUME_SERIAL=1)
target_link_libraries(consume_serial PRIVATE TurboParser::Serial)
```

When capture is enabled, also require `TurboParser::Capture`, compile a
`turbo_video_mode_fps()` smoke test, and reject an unexpected Capture target
when the feature is disabled.

- [ ] **Step 2: Run the consumer configure before implementation**

Run the `win-release-user` configure and invoke the verification script.
Expected: configuration fails because neither new TurboParser target exists.

- [ ] **Step 3: Register install verification**

Add a `verify_installed_device_components` custom target that installs into a
build-local staging prefix, configures the external consumer with explicit
`TurboParser_DIR` and `TurboUtils_DIR`, builds it, and runs the Windows capture
consumer when capture is enabled.

- [ ] **Step 4: Commit the contract test**

```bash
git add CMakeLists.txt cmake/VerifyInstalledDeviceComponents.cmake tests/install_consumer
git commit -m "test(package): define capture and serial owner contracts"
```

### Task 2: Move the implementations into TurboParser

**Files:**
- Create: `capture/**` from TurboUtils `capture/**`
- Create: `turbo_serial/**` from TurboUtils `turbo_serial/**`
- Modify: `capture/CMakeLists.txt`
- Modify: `capture/README.md`
- Modify: `capture/tests/CMakeLists.txt`
- Modify: `turbo_serial/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `CMakeOptions.cmake`
- Modify: `vcpkg.json`
- Modify: `CMakeUserPresets.json`
- Modify: `ARCHITECTURE.md`

**Interfaces:**
- Consumes: `TurboUtils::Core`, `TurboUtils::STL`, and `TurboUtils::TinyTest`.
- Produces: `TurboParser::Capture`, `TurboParser::Serial`, unchanged installed C headers and DLL/library filenames.

- [ ] **Step 1: Copy both complete module trees**

Copy every tracked file while preserving relative paths. Validate source and
destination file manifests are identical before editing ownership metadata.

- [ ] **Step 2: Change only package ownership metadata**

```cmake
cmake_config_target(turbo_capture
  ALIAS TurboParser::Capture
  EXPORT_NAME Capture)
install(TARGETS turbo_capture
  EXPORT TurboParserTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin)

cmake_config_target(turbo_serial
  ALIAS TurboParser::Serial
  EXPORT_NAME Serial)
install(TARGETS turbo_serial
  EXPORT TurboParserTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

Update capture tests to link `TurboParser::Capture`; retain all serial public
ABI and mock tests unchanged.

- [ ] **Step 3: Move option, feature, and presets**

Add `TURBO_ENABLE_CAPTURE` and vcpkg feature dependencies `miniaudio` and
`libyuv` to TurboParser. Add matching Windows/Linux configure, build, test, and
install capture presets and enable capture in Android dependency profiles.

- [ ] **Step 4: Run focused tests**

```text
ctest --preset win-release-user -R "^(test_serialport|test_turbo_serial_.*)$" --output-on-failure
ctest --preset win-capture-release-user -R "^capture_" --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 5: Commit the new owner**

```bash
git add ARCHITECTURE.md CMakeLists.txt CMakeOptions.cmake CMakeUserPresets.json vcpkg.json capture turbo_serial docs/superpowers
git commit -m "refactor(device): own capture and serial in TurboParser"
```

### Task 3: Remove the old TurboUtils owner

**Files:**
- Delete: `capture/**`
- Delete: `turbo_serial/**`
- Modify: `CMakeLists.txt`
- Modify: `CMakeOptions.cmake`
- Modify: `CMakeUserPresets.json`
- Modify: `vcpkg.json`
- Modify: `cmake/VerifyInstalledPackage.cmake`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `ARCHITECTURE.md`

**Interfaces:**
- Consumes: TurboParser addition from Task 2 as the replacement owner.
- Produces: a TurboUtils package with no Capture/Serial targets, headers, libraries, presets, or capture feature.

- [ ] **Step 1: Remove root and package references**

Delete both subdirectory registrations, install-verification dependencies,
capture option/feature/presets, and installed-consumer branches. Remove the
current architecture statement that assigns serial to TurboUtils.

- [ ] **Step 2: Delete the source trees after target references are gone**

Validate the resolved deletion roots are exactly the two isolated-worktree
directories, then remove the tracked trees with `git rm`.

- [ ] **Step 3: Verify no live ownership references remain**

```text
rg.exe -n "add_subdirectory\\((capture|turbo_serial)\\)|TurboUtils::(Capture|turbo_serial)|TURBO_ENABLE_CAPTURE" CMakeLists.txt CMakeOptions.cmake CMakeUserPresets.json cmake tests vcpkg.json ARCHITECTURE.md
```

Expected: no matches outside dated historical documents.

- [ ] **Step 4: Commit the old-owner removal**

```bash
git add -A capture turbo_serial CMakeLists.txt CMakeOptions.cmake CMakeUserPresets.json vcpkg.json cmake/VerifyInstalledPackage.cmake tests/install_consumer ARCHITECTURE.md
git commit -m "refactor(device): remove capture and serial from TurboUtils"
```

### Task 4: Verify the two-package migration

**Files:**
- Verify only; no production changes expected.

**Interfaces:**
- Consumes: both repository commits and their installed Release SDKs.
- Produces: reproducible build, test, install, consumer, and export-audit evidence.

- [ ] **Step 1: Verify and install TurboUtils**

Run `cmake --fresh --preset win-release-user`, build, full CTest, and
`install-win-release-user` from a Visual Studio developer environment.

- [ ] **Step 2: Verify TurboParser default and capture profiles**

Run default Release configure/build/full CTest/install, then capture Release
configure/build/focused CTest/install and the installed-device verification
target.

- [ ] **Step 3: Audit installed exports**

Search both installed CMake package directories for worktree/source paths,
`TurboUtils::Capture`, `TurboUtils::turbo_serial`, and private platform/vendor
targets. Expected: no matches.

- [ ] **Step 4: Record platform residual risk**

Windows validates the current host backend. Android, Linux, macOS, and iOS
backend compilation/lifecycle checks remain required on their native toolchain
when unavailable locally; no fallback behavior is introduced.
