# Handoff

## 2026-11-01 SaltsCore / CMeta split-package cutover

- Configure against exact installed SDK roots `SALTSCORE_ROOT` and
  `CMETA_ROOT`. Do not restore `SALTS_ROOT`, `find_package(Salts)`, default
  prefix search, or aliases for the old monolithic package.
- Native filesystem and process implementations belong to SaltsCore as
  `Salts::FS` and `Salts::Process`. The bounded CFlow adapters in this
  repository are `Salts::CFlowFS` and `Salts::CFlowProcess`, with public
  headers under `include/cflow/`; installing a second `<salts/fs.h>` or
  `<salts/process.h>` silently shadows the Core owner.
- USB moved completely to SaltsCore `Salts::USB`. The former salts-utils
  `cflow-usb` header, source and tests were byte-equivalent apart from line
  endings and were removed together with the vcpkg `usb` feature. Do not add a
  second `cflow_usb_*` owner.
- Playback, Capture, and Serial also belong to SaltsCore. Their duplicate
  SaltsUtils directories, Capture-only presets/features, and Capture TSAN
  workflow were removed. SaltsUtils packaging must not assert that their
  headers or libraries are part of the SaltsUtils payload.
- Restore `SaltsCore.Native` at `0.1.0` and `CMeta.Native` at `0.1.1`.
  The restore result exports target and host forms of both
  roots. CMeta-owned targets use `CMeta::`; Core-owned targets use `Salts::`.
- llvm-mingw can set `CMAKE_CROSSCOMPILING=TRUE` even for a host-runnable
  Windows target. Host tools such as lemon must compare host and target system
  name/processor before demanding a separately supplied executable.
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
  C++17. It includes both FS and Process adapters so the split boundary is not
  left untested.
- Installed-SDK consumer projects and their private presets are not part of the
  supported CI graph. Keep package discovery checks in
  `databind/tests/package_config` and behavior checks in the normal CTest graph;
  otherwise stale consumer presets can silently retain the removed monolithic
  `SALTS_ROOT` contract.
- Admit published SDKs against their actual immutable payload, not a guessed
  umbrella layout. SaltsCore exposes `salts_core/runtime_abi.h` and
  `salts_unicode.h`; it has no `salts/core.h` or
  `unicode/salts_unicode.h`. `Salts::Crypto` is imported by the sibling
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
