# SaltsUtils

**Higher-level utilities for the Salts C11 ecosystem.**

SaltsUtils builds on the installed [Salts](https://github.com/qigao/salts) SDK and extends its shared type, ownership, execution, and error semantics with parsers, QueryVM, crypto, filesystem/process adapters, templates, Unicode support, media helpers, and related utilities. DataBind now has an independent installed package owner even though its sources are still physically hosted in this repository during staged extraction.

It deliberately does **not** create a second runtime. CMeta remains the semantic type foundation, CFlow remains the execution/dataflow foundation, CSTL remains the concrete container layer, and Platform/Core remain owned by Salts.

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
  ├── salts-utils        parsers / QueryVM / crypto / FS / Process / templates / Unicode / helpers
  ├── salts-net          protocol and network tooling
  └── DataBind           schema / compiler / native-dynamic binding
```

SaltsUtils is the **general-purpose extension layer**. Protocol networking belongs in
[salts-net](https://github.com/qigao/salts-net). DataBind is already a sibling **package owner** for schema/compiler/data-binding concerns; only the physical repository extraction remains staged.

The DataBind sources and compiler are still physically built from this repository, but `SaltsUtilsTargets` no longer owns or exports DataBind/TBE targets. Consumers resolve the independent `DataBindConfig.cmake` package explicitly.

## Main capabilities

| Area | Public capability |
| --- | --- |
| Crypto | `Salts::Crypto` |
| Filesystem | `Salts::FS` |
| Process adapters | `Salts::Process` |
| Query | `Salts::QueryVM` |
| Parsers | JSON, XML, YAML, CSV, INI, TLV/LTV, Modbus, SOA, DotEnv, Cmd, TOON, TOML, DateTime, and related component targets |
| Templates | Mustache and Jinja CMeta |
| Unicode | generated Unicode property/scalar support |
| Media/helpers | Playback, Capture, Serial, Cron, and related utilities |
| DataBind source hosting | DataBind/TBE sources and compiler remain physically hosted here during repository extraction; package ownership is independent |

Parser capabilities remain independent component targets rather than a single aggregate parser facade.

## Ownership boundaries

The intended semantic boundaries are:

```text
CMeta
  native structure, semantic type identity, traits, ranges        (Salts)

CSTL
  concrete container storage                                      (Salts)

CSerde
  canonical format-neutral token contract                         (Salts)

QueryVM / parsers / utility adapters
  high-level format, protocol, query, template, and utility work   (SaltsUtils)

DataBind
  schema overlay, compiler, native/dynamic conversion,
  rollback, format orchestration                                  (sibling boundary; extraction in progress)
```

External names, presence/defaults, wire layout, validation, and schema fingerprints belong to the schema/binding layer; they are not a second CMeta type system.

## CMake

Configure against a matching installed Salts profile through `SALTS_ROOT`.

```cmake
find_package(SaltsUtils CONFIG REQUIRED)

target_link_libraries(app PRIVATE
  Salts::JsonParser
  Salts::XmlParser
  Salts::Crypto
  Salts::FS
  Salts::Process
  Salts::Playback
  Salts::Mustache
  Salts::JinjaCMeta
  Salts::Unicode
  Salts::Cron)
```

The package is fail-fast by design. It does not silently search unrelated prefixes, source trees, compatibility shims, or fallback implementations when the required installed Salts profile is missing.

### Independent DataBind package

DataBind is resolved from its own package root:

```cmake
find_package(DataBind 3 CONFIG REQUIRED
  PATHS "$ENV{DATABIND_ROOT}"
  NO_DEFAULT_PATH)

target_link_libraries(app PRIVATE
  Salts::TbeSchema
  Salts::DataBind
  Salts::DataBindCFlow)
```

The canonical target namespace remains `Salts::`, but ownership comes from `DataBindConfig.cmake`, not `SaltsUtilsConfig.cmake`. Base DataBind discovery requires Salts only; concrete adapter components resolve SaltsUtils explicitly when requested. There is no forwarding package or fallback through SaltsUtils.

## Selected modules

### Filesystem

`Salts::FS` provides bounded filesystem services, native watch support, and typed watch publishers.

Public headers include:

```text
<salts/fs.h>
<salts/fs_watch.h>
<salts/fs_watch_publisher.h>
```

### Process

`Salts::Process` adapts Salts process ownership and CFlow-native pipes into bounded asynchronous standard-stream handling.

### Mustache and Jinja CMeta

Mustache and Jinja CMeta have independent source, tests, documentation, and install headers. Jinja reuses the Mustache runtime through a one-way dependency rather than duplicating template execution machinery.

### Unicode

The Unicode component uses generated data with a fixed Unicode version and exposes UTF-8 scalar and identifier/whitespace property APIs without embedding template-engine semantics.

## Schema / DataBind transition

The current repository still physically contains the TBE compiler and DataBind implementation, but the installed package boundary is already a sibling **DataBind** owner focused on:

- schema definition and validation;
- compiler/code generation;
- native and dynamic value binding;
- rollback/failure-atomic conversion;
- format orchestration over parser/token contracts;
- adapters to CMeta, CSTL, CSerde, and CFlow.

The remaining physical repository extraction must preserve one semantic source of truth: **CMeta owns native type identity; DataBind owns schema/binding concerns.**

Current detailed documentation remains available at:

- [TBE compiler CLI options](tbe/tbe_compiler/CLI_OPTIONS.md)
- [Database DDL generation design](docs/architecture/tbe-database-ddl-generation.md)
- [DataBind ownership and adapter design](tbe/data_bind/README.md)

## Build and test

Use the repository presets with the same profile as the installed Salts SDK.

Typical flow:

```sh
cmake --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user
cmake --build --preset install-linux-release-user
```

Windows uses the corresponding `win-*` presets.

## Design rules

- Reuse Salts semantic/runtime contracts instead of introducing parallel ones.
- Keep ownership, capacity, backpressure, rollback, and error propagation explicit.
- Prefer independent component targets over broad facades.
- Do not add compatibility aliases or hidden fallback paths for migrated capabilities.
- Keep package boundaries acyclic: Salts is the foundation; SaltsUtils is an extension consumer.

---

**Salts provides the semantics. SaltsUtils turns them into reusable higher-level tools.**

## License

SaltsUtils first-party code is licensed under the Apache License 2.0. See
[LICENSE](LICENSE). Bundled third-party software and data retain their upstream
licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
