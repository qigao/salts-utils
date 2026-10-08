# SaltsUtils

**Higher-level utilities for the Salts C11 ecosystem.**

SaltsUtils builds on the installed [Salts](https://github.com/qigao/salts) SDK and extends its shared semantics with parsers, QueryVM, crypto, filesystem/process adapters, templates, Unicode support, media helpers, IDL/Schema tooling, language bindings, and DataBind.

**IDL, Schema and DataBind are distinct SaltsUtils capabilities.** IDL defines contracts, Schema defines Data shape, and DataBind owns logical/native binding. They are not separate packages.

SaltsUtils does not create a second runtime. CMeta remains the semantic type foundation, CFlow remains the execution/dataflow foundation, CSTL remains the concrete container layer, and Platform/Core remain owned by Salts.

**Tags:** C11 · utilities · parsers · query-engine · crypto · filesystem · process · templates · unicode · data-binding · code-generation

## Built on Salts

SaltsUtils reuses Salts instead of reimplementing its low-level contracts:

- **CMeta** for type identity, metadata, traits, ranges, interfaces, and shared semantic descriptors.
- **CFlow** for typed stream/reactive composition and bounded asynchronous adapters.
- **CSTL** for concrete typed container storage.
- **CSerde** for the canonical format-neutral token contract.
- **Platform / Core** for operating-system primitives, memory, strings, files, processes, and common runtime facilities.

This keeps higher-level utilities compatible with the same explicit ownership, bounded state, lifecycle, and error model used across the wider Salts ecosystem.

## Role in the ecosystem

```text
Salts
  ├── salts-utils
  │     ├── parsers / QueryVM / crypto / filesystem / process
  │     ├── templates / Unicode / media / helpers
  │     ├── IDL / Schema / salts-idlc
  │     ├── DataBind binding / plans / native kernel
  │     └── C++ / Lua / QuickJS bindings
  └── salts-net: protocol and network tooling
```

SaltsUtils is the general-purpose extension layer. Protocol networking belongs in [salts-net](https://github.com/qigao/salts-net). `Salts::IDL` owns transport-neutral contracts, `Salts::Schema` owns the Data subset, and `Salts::DataBind` owns binding. Binary is a format/backend, not the owner of IDL or Schema.

## Main capabilities

| Area | Public capability |
| --- | --- |
| Crypto | `Salts::Crypto` |
| Plugin | `Salts::PluginABI` / `Salts::Plugin` are consumed from Salts 1.8 |
| Filesystem | `Salts::FS` |
| Process adapters | `Salts::Process` |
| Query | `Salts::QueryVM` |
| Parsers | JSON, XML, YAML, CSV, INI, TLV/LTV, Modbus, SOA, DotEnv, Cmd, TOON, TOML, DateTime, and related component targets |
| Templates | Mustache and Jinja CMeta |
| Unicode | generated Unicode property/scalar support |
| Media/helpers | Playback, Capture, Serial, Cron, and related utilities |
| IDL / Schema | `Salts::IDL`, `Salts::Schema`, `salts-idlc` |
| DataBind | `Salts::DataBind`; native/dynamic binding, immutable plans, rollback |
| Language bindings | `Salts::BindingsCpp`, `Salts::Lua`, `Salts::QuickJS` (QuickJS-NG) |

Parser capabilities remain independent component targets rather than a single aggregate parser facade.

Capture and Playback are enabled together by `SALTS_UTILS_ENABLE_CAPTURE`. When disabled, neither component's libraries, headers, or tests are added to the build/install graph. The `salts-idlc` compiler is always built and installed with DataBind.

## Ownership boundaries

```text
CMeta    native type/function/interface truth                 (Salts)
CSerde   canonical token truth                               (Salts)
CFlow    execution truth                                     (Salts)
Plugin   module/loading/lease/lifecycle truth                 (Salts)

IDL      Data + Service + Channel + Component contract truth (SaltsUtils)
Schema   Data-only logical shape -> CMeta projection          (SaltsUtils)
DataBind logical/native binding + BindingPlan runtime         (SaltsUtils)
```

Compiler projections consume one typed `IdlContract`. Binary wire facts
live in `databind_binary_format_plan` and lower through BinaryLayoutIR;
transport state remains in transport runtimes.
DataBind does not own parser syntax, network sessions, Plugin loading or CFlow
execution.


## CMake

This integration branch requires Salts 3.x. It is development work, not a new SaltsUtils release; the next release version will be assigned after the remaining work is complete. Build against the latest published Salts SDK with a matching build profile through `SALTS_ROOT`; both the source build and installed package reject Salts 2.x. Generated fixed arrays use its canonical CMeta array provider and element lifecycle traits. Consumers explicitly select the SaltsUtils installation through `SALTS_UTILS_ROOT`:

```cmake
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)

target_link_libraries(app PRIVATE
  Salts::JsonParser
  Salts::XmlParser
  Salts::Crypto
  Salts::Plugin
  Salts::FS
  Salts::Process
  Salts::Playback
  Salts::Mustache
  Salts::JinjaCMeta
  Salts::Unicode
  Salts::Cron)
```

The package is fail-fast by design. It does not silently search unrelated prefixes, source trees, compatibility shims, or fallback implementations when the required installed Salts profile is missing.

SaltsUtils does not discover Lua or QuickJS for consumers of unrelated modules.
Applications linking `Salts::Lua` must explicitly call
`find_package(unofficial-lua CONFIG REQUIRED)`; applications linking
`Salts::QuickJS` must call `find_package(qjs CONFIG REQUIRED)`.
The binding targets and their runtime link dependencies remain available.

### Restore the published SDK and shared dependency cache

Local user presets use the same [vcpkg-cache](https://github.com/qigao/vcpkg-cache) toolchain as Salts. Keep its checkout at `%LOCALAPPDATA%/qigao/vcpkg-cache` on Windows or `$HOME/.cache/qigao/vcpkg-cache` on Linux. The shared GitHub Packages binary cache is read-only; the default local vcpkg cache remains writable. Overlay ports come from that checkout. CI inherits the cache action's environment and uses the same pinned action revision as Salts.

Before configuring, provide `PROJECT_ROOT`, `VCPKG_ROOT`, and a `GITHUB_TOKEN` with `read:packages` in the parent environment. `PROJECT_ROOT/external/pkgs` is the SaltsUtils install location; dependency SDKs are restored separately. The tracked NuGet configuration references the token through the environment and does not contain credentials.

The restore commands require .NET SDK 8. Windows also requires PowerShell 7 and the Visual Studio developer environment:

```powershell
./cmake/ci/restore-native-sdk.ps1 -SaltsRid windows-x64 -Re2cRid windows-x64 -Local
cmake --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

On Linux, install Python 3 and Mono for package/cache tooling, then run:

```bash
bash cmake/ci/restore-native-sdk.sh linux-x64 linux-x64 0 local
source build/native-sdk/env.sh
cmake --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user --output-on-failure
```

Both restore paths request the **latest stable Salts.Native** from GitHub Packages and the latest published re2c tools. Salts is not version-pinned: each restore uses `Version="*"` with `--no-cache --force-evaluate` to resolve the current release. Run restore before configure when updating dependencies; configure consumes the resolved SDK and does not download packages. An incompatible release must fail rather than select an older SDK. `-WithTurboWasm` (PowerShell) or the third argument `1` (Bash) also restores TurboWasm. The default local package directory is `stage/nuget`; `QIGAO_NUGET_PACKAGES` selects another cache. Package paths come from NuGet's resolved assets, so older cached versions do not affect selection. Local Windows/Linux Release presets use version-independent links at `stage/dependencies/salts/<RID>` and `stage/dependencies/re2c/<host-RID>`. Restore updates these links from NuGet's resolved assets (Windows junctions, Unix symbolic links), so an already running IDE can configure without inheriting new SDK environment variables. Existing non-link directories at these paths are rejected instead of overwritten. CI, Debug, and cross-compilation presets retain their explicit environment-root contract. The published Salts SDK contains Release libraries; use Release presets with it. Debug profiles require a matching Debug SDK. For Android, restore `android-arm64-v8a` as the target RID and the native host RID as the re2c RID before using the Android preset.

### Migrating from Salts 1.x

Rebuild SaltsUtils and its consumers against the same complete Salts 3.x SDK, and regenerate IDL artifacts with the updated `salts-idlc`. Salts Core and Plugin symbols now use `cmeta_*` / `CMETA_PLUGIN_*`; coroutine symbols use `coro_*`. SaltsUtils-owned `salts_*` names and Lua/QuickJS method calls retain their names. This integration changes the required SDK major; the DataBind ABI remains 10. Do not combine an existing SaltsUtils installation built against Salts 2.x with Salts 3.x. Rollback restores the complete previous Salts/SaltsUtils installation and regenerates consumers with its matching host tools. CI artifacts from this branch may qualify the integration; they do not establish a published or release-ready SaltsUtils SDK.

Native receiver metadata uses `cmeta_receiver_operation` and references the canonical `cmeta_function_abi_desc`; the function descriptor is obtained through `operation->abi->function`. Object providers use `cmeta_object_operation_provider`. Update consumer-authored metadata to this layout instead of retaining a separate function descriptor in each receiver entry. This migration changes the native API/ABI and requires recompilation; it does not change DataBind wire formats.

### DataBind consumption

The exact public consumption target is **`Salts::DataBind`**:

```cmake
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE Salts::DataBind)
```

SaltsUtils exports the actual runtime and owns its internal dependency closure. Consumers do not assemble internal Core/CMeta/CFlow/format-adapter targets, introduce alternate target spellings, or manufacture aliases to conceal a missing export. There is one SaltsUtils installation and release, with no independent DataBind package/root or fallback lookup.

## Selected modules

### Plugin

Plugin runtime ownership moved to Salts 1.8. SaltsUtils consumes
`Salts::PluginABI` / `Salts::Plugin` for generated Plugin artifacts. Plugin
and CFlow remain independent; there is no public PluginCFlow subsystem.

### Filesystem

`Salts::FS` provides bounded filesystem services, native watch support, and typed watch publishers. Public headers include `<salts/fs.h>`, `<salts/fs_watch.h>`, and `<salts/fs_watch_publisher.h>`.

### Process

`Salts::Process` adapts Salts process ownership and CFlow-native pipes into bounded asynchronous standard-stream handling.

### Mustache and Jinja CMeta

Mustache and Jinja CMeta have independent source, tests, documentation, and install headers. Jinja reuses the Mustache runtime through a one-way dependency rather than duplicating template execution machinery.

### Unicode

The Unicode component uses generated data with a fixed Unicode version and exposes UTF-8 scalar and identifier/whitespace property APIs without embedding template-engine semantics.

### IDL, Schema, DataBind and the Binary format compiler

`Salts::IDL` defines contracts and feeds the `salts-idlc` compiler. `Salts::Schema` owns Data-only shape/CMeta projection. `Salts::DataBind` owns native/dynamic binding, immutable BindingPlan execution and rollback. Binary is a format compiler that produces typed wire facts rather than changing IDL semantics.

**CMeta owns native type identity; IDL owns logical contracts; Schema owns data shape; DataBind owns binding.**

Detailed documentation:

- [IDL architecture](docs/IDL.md)
- [IDL compiler CLI options](databind/compiler/CLI_OPTIONS.md)
- [Database DDL generation design](docs/architecture/databind-database-ddl-generation.md)
- [DataBind ownership and adapter design](databind/runtime/README.md)

## Build and test

Use the repository root presets with the same profile as the installed Salts SDK. Build and install the complete SaltsUtils package, including DataBind and its compiler:

```sh
cmake --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user
cmake --build --preset install-linux-release-user
```

Windows uses the corresponding `win-*` presets. The `databind/` subtree is a component, not an alternative standalone configure/install entry point.

### Installed consumer validation

The existing package consumers have their own configure/build/test presets and
vcpkg manifests. On Windows, run these commands from a Visual Studio developer
PowerShell at the repository root:

```powershell
cmake --preset win-sdk-package-user
cmake --build --preset install-win-sdk-package-user
$env:SALTS_UTILS_ROOT = "$PWD/stage/sdk/windows-x64"
Push-Location databind/compiler/package_config/databind_target
cmake --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user
Pop-Location
```

The eight CTest entries exercise installed generated messages, services and
FlowMQ projections. The Binary consumer checks literal wire bytes, bounded
output and decoded byte-buffer ownership after input reuse and codec destruction.
The Producer, Plugin and public CMeta consumer projects also expose these presets.
These consumer projects use `ci-native-release-user` with explicit `SALTS_ROOT`, `SALTS_UTILS_ROOT`,
`QIGAO_TARGET_TRIPLET` and `VCPKG_CACHE_REPOSITORY_ROOT` inputs. Each consumer selects
that exact SDK; a cached package directory cannot select another installation.

### Cross-compilation host tools

Cross-compilation requires an existing host Lemon executable through
`SALTS_UTILS_HOST_LEMON_EXECUTABLE`. The build fails immediately when that input is
missing; it does not create a separate host build or select another compiler.
Before configuring a local Windows-hosted Android preset, prepare the host tool:

```sh
cmake --preset win-release-user
cmake --build --preset win-release-user --target lemon
cmake --preset android-arm64-v8a-release-win
cmake --build --preset android-arm64-v8a-release-win
```

The Android Windows presets reference `build/Msvc-Release/bin/lemon.exe` explicitly.
Android/iOS CI builds the host graph with `ci-host-release-user` before
configuring the target SDK, which uses the completed host `lemon` executable.
This removes the former implicit host-build path; callers
of other cross-compilation profiles must supply the completed host executable.

### CI and releases

CI follows the Salts workflow layout: select affected work, compile each profile once,
restore its immutable build for CTest, and package only during manual release preparation.
Publication accepts the exact successful preparation run, commit SHA and existing version tag;
it neither rebuilds nor repacks. See [CI and release workflow](docs/CI.md) for profiles,
artifact ownership, validation boundaries and release commands.

## Design rules

- Reuse Salts semantic/runtime contracts instead of introducing parallel ones.
- Keep ownership, capacity, backpressure, rollback, and error propagation explicit.
- Keep internal implementation decomposition behind the documented public consumption contract.
- Do not add compatibility aliases or hidden fallback paths for migrated capabilities.
- Keep package boundaries acyclic: Salts is the foundation; SaltsUtils is an extension consumer.

---

**Salts provides the semantics. SaltsUtils turns them into reusable higher-level tools.**

## License

SaltsUtils first-party code is licensed under the Apache License 2.0. See
[LICENSE](LICENSE). Bundled third-party software and data retain their upstream
licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
