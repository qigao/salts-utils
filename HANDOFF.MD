# Handoff

## 2026-11-01 SaltsCore / CMeta split-package cutover

- Configure against exact installed SDK roots `SALTSCORE_ROOT` and
  `CMETA_ROOT`. Do not restore `SALTS_ROOT`, `find_package(Salts)`, default
  prefix search, or aliases for the old monolithic package.
- Native filesystem and process implementations belong to SaltsCore as
  `Salts::FS` and `Salts::Process`. Commit `23ce8488` removed the former
  `Salts::CFlowFS` and `Salts::CFlowProcess` adapters from this repository;
  do not restore their directories or targets here.
- USB moved completely to SaltsCore `Salts::USB`. The former salts-utils
  `cflow-usb` header, source and tests were byte-equivalent apart from line
  endings and were removed together with the vcpkg `usb` feature. Do not add a
  second `cflow_usb_*` owner.
- Playback, Capture, and Serial also belong to SaltsCore. Their duplicate
  SaltsUtils directories, Capture-only presets/features, and Capture TSAN
  workflow were removed. SaltsUtils packaging must not assert that their
  headers or libraries are part of the SaltsUtils payload.
- Restore `SaltsCore.Native` at `0.1.2` and `CMeta.Native` at `0.1.3`.
  The restore result exports target and host forms of both
  roots. CMeta-owned targets use `CMeta::`; Core-owned targets use `Salts::`.
- llvm-mingw can set `CMAKE_CROSSCOMPILING=TRUE` even for a host-runnable
  Windows target. Host tools such as lemon must compare host and target system
  name/processor before demanding a separately supplied executable. The shared
  native LLVM toolchain also sets `CMAKE_SYSTEM_NAME` without setting
  `CMAKE_SYSTEM_PROCESSOR`; an empty target processor is an unknown field, not
  evidence of a different ABI, when the system names already match.
- C++ header tests built by llvm-mingw require its `bin` directory on runtime
  `PATH` for `libc++.dll`. Without it, direct launch exits with loader error and
  CTest may appear to hang; this is not a compiler or TinyTest deadlock.
- The full Windows CTest preset also needs `QIGAO_TARGET_TRIPLET` available so
  its vcpkg runtime `bin` path expands to `liblua.dll`. Missing that variable
  can trigger a Windows loader dialog that looks like a hung C++ binding test.
- IDL transaction staging may pass an already-unique staging pathname to the
  renderer. A renderer temporary file must therefore be a unique sibling based
  on the destination directory, not the full staged filename plus another UUID;
  the nested form exceeded the legacy Windows path limit in deep build trees.
- Nested package-config CTests must propagate the exact make program and the
  configured LLVM-MinGW/vcpkg environment. Passing only `CMAKE_TOOLCHAIN_FILE`
  is insufficient because `QigaoVcpkgToolchain.cmake` intentionally requires
  `LLVM_MINGW_TOOLCHAIN_FILE` for Windows product compilation.
- The native reduced graph intentionally enables C++ only when tests are on,
  because the production modules are C11 but public-header tests compile as
  C++17. It contains only the remaining QueryVM, JsonParser, CYaml and
  BehaviorTree providers.
- Installed-SDK consumer projects and their private presets are not part of the
  supported CI graph. The former `behavior-tree/package_config` and
  `databind/tests/package_config` projects were removed; keep product behavior
  in compiled C/C++ CTests and dependency restore behavior in the root package
  contract test. Do not restore standalone installer/consumer projects or the
  removed monolithic `SALTS_ROOT` contract.
- Admit published SDKs against their actual immutable payload, not a guessed
  umbrella layout. SaltsCore exposes `runtime_abi.h` and `unicode.h`; it has
  no `salts/`, `salts_core/`, or compatibility header tree. `Salts::Crypto` is
  imported by the sibling
  `Crypto/CryptoTargets.cmake`, while the other checked Core targets live in
  `SaltsCoreTargets.cmake`.
- SaltsUtils release RIDs must be a subset of the published split dependency
  RIDs. Use the canonical dependency/package RID `android-arm64` (the CMake
  `ANDROID_ABI` remains `arm64-v8a`). Do not restore the former `ios-arm64`
  release profile unless SaltsCore.Native and CMeta.Native first publish a
  matching iOS SDK; substituting `macos-arm64` would be ABI-invalid.
- Close the source nupkg before `os.replace` when rewriting Unix executable
  bits. Unix permits replacement through an open archive, but Windows rejects
  it with `ERROR_ACCESS_DENIED`; keep the cross-platform regression test.
- Published SaltsCore/CMeta Windows SDKs use the LLVM-MinGW UCRT ABI. Every
  SaltsUtils Windows CI producer must use the pinned `setup-llvm-mingw`
  toolchain and `qigao-x64-windows-llvm-mingw` triplet. Activating VSDevCmd
  mixes MSVC with dependency `.a` files and fails at link time on MinGW runtime
  symbols such as `___chkstk_ms` and `__imp__setjmp`.
- `Salts::Crypto` is already part of the admitted SaltsCore SDK. Do not check
  out archived `qigao/crypto` or GmSSL and rebuild a second `CRYPTO_ROOT` in
  SaltsUtils CI. Besides violating the sole-owner contract, the archived
  merge script selects MSVC `lib.exe` flags from host `WIN32` and fails under
  LLVM-MinGW `ar`; downstreams must consume the published Core owner.
- CMeta.Native `0.1.3` admits UUID data providers by stable semantic identity
  instead of callback address. Keep this exact pin: Unix shared-module graphs
  can materialize equivalent callback addresses separately, while `0.1.1`
  rejects the valid DataBind UUID runtime descriptor on Linux and macOS.
- SaltsUtils no longer owns a NuGet packaging project or release publisher.
  Do not reintroduce package preparation, installer/consumer jobs, release-tag
  helpers, or build-artifact handoff into its CI.
- `Salts::SIMD` moved from SaltsCore 0.1.0 to SaltsUtils. The 0.1.2 Core pin is
  required because the previous immutable SDK still exports the same target;
  do not hide a duplicate owner with conditional `if(NOT TARGET ...)` logic.
- Do not restore `verify_databind_release_source.py` or similar source-token
  gates. Dependency restore behavior is the formal
  `saltsutils.github_packages_contract` CTest; product contracts belong to
  compiled C/C++ tests, while workflow syntax is checked separately.
- Public headers no longer use a `salts/` or `salts_*` path/name prefix.
  Functional directories such as `bindings/` remain intact, while formerly
  prefixed headers such as `simd.h`, `behavior_tree.h`, `cron.h`, and
  `selector.h` are installed without compatibility forwarding headers.
- The CYaml benchmark includes implementation files after its own system
  headers so it can interpose allocation calls. On Linux it must define
  `_GNU_SOURCE` before those first headers; defining it inside `cyaml_utf8.c`
  is too late for the amalgamated benchmark translation unit and hides the
  `strtod_l` declaration.
- The formal Markdown CTest deliberately fails when its independent Python
  oracles are absent. Native CI must install the pinned
  `parser/markdown/tests/oracle-requirements.txt` before running CTest; a
  developer machine's preinstalled modules are not evidence that CI has them.

## 2026-11-01 CI migration after migrated-library removal

- After deleting a module directory, update both branches of the root CMake
  graph. Leaving `add_subdirectory(cflow-fs)` or `add_subdirectory(cflow-process)`
  behind makes every platform fail during configure before compiler selection.
- Keep SaltsUtils CI aligned with CMeta/SaltsCore: one eight-RID LLVM matrix,
  pinned `vcpkg-cache` setup actions, configure/build/CTest in the same job,
  Windows LLVM-MinGW x64+arm64, and `obfuscator: false` on every compiler path.
- Automatic `paths` cover actual module directories only. CI/configuration,
  documentation and test-only changes use `workflow_dispatch` when qualification
  is needed; do not broaden every source PR with infrastructure-only triggers.
- PR #732 is stacked on obsolete monolithic-Salts and unrelated IDL branches.
  Only its final commit's byte-identical `tinymock/` and `simd/` module trees
  were imported. Their CMake links must use the current split owners
  `Salts::TinyTest` and `CMeta::CMeta`; do not reapply its `Salts.Native 4.0.0`
  package/preset changes.
