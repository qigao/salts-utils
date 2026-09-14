# DataBind-Only Convergence Design

**Status:** Approved on 2026-09-13

## Context

SaltsUtils currently builds and exports `Salts::DataBind`. DataBind owns runtime
schema loading, dynamic values, typed/generated C conversion, binary and text
serialization, parser orchestration, query execution, streaming, limits and
diagnostics.

The base Salts package also exports a native descriptor binder with overlapping
decode responsibilities. SaltsUtils production targets do not link that binder,
but one schema integration test, one installed package consumer and current
documentation still make it part of the SaltsUtils design. That split ownership
is misleading and would create two binding engines if the previous convergence
plan were completed.

## Decision

DataBind is the only public binding engine owned or consumed by SaltsUtils.

The duplicate base-package binder is outside the SaltsUtils dependency,
configuration, export, test and documented consumer surface. Its upstream target
is not removed by this project because unknown base-package consumers are outside
the SaltsUtils compatibility boundary.

No feature flag, fallback, forwarding facade or compatibility path is added.
Unsupported descriptor, schema, container and format combinations fail through
the existing explicit DataBind error contract.

## Target architecture

```text
schema text
    |
    v
schema overlay -------------------------------+
external names / aliases / defaults / wire    |
                                               v
CMeta semantic graph ----------------> DataBind conversion core
struct / enum / scalar / container             |             |
                                                v             v
                                      native C storage   dynamic values
                                                           backed by CSTL

format parsers <------ CSerde tokens ------ DataBind orchestration
```

Dependency direction is one way:

- CMeta owns semantic identity and structural reflection.
- The schema overlay owns external names, aliases, optional/default state,
  validation, binary layout and compatibility fingerprints.
- CSTL owns concrete dynamic container storage and lifecycle operations.
- CSerde and the format parsers own tokenization and format mechanics.
- DataBind owns conversion, rollback, parse/serialize orchestration and public
  error translation for both native and dynamic destinations.

Structural metadata must never absorb schema-only wire policy. A CMeta field
describes the actual C storage field name, type, offset, size and alignment. The
overlay may map that field to a different external name or wire offset without
changing the CMeta graph.

## Native typed path

Generated and existing-struct binding remain DataBind capabilities. The current
`TbeTypedType` metadata is reduced incrementally until native storage identity,
shape, offsets and scalar kinds come only from the canonical CMeta graph.

Wire-only properties remain in a DataBind-owned overlay:

- external and alias names;
- optional presence bits and defaults;
- binary offsets, widths and byte order;
- schema fingerprint and compatibility validation.

The first removal slice does not delete public `TBE_TYPED_*` entry points. It
removes the unrelated base binder from SaltsUtils and freezes the direction for
subsequent typed-runtime work. Later removal of duplicated metadata requires a
separate RED/GREEN migration gate; it must not be hidden behind a facade.

## Dynamic value path

Runtime schemas continue to parse into an owning `DataBindValue` root without a
generated application type. Dynamic values use the same CMeta semantic graph as
native values and move their sequence, set and map storage to CSTL providers.

The public name remains `DataBindValue` during this convergence. Introducing a
second generic object brand would not remove duplication. Child views are
borrowed from the owning root and become invalid when that root or its containing
storage is mutated or released.

## Error and rollback contract

- Invalid arguments return `DATA_BIND_ERR_INVALID_ARG` without consuming or
  publishing output.
- Missing or incompatible schema/CMeta mappings return `DATA_BIND_ERR_SCHEMA`
  with the most specific `Type.field` path available.
- Type/range mismatches keep their existing typed status and do not partially
  replace the caller's destination.
- Allocation and configured resource limits remain distinguishable.
- Native conversion is transactional: build temporary zero-state storage,
  publish only on success and restore every owned field on failure.
- No error is converted to another engine, inferred storage or legacy path.

## Issue decomposition

| Issue | DataBind-only disposition |
| --- | --- |
| #5 | Close as superseded; its referenced `tbe_cbind` implementation is absent. |
| #6 | Reframe parser-owned CSerde adapters as direct DataBind inputs. |
| #7 | Cover DataBind, schema, CSerde adapters and other active parser surfaces. |
| #8 | Master tracker for this design. |
| #9 | Benchmark DataBind native, dynamic, parser and end-to-end costs separately. |
| #46 | Move dynamic structural identity and containers to CMeta/CSTL. |
| #47 | Make the DataBind typed runtime consume the canonical CMeta graph. |
| #48 | Split DataBind by the ownership boundaries above after #46/#47 gates. |
| #50 | Close after the merged main-branch repair evidence is verified. |

Standalone Jinja work in #26 is an independent subsystem and keeps its own plan
and acceptance sequence.

## Delivery sequence

1. Add a repository gate that rejects active source/build dependencies on the
   duplicate binder, and observe the existing tree fail it.
2. Remove the schema integration dependency and tests that validate another
   package's binding engine. Preserve the schema/CMeta provider construction,
   semantic-identity and atomic-publication tests.
3. Remove the unrelated dependency assertion from the installed Cron consumer.
4. Update current public architecture, DataBind and compiler documentation so
   SaltsUtils users are directed to DataBind.
5. Re-run schema, typed, generated-consumer and installed-consumer gates.
6. Reframe/close the affected GitHub issues using exact-head evidence.
7. Implement #47, then #46, then #48 as independent RED/GREEN slices.
8. Complete adapter, fuzz and benchmark work (#6/#7/#9), then resume #26.

## Verification

The first slice is complete only when:

- no active SaltsUtils source or CMake target includes, links or asserts the
  duplicate binder;
- `test_schema_cmeta` still validates explicit STRING/BYTES storage providers,
  copied semantic identities and atomic descriptor publication;
- schema, enum, descriptor, direct-parser and installed-consumer workflows pass
  on the same exact head;
- installed exports still contain `Salts::DataBind`, `Salts::DataBindCMeta` and
  `Salts::DataBindCFlow` with no added dependency;
- current public documentation describes DataBind as the sole SaltsUtils binding
  engine;
- unsupported mappings remain fail-fast and no compatibility path exists.

## Alternatives rejected

### Default-off compatibility option

Rejected because SaltsUtils has no active production target to toggle. A switch
would preserve a false architectural choice and add an untested branch.

### Cross-repository removal

Rejected for this project. Removing the upstream target requires a separate
consumer inventory and breaking-change review; it is not necessary to make the
SaltsUtils boundary unambiguous.

### Delegate DataBind native conversion upstream

Rejected because it leaves DataBind dynamic conversion and native conversion
with separate ownership, error and rollback semantics. The approved design uses
one DataBind conversion boundary over one CMeta graph.

## Rollback

Rollback is a source-control revert of the complete removal slice. There is no
runtime fallback. If a required SaltsUtils consumer is discovered, its concrete
DataBind requirement must be specified and tested before the design is amended.
