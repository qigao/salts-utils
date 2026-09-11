# DataBind Convergence Design

Date: 2026-09-12
Status: approved architecture / implementation tracked by issues
Master tracker: #8

## 1. Goal

Refactor DataBind from a second type/container/binding runtime into a thin runtime-schema layer built on the canonical Salts primitives.

The final ownership model is:

- **CMeta** — runtime data type identity, Struct/Enum/generic structural reflection, container descriptors, Range/Collector, semantic identity.
- **CSTL/container** — concrete sequence/set/map storage and lifecycle providers.
- **CBind** — the single native C binding engine over CMeta-described storage, for decode and encode.
- **CSerde + format parsers** — canonical token transport plus JSON/YAML/XML/CSV format mechanics.
- **DataBind** — schema/wire contract overlay, schema-aware owning dynamic values, validation/fingerprint, parser selection, query/stream orchestration, and error translation.

The refactor must remove duplicated native binding, duplicated structural reflection/type identity, and duplicated container implementations from DataBind.

## 2. Non-goals

- Do not move schema/wire semantics into CMeta.
- Do not put the dynamic object runtime inside CBind.
- Do not require native C structs to materialize a dynamic object tree.
- Do not make schema support every CMeta capability.
- Do not preserve a hidden legacy engine as a fallback.
- Do not introduce `CObject` as a generic runtime object model.

## 3. Target architecture

```text
Schema / TBE
    |  wire names, aliases, constraints, compatibility, fingerprint
    v
CMeta data graph  <---->  schema metadata overlay
    |
    +--> CBind ----------------------> native C storage
    |        decode + encode
    |
    +--> CDynamicValue runtime ------> CSTL/container storage
             |
             +--> CMeta Range/reflection --> CFlow / consumers

Format parsers <--> CSerde readers/writers
        ^                 |
        +------ DataBind orchestration ------+
                 query / stream / diagnostics
```

Dependency direction is one-way. CBind/CMeta/CSTL/CSerde must not depend on DataBind.

## 4. Type and reflection ownership

CMeta is the source of truth for structural runtime type information:

- scalar/native type identity;
- Struct fields and Enum metadata;
- semantic generic identities;
- Pair/Tuple/Option/Result data shapes;
- container descriptors;
- Range/Collector protocols;
- structural reflection and type registry.

DataBind keeps only schema-specific metadata:

- schema names and versions;
- external field names and aliases;
- required/optional/wire presence semantics;
- format annotations;
- schema validation/default constraints;
- compatibility and fingerprint.

DataBind must not add a private type kind if CMeta already represents the same semantic data type.

Generic identity is semantic and must not depend on descriptor pointer equality.

## 5. Schema type universe

Schema supports the **serializable data-type subset of CMeta plus wire semantics**, not all CMeta capabilities.

Supported/target families:

- bool;
- signed/unsigned fixed-width integers;
- floating point;
- string and bytes;
- Struct;
- Enum;
- Option/presence;
- Pair and Tuple when canonical wire semantics are defined;
- semantic sequence/set/map containers;
- Result only after canonical wire representation is defined;
- Variant/oneof only after the corresponding CMeta data representation is stable.

Not schema data types:

- raw pointer identity / const pointer identity;
- Traits;
- callable / `typed_any`;
- interface / implements;
- Range;
- Collector;
- effect/property metadata;
- other execution/control protocols.

Schema describes a semantic container shape, not a concrete CSTL implementation. For example `sequence<User>` may bind to Vec/List/Deque according to the native CMeta descriptor/profile.

## 6. Native binding

CBind becomes the only native C binding engine.

The existing DataBind/TBE typed path must converge as follows:

```text
schema metadata
      +
CMeta descriptor
      |
      v
CBind decode / encode
      |
      v
native C storage
```

`TbeTypedDescriptor`, `TBE_TYPED_*`, generated helper code and existing-struct mapping may temporarily remain as compatibility facades, but their implementation must delegate to CMeta + CBind. No new semantics may be added to the legacy engine.

CBind encode is tracked in `qigao/salts#255`; legacy typed serialization cannot be removed until that prerequisite is complete.

## 7. Dynamic object/value model

Runtime-schema users still need an owning dynamic representation. This is a storage/object-model concern, not a binding-kernel concern.

Preferred long-term shape:

- `CDynamicValue` — single owning runtime value model;
- optional `CDynamicObject` convenience facade for object roots;
- CMeta semantic type identity and reflection;
- CSTL/container storage for sequence/set/map data;
- explicit owner/borrowed-child lifetime rules;
- optional schema association overlay.

Do not introduce `CObject`.

Existing `DataBindObject`, `DataBindValue` and `DataBindRecord` may be retained temporarily as migration facades. They should be removed or collapsed once equivalent behavior is covered by the canonical dynamic-value runtime.

Native CBind paths do not allocate the dynamic tree unless a caller explicitly requests dynamic materialization.

## 8. Containers

Concrete container storage belongs to CSTL/container.

DataBind must not own independent list/map/set storage or lifecycle rules after convergence.

CMeta descriptors and Range/Collector provide the abstraction boundary so CBind and dynamic-value code do not depend on concrete CSTL layouts.

Container lifecycle, ownership, rollback and semantic-zero restoration follow the CMeta/container provider contracts.

## 9. Format and serialization boundaries

Format parsers own syntax and format-specific mapping. CSerde provides canonical reader/writer tokens. DataBind orchestrates parser selection and schema-aware conversion but does not implement a second parser facade.

Supported format paths must use installed Salts parser/CSerde APIs directly.

Query syntax and execution remain in parser frontends/QueryVM. DataBind forwards limits, selection context, diagnostics and result ownership; it does not implement another query VM.

## 10. DataBind final responsibilities

After migration DataBind contains only three major domains:

1. **Schema/wire contract**
   - schema loading;
   - wire names/aliases;
   - validation/defaults;
   - compatibility/fingerprint.

2. **Dynamic runtime facade**
   - owning dynamic root;
   - schema association;
   - convenient dynamic lookup;
   - explicit lifetime rules.

3. **Orchestration**
   - parser/CSerde selection;
   - serialization/deserialization coordination;
   - QueryVM integration;
   - streaming;
   - limits/diagnostics/error translation.

DataBind no longer owns a private generic system, structural reflection universe, native typed binder, or concrete container engine.

## 11. Migration sequence

### Phase 0 — capability inventory

- land schema ↔ CMeta capability matrix;
- identify all DataBind-private type/reflection/container/typed APIs;
- freeze migration/removal rules.

### Phase 1 — core prerequisites

- add CBind encode (`qigao/salts#255`);
- complete parser-owned CSerde adapters required by supported formats (#6).

### Phase 2 — native typed convergence

- extend type coverage only through canonical schema→CMeta/CBind mapping (#5);
- migrate generated/existing-struct typed paths to CBind (#47).

### Phase 3 — dynamic runtime convergence

- introduce CMeta/CSTL-backed dynamic value storage (#46);
- preserve runtime unknown-schema/plugin/script use.

### Phase 4 — thin DataBind runtime

- split DataBind by ownership domain;
- remove redundant reflection/container/typed logic and zero-value adapters (#48).

### Phase 5 — conformance/removal gate

- benchmark and workload comparison (#9);
- fuzz/sanitizer expansion (#7);
- full Release CTest;
- install/export and downstream consumer tests;
- C/C++ public-header tests;
- generated static/shared schema consumers;
- dependency-closure checks;
- publish migration/removal notes;
- remove legacy compatibility code only after equivalent behavior is covered.

## 12. Issue map

- #8 — master tracker
- #45 — schema/type/reflection convergence on CMeta
- #46 — CMeta/CSTL-backed dynamic value runtime
- #47 — native typed binding migration to CBind
- #48 — final DataBind thinning and module ownership
- #5 — schema binding type coverage
- #6 — parser/CSerde adapters
- #7 — fuzz/sanitizer coverage
- #9 — benchmark/workload coverage
- `qigao/salts#255` — CBind encode prerequisite

## 13. Completion criteria

The refactor is complete only when all of the following are true:

- CMeta is the structural reflection/type source of truth.
- CSTL/container owns runtime container storage.
- CBind owns native decode and encode.
- DataBind has no independent native binder, structural reflection/type system, generic system, or private container engine.
- Runtime dynamic-schema users retain a supported owning dynamic-value path.
- Supported formats use direct Salts/CSerde adapters.
- Query/stream/error/limit semantics are documented and tested.
- Install/export dependency closure is clean.
- Migration notes and conformance/benchmark evidence are published.
