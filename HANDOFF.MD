# Root CMake ownership and dependency isolation

- All modules belong to the root `SaltsUtils` project. Its C11/C++17,
  extensions-off and PIC settings replace repeated target settings. Public
  compile features and symbol/linker settings remain. The installed
  `cmake/IDL.cmake` helper keeps generated targets' C11 requirement because
  it also runs in consumer projects. Its Plugin `VERSION` argument identifies
  a runtime contract, not a CMake project or library ABI version.
- The unused croar wrapper no longer declares a second project. The target
  helper no longer accepts library VERSION/SOVERSION. No standalone validation
  project was added; qualification uses the existing root presets and tests.
- SaltsBase 0.1.6's installed configuration unconditionally resolves JPEG and
  libyuv. The previously selected CMeta 0.1.6 binary pins that same Base version,
  so changing only the consumer's Base version is invalid. The authorized
  replacement is released Base 0.1.8 plus the repository's declared CMeta
  source SDK at `679710f7e3312811efdd92494744a2499286a6a5`, built against that
  Base using `.github/ci/build-cmeta-sdk.ps1 -Preset llvm-mingw-x64-release-user`.
  CMeta's product version remains 0.1.6; its installed configuration now requires
  Base 0.1.8. This is a declared source SDK composition, not a new CMeta binary
  release or a dependency fallback. The SDK is installed independently under
  `build/dependencies/cmeta-0.1.6-base-0.1.8-679710f7-windows-llvm-mingw-x64`;
  its `sdk-restore-receipt.json` records source and upstream inputs.
- Local `.env` selects Base 0.1.8 and that CMeta SDK and records the immutable
  CMeta ref. Previous SDK directories, credentials and other machine settings
  are preserved. The Base SDK archive was verified during the preceding CNet
  release; its original restoration receipt is
  `../salts-core/build/release-0.1.12/base-sdk-receipt.json`.
- Lua and QuickJS use required package resolution under the selected vcpkg
  triplet's own package directories. A fresh configure has zero JPEG/libyuv
  package lookups and cache entries; no global CMAKE_PREFIX_PATH is injected.
  Keep Media dependency discovery with direct Media consumers.

# Verification

- Used a task-owned worktree with the original pending changes and the cleanup
  applied, the same LLVM-MinGW compiler and vcpkg-cache 0.1.3 installation, the
  released Base 0.1.8 SDK and the declared CMeta source SDK. The original shared
  build caches and other worktrees were not reconfigured.
- Configure: `cmake --fresh --preset native-release-user
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. The actual command additionally wrote a
  JSON CMake trace to `build/cmake-cleanup/trace.jsonl` in this checkout.
- Build: `cmake --build --preset native-release-user --parallel 8` passed.
  `ctest --preset native-release-user --output-junit <report>` passed 188/188
  in 13.84 seconds, with zero JUnit failures/errors/skips. Installation through
  `cmake --build --preset native-release-user --target install` also passed;
  the prefix stayed inside the task worktree.
- Compared clean pre/post generation using the same selected SDKs: all 188
  CTest commands and properties remain identical, and all 606 compilation
  commands retain their source and non-standard flags. Only language extension
  mode changes are expected. The trace has one source project (`SaltsUtils`);
  CMake's five generated compiler probes are not repository projects.
- The first full CI run (`43398528757`) exposed that Unix IDL `strdup` calls
  depended on GNU language mode to reveal their declarations. Android's Linux
  host IDL build failed at the same source file. The root now explicitly selects
  `_POSIX_C_SOURCE=200809L` for C compilation on Unix, also covering compiler
  tests' `fileno` call. The existing compiler source guard accepts this same
  value. Strict C11 remains enabled; no test or error diagnostic is suppressed.
- Evidence is under `build/cmake-cleanup/`: `generation-audit.json`,
  `configure.log`, `build.log`, `ctest.log`, `ctest.xml`, `install.log` and saved
  installed package/export files. Local runtime qualification covers Windows
  LLVM-MinGW x64; other platforms require their formal CI lanes.

# SaltsUtils 0.1.4 release

- The cleanup's final source at `1b369ec81c1b2475d4283a5032bd926d7b374ea8`
  passed all ten profiles in CI run `43399268079`: eight complete native
  CTest/build/install lanes and two Android cross-build/install lanes.
- Version 0.1.4 uses the existing split SDK TAR release contract, with a fresh
  exact-version CI run before publication. CMake and vcpkg product versions
  advance together; Base 0.1.8, CMeta 0.1.6 at the declared immutable ref and
  vcpkg-cache 0.1.3 remain separate dependency identities.
- The historical manual NuGet workflow expects the former preparation/package
  layout. Publish the qualified original `utils-sdk-<profile>-<commit>` TARs
  with a manifest and SHA256SUMS, following the preceding split SDK releases.
  Check installed versions, source/dependency provenance, original artifact
  digests and all native JUnit reports. Do not rebuild, repack or alter old
  release assets, and do not claim Android device execution.


# Empty Service DTO audit at 0.1.4 (2026-11-17)

- Source checkout `7b17781593c4f74e1791f8c8633a66e561b1db85` matches the
  source recorded in TurboRaft's selected SaltsUtils 0.1.4 SDK manifest.
- `databind/runtime/data_bind_binding_plan.c` finds request/response root
  parameters before iterating fields, but marks their parameter ownership
  inside those loops. With zero fields, the final unused-parameter check
  rejects an existing root parameter as outside the Service contract.
- Empty records are explicitly unsupported by the Contract-only Native IR
  validator (`databind/compiler/native_source_ir.c`) and its existing
  `test_projection_registry.c` regression. The Mustache C struct templates
  instead emit an empty struct for an empty message, which is not portable C11.
  Supporting no-argument Services therefore requires a consistent native
  representation contract, generator behavior and root-parameter admission;
  changing only the ownership flag does not complete that support.
- This was a source audit. No production source or `.env` selections changed.
  Existing native-release-user cache still selects Base/CMeta 0.1.4, while
  `.env` selects Base 0.1.8 and CMeta 0.1.6; no stale-cache tests were run and
  no build/test pass is claimed. A later implementation must fresh-configure
  with the selected environment and extend the owning formal test suites.


# SaltsUtils 0.1.5 empty-message refactor

- Empty C records now have one `_empty_storage` byte and zero logical fields in
  both NativeSourceIR and Binary-backed C generation. C11/C++17 layout agrees;
  canonical MessagePlan decode/encode emits only MAP_BEGIN/MAP_END. Nonempty
  record layouts and existing Service function signatures are preserved.
- NativeSourceIR units bind the existing canonical complete Struct value.
  Empty values have no field projection or state overlay. CMeta's bounded
  reflection contract requires a nonempty field projection; changing that SDK
  contract is unnecessary for a complete zero-field value. Metadata still
  borrows the caller-owned binder at a stable address until plan destruction.
- Service roots are claimed before field iteration. The request remains owned
  by `frame.request`, rather than being initialized again through parameter
  slots. Response roots retain their existing OUT-slot ownership. Unclaimed
  parameters still fail admission.
- Adding the empty-record template exposed a Mustache bytecode bug: insertion
  changes the instruction-buffer length before the encoded jump width was
  measured. Cache the jump length/width before insertion for ordinary and
  inverted sections. The new boundary regression failed before this fix and
  passed afterwards; it checks body lengths 0..300 with empty/nonempty lists.
- Formal coverage extends existing targets with Native IR C11/metadata checks,
  generated Message C/C++ layout and token round trips, and generated HTTP/RPC
  Service plans for Empty->Empty (typed error), Empty->Status and Status->Empty.
- Loaded only the existing `.env` dependency selections: Base 0.1.8, declared
  CMeta 0.1.6 source SDK and vcpkg-cache 0.1.3. No `.env` fields or SDK roots were
  changed. Product/CMake and vcpkg versions are 0.1.5; obsolete OLD policies are
  removed, with no public ABI epoch or compatibility branch added.
- `cmake --fresh --preset native-release-user`, the complete
  `cmake --build --preset native-release-user`,
  `ctest --preset native-release-user --output-on-failure --output-junit empty-dto-ctest.xml`
  and `cmake --build --preset install-native-release-user` passed locally.
  CTest: 188/188, no failures/errors/skips, 6.07 seconds. The formal Plugin DSO
  regression verifies that request-root storage remains separate from slots.
- Release qualification uses the existing ten-profile split SDK CI and original
  SDK TAR bytes. This commit's remote CI and publication receipts are recorded
  under ignored `build/` after qualification; publication must not rebuild or
  repack producer SDKs. Android qualification remains cross-build/install only.

# Slate native build

- `slate.json` and the owning modules’ manifests declare the Jinja and IDL
  grammar generators, generated DataBind fixtures, and the native test suite.
  Use `slate test --preset tests --list` to compare test registration with the
  existing CMake suite. The `benchmark` preset includes the two registered
  benchmarks.
- TinyTest auto-detects terminal output, so OpenIDEA's captured Run output
  needs TinyTest's `--color` argument. The six Mustache suites declare it in
  `test.args`. All six passed with their original ANSI status colors under
  redirected stdout using the `debug-mingw` preset.
- Extract re2c and Lemon from the package archives verified by
  `cmake/re2c.lock.json` and `cmake/lemon.lock.json`. Set `SLATE_RE2C_ROOT` and
  `SLATE_LEMON_ROOT` to the package roots. Both host tools resolve using
  `tools/<host-rid>` independently of the target SDK architecture.
- On a clean Windows build, code generation must wait for `salts-idlc` and
  its staged runtime DLLs. The Slate Ninja task dependency on the tool’s
  module alias fixes a missing-DLL start failure that can otherwise be masked
  by another `libsalts.dll` on `PATH`. Verified with a serial Ninja build
  with the installed Slate directory excluded from `PATH`.
- `debug-msvc` uses `VCPKG_INSTALLED_DIR_MSVC`, `SALTS_BASE_ROOT_MSVC`, and
  `CMETA_ROOT_MSVC`. The LLVM-MinGW cache contains only its own triplet, and
  the shared MSVC cache lacks the Lua and QuickJS ports used here. The local
  MSVC preset uses the SaltsUtils manifest's `vcpkg_installed/x64-windows`
  tree with SaltsBase 0.1.8 and the pinned CMeta 0.1.8 source built in Debug.
  This resolved both the missing triplet and the CRT mismatch from Release
  SDKs. The MSVC build and the six Lua, QuickJS, and Mustache suites passed.
