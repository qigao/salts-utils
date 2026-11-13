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
