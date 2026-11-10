# SaltsUtils

## Upstream dependency versions

Select the upstream SDK release through `VCPKG_CACHE_VERSION` in the parent
environment (for example, by loading the local `.env`). Use the same version
for the `VCPKG_CACHE_REPOSITORY_ROOT` checkout. Values contain the version
without the `v` prefix; missing variables fail rather than choosing `latest`.
The shared `resolve-release` entry point is required (not present in v0.1.2).

The per-triplet files in `cmake/dependencies/` are release descriptors. Their
historical `.lock.json` suffix is retained; `source.kind=github-release`
distinguishes them from generated literal URL/hash locks. Resolve the selected
release into the build directory before restoring it:

```sh
python "$VCPKG_CACHE_REPOSITORY_ROOT/scripts/restore-native-dependency.py" resolve-release \
  "cmake/dependencies/$VCPKG_TARGET_TRIPLET.lock.json" \
  --output-lock "build/dependencies/$VCPKG_TARGET_TRIPLET.lock.json"
python "$VCPKG_CACHE_REPOSITORY_ROOT/scripts/restore-native-dependency.py" locked \
  "build/dependencies/$VCPKG_TARGET_TRIPLET.lock.json" \
  --destination "build/dependencies/$VCPKG_TARGET_TRIPLET-$VCPKG_CACHE_VERSION" \
  --payload vcpkg_installed
```

Set `VCPKG_INSTALLED_DIR` to the reported root for the selected preset. The
resolver requires the published asset's SHA-256 digest; the existing restore
checks downloaded bytes and preserves immutable installations. Changing a
version never overwrites an old SDK. CMake does not load `.env` or download
the dependency SDK implicitly. Existing host-tool package locks remain exact.

CI uses the same named repository Actions variables and selects local shared
actions from the chosen infrastructure checkout. SaltsBase and SaltsCore
prepare jobs resolve once, then distribute generated locks and verified blobs
to all matrix workers. Set the CI variables before enabling these changes;
the selected infrastructure release must contain `resolve-release`.

This keeps consumer version policy in the environment, asset digests in the
upstream release, and generated restore locks in build inputs. Updating a
release no longer requires editing a digest table for every platform. SDK
manifests, compiler/ABI selection and product tests remain mandatory. To roll
back, choose the previous exact release and restore into its own directory.

`SALTS_BASE_VERSION` selects the exact installed SDK at `SALTS_BASE_ROOT`;
`CMETA_VERSION` selects the installed SDK at `CMETA_ROOT`. CMeta must itself be
built against the same Base version. Native platform/FS/process/parser/Unicode
facilities come from Base. CMeta, CFlow, CSTL and Plugin come from CMeta. The
product does not depend on the old monorepo SaltsCore SDK.

## Repository ownership

This split repository owns Mustache, Jinja, IDL/Schema, the `salts-idlc` compiler,
DataBind and its format/native adapters, and the C++/Lua/QuickJS bindings.
Parsers, QueryVM, Unicode, Cron, operating-system primitives and media are Base
capabilities and are consumed through Base's existing targets. Networking and
TLS remain SaltsCore capabilities; SaltsUtils does not duplicate their runtime.

IDL defines transport-neutral contracts, Schema defines data shape and
DataBind owns binding and serialization. They share one SaltsUtils package.
DataBind's binary format and fingerprint algorithm remain unchanged. The
existing namespaced Monocypher fingerprint source stays in `vendor/`; it is
not replaced by another hash algorithm during the migration.

Native UUID fields use Base's `uuid_t` byte array. Generated C codecs and
metadata retain the existing 16-byte wire representation. Regenerate outputs
with the matching compiler and rebuild consumers after switching SDK headers.

## Restore and build

Use the CMake user presets below. The retired Praktor `build.yml` referenced
drivers removed by the split migration and has been removed with them.

Load the local ignored `.env` into the parent shell before invoking presets.
Use `.env.example` for nonsecret fields; keep existing machine settings and
credentials local. CMake consumes explicit installation roots and does not
run vcpkg or load dotenv files. The shared restoration commands above first
resolve the selected cache release and then restore its URL/hash lock.
Set `VCPKG_INSTALLED_DIR` to the returned root, not an inferred path.
`ci-*-user` presets also use `SALTS_CI_VCPKG_INSTALLED_DIR`.

Base 0.1.4 provides the public thread-pool callback query required by the
current CMeta/CFlow SDK. To prepare CMeta from its current checkout, use its
normal product configure/build/install preset; point `CMETA_ROOT` at the
resulting `build/<preset>/stage`. The historical CMeta 0.1.003 monorepo SDK is
not interchangeable with this split SDK despite its CMake version of 0.1.3.

For Windows LLVM-MinGW, with the environment already loaded:

```sh
cmake --fresh --preset native-release-user
cmake --build --preset native-release-user
ctest --preset native-release-user --output-on-failure --no-tests=error
cmake --build --preset install-native-release-user
```

Use `gcc-release-user`, `llvm-native-release-user` or `msvc-release-user` with
matching compiler/ABI SDKs. Cross builds require a host `salts-idlc` and its
explicit Base/CMeta runtime roots through the existing `SaltsUtils_IDLC_*`
inputs. Tests belong to their owning modules. Runtime behavior is checked by
the formal executable suites; SDK configuration and installation use the
normal build graph, without nested consumer verification projects.

### Host generators

Re2c and Lemon are separate host tools. Their immutable package IDs, versions
and hashes are locked in `cmake/re2c.lock.json` and `cmake/lemon.lock.json`.
An authenticated feed can restore them through the existing shared helper.
Alternatively, select the original digest-qualified NuGet archives published
as assets of the chosen vcpkg-cache release, and explicitly set
`RE2C_ARCHIVE_FILE` and `LEMON_ARCHIVE_FILE`. The same helper validates package
identity and hashes; Lemon's executable, template and source receipt remain
paired. This is an explicit archive input, not an automatic retry or alternate
parser. Authentication, integrity and missing-input failures stop the build.

### Package consumption

The installed package requires the matching Base and CMeta roots and its
preinstalled dependency prefix. Lua and QuickJS are unconditional modules in
the complete graph; their existing `unofficial-lua` and `qjs` packages are
required when importing the exported target set.

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

Plugin runtime ownership is in the CMeta SDK. SaltsUtils consumes
`CMeta::PluginABI` / `CMeta::Plugin` for generated Plugin artifacts. Plugin
and CFlow remain independent; there is no public PluginCFlow subsystem.

### Filesystem and process dependencies

Native filesystem and process APIs come from SaltsBase's `Salts::Core`
(`<file/fs.h>` and `<file/process.h>`). CFlow I/O primitives belong
to `CMeta::CFlow`. SaltsUtils no longer builds or exports the removed
`Salts::CFlowFS` / `Salts::CFlowProcess` adapters or their `<cflow/fs*.h>`
and `<cflow/process.h>` headers.

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
cmake --preset gcc-release-user
cmake --build --preset gcc-release-user
ctest --preset gcc-release-user
cmake --build --preset install-gcc-release-user
```

Windows can use `msvc-release-user` from a Visual Studio developer environment. The `databind/` subtree is a component, not an alternative standalone configure/install entry point.

Configuration fails on missing SDK roots, dependencies, generators and
unsupported sanitizer profiles. `CMakeOptions.cmake` declares `BUILD_TESTS`,
`BUILD_BENCHMARK`, `BUILD_EXAMPLES` and the existing `SALTSCORE_ENABLE_*`
sanitizer inputs. SDK versions and paths come from the parent environment;
use `--fresh` after changing them. The normal build graph and module CTests
cover generated messages, services, Binary wire values, Plugin leases and
public CMeta adapters; no nested consumer verification project is required.

### CI and releases

CI resolves exact Base/cache releases once and shares verified inputs across
the ten published Base profiles, including native ARM64, MSVC and TSan.
`CMETA_REF` explicitly selects the source-built CMeta product SDK while
`CMETA_VERSION` validates its installed package version. This is a declared
source SDK input during the split migration, not a fallback after failed
restoration. Android uses an explicitly built native IDL compiler with its
separate Base/CMeta runtime closure. Native lanes run the full CTest suite;
Android lanes cross-build and install without claiming device execution.

Each qualified SDK TAR records source, upstream versions/refs and Base producer
provenance. The historical NuGet publisher remains for historical immutable
release inputs; this migration does not republish old package versions. A new
SaltsUtils release must use a new version and the split SDK artifact contract.
See [CI details](docs/CI.md).

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
