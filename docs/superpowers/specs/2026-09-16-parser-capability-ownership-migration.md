# Parser Capability Ownership Migration Design

## Context

Salts currently owns QueryVM and the format/protocol parser implementations under `parser/` and exports them in the `Salts::` namespace. SaltsUtils is already the higher-level package built strictly against an installed Salts profile and owns migrated utility capabilities such as Crypto, FS, and Process.

The repository boundary is being tightened so format parsing, query execution, and parser adapters live with the higher-level utility package instead of the low-level Salts foundation.

One parser is an architectural exception: `Salts::UriParser` is a direct private dependency of `Salts::CNet`. Moving UriParser into SaltsUtils would create a package dependency cycle (`Salts -> SaltsUtils -> Salts`). Uri parsing therefore remains a low-level Salts capability.

## Decision

- Salts remains the unique owner of `Salts::UriParser`.
- `parser/uri_parser` is physically separated from the parser aggregate and becomes a standalone Salts module at `uri/`.
- SaltsUtils becomes the unique owner of QueryVM and every other current parser capability under `salts/parser`.
- Existing installed target names remain unchanged. Ownership changes package, not target identity.
- SaltsUtils consumes `Salts::UriParser`, `Salts::Core`, `Salts::CSTL`, `Salts::CSerde`, and other required low-level targets only through the installed Salts package selected by `SALTS_ROOT`.
- Salts must not depend on SaltsUtils.

## Target ownership after migration

### Salts

- `Salts::UriParser`
- existing low-level targets such as `Salts::Core`, `Salts::Platform`, `Salts::CSTL`, `Salts::CMeta`, `Salts::CSerde`, `Salts::CFlow`, `Salts::CNet`

### SaltsUtils

- `Salts::QueryVM`
- `Salts::IniParser`
- `Salts::JsonParser`
- `Salts::JsonCSerdeAdapter`
- `Salts::XmlParser`
- `Salts::CsvParser`
- `Salts::TLVParser`
- `Salts::LtvParser`
- `Salts::ModbusParser`
- `Salts::SoaParser`
- `Salts::DotEnvParser`
- `Salts::CmdParser`
- `Salts::Toon`
- `Salts::TomlParser`
- `Salts::DateTimeParser`
- `Salts::CYaml`
- `Salts::CYamlJsonAdapter`
- `Salts::Selector`

## Dependency direction

```text
application -> SaltsUtils parser/query target -> installed Salts low-level target
                                         \-----> installed Salts::UriParser (only where needed)

Salts::CNet -> Salts::UriParser

Salts -/-> SaltsUtils
```

`TLVParser`, `LtvParser`, and `SoaParser` continue to use `Salts::UriParser`, but now across the installed package boundary. JSON/CSerde composition remains explicit through `Salts::JsonCSerdeAdapter`; `Salts::JsonParser` itself does not gain a CSerde dependency.

## Public API and compatibility

This is a package-ownership breaking cutover but not a parser runtime API redesign.

- Existing C headers, parser state machines, diagnostics, ownership rules, and error codes stay unchanged unless a build-boundary fix requires a path-only change.
- Installed target names stay in the shared `Salts::` namespace.
- Consumers that need moved parser/query targets must load `SaltsUtils`; they must not expect those targets from the Salts package.
- No compatibility aliases, forwarding headers, duplicate source copies, source-tree fallback, or implicit package search paths are permitted.

## Fail-fast package boundary

SaltsUtils already rejects an installed Salts profile that still exports capabilities migrated to SaltsUtils. Extend that gate to every moved parser/query target. A pre-migration Salts SDK must fail configuration immediately rather than creating two owners for the same `Salts::` target.

Conversely, the post-migration Salts package must export `Salts::UriParser` and must not export any moved parser/query target.

## Build-system constraints

- Preserve `SALTS_ROOT` exact-profile lookup and `NO_DEFAULT_PATH` behavior.
- Do not add a synthetic CMake install-verification framework.
- Verification uses normal configure/build/CTest/package export behavior and CI.
- Generated lexer/parser workflows continue to use the repositories' existing re2c/lemon tooling.
- `utils/parser` in Salts must not retain the stale `${PROJECT_SOURCE_DIR}/parser` include path once the aggregate parser directory is removed.

## Documentation

Update both repositories' architecture and README descriptions so canonical ownership is unambiguous:

- Salts documents UriParser as its URI parsing primitive and removes claims that it owns QueryVM/all format parsers.
- SaltsUtils documents QueryVM and format/protocol parsers as its capabilities and identifies UriParser as an installed Salts dependency.

Historical design documents remain historical records and are not rewritten.

## Migration order

1. Add SaltsUtils fail-fast checks for moved targets and add the destination parser/query build graph.
2. Preserve target names while making all cross-package links reference imported `Salts::*` low-level targets.
3. Separate UriParser inside Salts and keep CNet linked to `Salts::UriParser`.
4. Remove moved parser/query sources, aggregate CMake ownership, installs, and exports from Salts.
5. Remove stale Salts source-tree include references and update architecture docs.
6. Run normal tests/CI in both repositories and inspect package exports for single ownership.

## Acceptance criteria

- Salts builds and tests with `Salts::CNet -> Salts::UriParser` and no dependency on SaltsUtils.
- Salts install exports `Salts::UriParser` and no moved parser/query targets.
- SaltsUtils configures only against the post-migration installed Salts profile and fails against a pre-migration profile that exports moved targets.
- SaltsUtils builds/tests all moved parser/query targets with their existing installed target names.
- No duplicate implementation remains in Salts.
- No compatibility alias, forwarding header, source-tree fallback, or new install-verification framework is introduced.
