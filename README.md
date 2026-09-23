# SaltsUtils

**Higher-level utilities for the Salts C11 ecosystem.**

SaltsUtils builds on the installed [Salts](https://github.com/qigao/salts) SDK and extends its shared type, ownership, execution, and error semantics with parsers, QueryVM, crypto, filesystem/process adapters, templates, Unicode support, media helpers, and DataBind schema/compiler/native-dynamic binding.

**DataBind is part of SaltsUtils. Its source, build, installation, release, and public target export are owned by this repository.** It is not a separate project or package.

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
  │     └── DataBind: schema / compiler / native-dynamic binding
  └── salts-net: protocol and network tooling
```

SaltsUtils is the general-purpose extension layer. Protocol networking belongs in [salts-net](https://github.com/qigao/salts-net). DataBind is SaltsUtils' canonical transport-neutral IDL and binding compiler. Its implementation lives under `databind/`; TBE is a DataBind format/backend rather than the owner of the schema/compiler tree.

## Main capabilities

| Area | Public capability |
| --- | --- |
| Crypto | `Salts::Crypto` |
| Plugin ABI | `Salts::Plugin`; CMeta Interface/Callable manifests and semantic admission |
| Filesystem | `Salts::FS` |
| Process adapters | `Salts::Process` |
| Query | `Salts::QueryVM` |
| Parsers | JSON, XML, YAML, CSV, INI, TLV/LTV, Modbus, SOA, DotEnv, Cmd, TOON, TOML, DateTime, and related component targets |
| Templates | Mustache and Jinja CMeta |
| Unicode | generated Unicode property/scalar support |
| Media/helpers | Playback, Capture, Serial, Cron, and related utilities |
| DataBind | `Salts::Databind`; schema, native/dynamic binding, rollback, and compiler/code generation |

Parser capabilities remain independent component targets rather than a single aggregate parser facade.

## Ownership boundaries

```text
CMeta
  native structure, semantic type identity, traits, ranges        (Salts)

CSTL
  concrete container storage                                     (Salts)

CSerde
  canonical format-neutral token contract                        (Salts)

QueryVM / parsers / utility adapters
  high-level format, query, template, and utility work            (SaltsUtils)

DataBind
  schema overlay, compiler, native/dynamic conversion,
  rollback, format orchestration                                 (SaltsUtils)
```

External names, presence/defaults, wire layout, validation, and schema fingerprints belong to the schema/binding component; they are not a second CMeta type system.

## CMake

Build SaltsUtils against a matching installed Salts profile through `SALTS_ROOT`. Consumers explicitly select the SaltsUtils installation through `SALTS_UTILS_ROOT`:

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

### DataBind consumption

The exact public consumption target is **`Salts::Databind`**:

```cmake
find_package(SaltsUtils CONFIG REQUIRED
  PATHS "$ENV{SALTS_UTILS_ROOT}" NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE Salts::Databind)
```

SaltsUtils exports the actual runtime and owns its internal dependency closure. Consumers do not assemble internal Core/CMeta/CFlow/format-adapter targets, introduce alternate target spellings, or manufacture aliases to conceal a missing export. There is one SaltsUtils installation and release, with no independent DataBind package/root or fallback lookup.

## Selected modules

### Plugin

`Salts::Plugin` defines a finite CMeta-based plugin manifest/export ABI. Stable `contract_id`/version values carry semantic identity across translation units and DSOs; descriptor, vtable, callable and load addresses remain representation facts. Dynamic loading/registry and optional CFlow execution adapters are layered above this contract rather than added to CMeta/CFlow core.

### Filesystem

`Salts::FS` provides bounded filesystem services, native watch support, and typed watch publishers. Public headers include `<salts/fs.h>`, `<salts/fs_watch.h>`, and `<salts/fs_watch_publisher.h>`.

### Process

`Salts::Process` adapts Salts process ownership and CFlow-native pipes into bounded asynchronous standard-stream handling.

### Mustache and Jinja CMeta

Mustache and Jinja CMeta have independent source, tests, documentation, and install headers. Jinja reuses the Mustache runtime through a one-way dependency rather than duplicating template execution machinery.

### Unicode

The Unicode component uses generated data with a fixed Unicode version and exposes UTF-8 scalar and identifier/whitespace property APIs without embedding template-engine semantics.

### DataBind and the TBE compiler

The SaltsUtils DataBind component provides schema definition and validation, compiler/code generation, native and dynamic value binding, rollback/failure-atomic conversion, and format orchestration over parser/token contracts. It reuses CMeta, CSTL, CSerde, and CFlow without duplicating their semantic foundations.

**CMeta owns native type identity; DataBind owns schema/binding concerns within SaltsUtils.**

Detailed documentation:

- [DataBind compiler CLI options](databind/compiler/CLI_OPTIONS.md)
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
