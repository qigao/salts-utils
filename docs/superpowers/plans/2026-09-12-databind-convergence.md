# DataBind Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Converge DataBind on CMeta/CSTL/CBind/CSerde so DataBind remains only a runtime-schema, dynamic-value, and orchestration layer.

**Architecture:** CMeta owns structural type/reflection, CSTL/container owns concrete containers, CBind owns native decode/encode binding, CSerde/parsers own format tokenization, and DataBind owns schema overlays plus dynamic/runtime orchestration. Work is gated by GitHub issues so every phase can be reviewed and landed independently.

**Tech Stack:** C11, CMake, Salts CMeta/CSTL/CBind/CSerde, Salts format parsers, QueryVM, TinyTest/CTest.

**Spec:** `docs/superpowers/specs/2026-09-12-databind-convergence-design.md`

## Global Constraints

- Do not create a second native binding engine.
- Do not create DataBind-private type/generic/reflection kinds when CMeta already represents the semantic data type.
- Do not create DataBind-private sequence/set/map storage when CSTL/container can provide the storage through CMeta protocols.
- Do not make CBind/CMeta/CSTL/CSerde depend on DataBind.
- Do not require native C structs to materialize a dynamic tree.
- Do not introduce `CObject`; prefer `CDynamicValue` and, only if useful, `CDynamicObject`.
- Do not retain parser compatibility/fallback layers.
- Unsupported mappings fail explicitly and deterministically.
- Legacy public entry points may remain temporarily only as delegating migration facades; no new behavior lands in the legacy implementation.

---

### Task 1: Freeze schema ↔ CMeta capability matrix (#45)

**Files:**
- Create: `tbe/data_bind/DATABIND_CMETA_TYPE_MATRIX.md`
- Modify: `tbe/data_bind/README.md`
- Modify: `ARCHITECTURE.md`
- Test: existing schema/reflection tests under `tbe/data_bind/`

**Interfaces:**
- Consumes: current TBE schema type model and installed CMeta descriptors.
- Produces: one documented mapping used by runtime schema lowering, generated code, and #5/#47 capability checks.

- [ ] **Step 1: Inventory existing schema and DataBind type/reflection kinds**

Record every scalar, struct/record, enum, optional/presence, collection and special type currently accepted by runtime and generated paths. For each item record the current native descriptor/representation and whether CMeta already exposes an equivalent semantic data type.

- [ ] **Step 2: Write the capability matrix**

The matrix must classify each schema type as `supported`, `unsupported-until-cmeta`, or `not-a-schema-type`, and name its canonical CMeta representation. It must explicitly classify Traits/callable/interface/Range/Collector/effect metadata as not schema field types.

- [ ] **Step 3: Add characterization tests for current supported schema/reflection behavior**

Add tests before changing implementation for every public reflection query that will later become a CMeta facade. Tests must distinguish structural type information from schema overlay metadata such as alias/external name/fingerprint.

- [ ] **Step 4: Run focused tests**

Run the existing DataBind/schema test targets selected by `tbe/data_bind/CMakeLists.txt` and verify the new characterization tests pass before implementation changes.

- [ ] **Step 5: Commit and update #45**

Commit the matrix/tests/docs and attach the exact test commands/results to #45.

### Task 2: Introduce canonical schema→CMeta lowering (#45, #5)

**Files:**
- Modify: `tbe/data_bind/data_bind_internal.h`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/tbe_cbind/src/schema_model.c`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Test: DataBind schema tests and TbeCBind generated/runtime tests

**Interfaces:**
- Consumes: `DATABIND_CMETA_TYPE_MATRIX.md`.
- Produces: one canonical CMeta descriptor graph for a supported schema data type, shared by runtime and generated paths.

- [ ] **Step 1: Add RED tests for semantic identity**

Cover scalar, nested struct, enum, optional/presence, and supported generic/container cases. Verify equivalent runtime/generated mappings compare by CMeta semantic identity rather than descriptor address.

- [ ] **Step 2: Add RED tests for unsupported CMeta gaps**

A schema construct without a stable CMeta representation must fail with a schema/type diagnostic; it must not instantiate a new private DataBind kind.

- [ ] **Step 3: Implement the minimal lowering path**

Move structural field/type queries to the canonical CMeta graph. Keep aliases, wire names, defaults, validation and fingerprint in the schema overlay.

- [ ] **Step 4: Make runtime and generated validation consume the same mapping**

Remove duplicated capability decisions between `schema_model.c`, `compiler_core.c` and DataBind runtime paths.

- [ ] **Step 5: Run focused and full schema/TbeCBind tests, commit, update #45/#5**

Do not proceed to native-binding removal until the same schema is accepted/rejected consistently in runtime and generated paths.

### Task 3: Land/consume CBind encode prerequisite (`qigao/salts#255`)

**Files in salts-utils after the Salts dependency is available:**
- Modify: dependency/version configuration used by `CMakeLists.txt`/package setup
- Modify: `tbe/data_bind/CMakeLists.txt`
- Test: installed dependency-closure and downstream consumer tests

**Interfaces:**
- Consumes: public CBind encode API from `qigao/salts#255`.
- Produces: an available native encode backend for #47 without linking DataBind internals into CBind.

- [ ] **Step 1: Verify installed Salts exposes CBind decode + encode and required CMeta/CSerde targets**
- [ ] **Step 2: Add compile-time/install consumer coverage for the new public CBind encode surface**
- [ ] **Step 3: Update the salts-utils dependency floor only after the consumer test is RED on the old dependency and GREEN on the new one**
- [ ] **Step 4: Commit dependency integration and record the exact Salts commit/release in #47**

### Task 4: Migrate typed/native decode to CBind (#47)

**Files:**
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/data_bind/tbe_typed.h`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/data_bind/benchmark_tbe_typed.c`
- Test: typed generated/existing-struct tests

**Interfaces:**
- Consumes: canonical CMeta graph from Task 2 and CBind decode.
- Produces: existing generated/existing-struct decode entry points delegating to CBind.

- [ ] **Step 1: Add RED delegation/equivalence tests**

For generated and existing structs, use the same schema/input and verify field values, ownership, init/clear, limits and rollback match the current externally documented behavior.

- [ ] **Step 2: Replace native conversion execution with CBind decode**

Keep schema validation and error translation outside CBind. Descriptor-defined semantic-zero lifecycle remains authoritative.

- [ ] **Step 3: Remove dead private decode helpers only after no production call path reaches them**
- [ ] **Step 4: Run generated static/shared C and C++ consumers plus focused CTest**
- [ ] **Step 5: Commit and update #47 with dependency-closure evidence**

### Task 5: Migrate typed/native encode to CBind (#47)

**Files:**
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/data_bind/tbe_typed.h`
- Modify: generated serializer paths in `tbe/tbe_compiler/compiler_core.c`
- Test: typed serialization/round-trip tests

**Interfaces:**
- Consumes: CBind encode from Task 3.
- Produces: generated/existing-struct serialization without DataBind-private typed serialization logic.

- [ ] **Step 1: Add RED canonical-output tests for supported typed graphs**
- [ ] **Step 2: Delegate typed serialization to CBind encode + the selected CSerde writer/format adapter**
- [ ] **Step 3: Preserve schema external-name/alias/wire rules in the schema/format overlay rather than embedding them into CBind**
- [ ] **Step 4: Remove private typed serializer helpers after all callers delegate**
- [ ] **Step 5: Run round-trip, static/shared generated consumer, install/export and full CTest; commit and update #47**

### Task 6: Build the CMeta/CSTL-backed dynamic value runtime (#46)

**Files:**
- Create or split focused internal dynamic-value source/header files under `tbe/data_bind/`
- Modify: `tbe/data_bind/data_bind.h`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/data_bind_record.c`
- Modify: `tbe/data_bind/data_bind_cmeta.c`
- Modify: `tbe/data_bind/data_bind_cmeta.h`
- Test: object/value/record/CMeta adapter tests

**Interfaces:**
- Consumes: canonical CMeta graph and CSTL/container providers.
- Produces: owning `CDynamicValue`-style runtime storage with optional object facade and explicit borrowed-child lifetime.

- [ ] **Step 1: Add RED ownership/lifetime tests**

Cover root ownership, borrowed child invalidation, nested object/sequence/map destruction, failure rollback, depth/item/byte limits, and schema-associated versus schema-independent values.

- [ ] **Step 2: Add RED CMeta identity/traversal tests**

Dynamic struct/object values must expose CMeta structural reflection; sequence/set/map values must traverse through CMeta Range/container descriptors.

- [ ] **Step 3: Implement scalar/object storage against CMeta descriptors**
- [ ] **Step 4: Replace private sequence/set/map storage with CSTL/container-backed providers**
- [ ] **Step 5: Make `DataBindObject`/`DataBindValue`/`DataBindRecord` delegating facades where still required**
- [ ] **Step 6: Remove redundant DataBindCMeta adaptation that has become identity/zero-cost plumbing**
- [ ] **Step 7: Run focused ownership/CMeta/container tests, sanitizer configuration and full CTest; commit and update #46**

### Task 7: Complete parser/CSerde format adapters (#6)

**Files:**
- Modify format parser modules that own YAML/XML/CSV adapters
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Test: format-specific CSerde conformance and DataBind end-to-end tests

**Interfaces:**
- Consumes: parser-owned CSerde readers/writers.
- Produces: DataBind orchestration that selects installed adapters without parser-compat shims.

- [ ] **Step 1: Add RED cross-format semantic-equivalence tests**
- [ ] **Step 2: Route DataBind format paths directly through installed parser/CSerde APIs**
- [ ] **Step 3: Delete remaining compatibility/fallback includes/targets**
- [ ] **Step 4: Run format, streaming, query-limit and diagnostics tests; commit and update #6**

### Task 8: Split and thin DataBind orchestration (#48)

**Files:**
- Split responsibilities currently concentrated in `tbe/data_bind/data_bind.c` into focused internal modules under `tbe/data_bind/`
- Modify: `tbe/data_bind/data_bind_internal.h`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Modify: `tbe/data_bind/README.md`
- Modify: `ARCHITECTURE.md`

**Interfaces:**
- Consumes: canonical schema/CMeta layer, dynamic runtime, CBind typed path, parser/CSerde adapters.
- Produces: DataBind containing only schema/wire metadata, dynamic facade, query/stream/format orchestration and diagnostics.

- [ ] **Step 1: Move schema/fingerprint code into one internal ownership unit without changing behavior**
- [ ] **Step 2: Move parser/format orchestration into adapter-focused internal units**
- [ ] **Step 3: Move stream/query/diagnostic lifecycle into its own internal unit**
- [ ] **Step 4: Delete private structural reflection/generic/container/native-binding helpers proven unreachable**
- [ ] **Step 5: Re-evaluate and remove obsolete DataBindCMeta/DataBindCFlow adapter targets only where direct CMeta/CFlow protocols fully replace them**
- [ ] **Step 6: Run focused tests after each move and full CTest/install/export after the final split**
- [ ] **Step 7: Commit and update #48**

### Task 9: Conformance, performance and removal gate (#7, #9, #8)

**Files:**
- Modify: DataBind/CBind benchmark sources including `tbe/data_bind/benchmark_tbe_typed.c`, `tbe/data_bind/benchmark_data_bind_pool.c`, `tbe/data_bind/benchmark_data_bind_json_path.c`
- Modify/add fuzz/sanitizer harnesses tracked by #7
- Modify: migration/release documentation

**Interfaces:**
- Consumes: converged implementation.
- Produces: evidence allowing legacy facade/removal decisions and master issue closure.

- [ ] **Step 1: Run semantic equivalence before timing**

Native CBind, dynamic DataBind and end-to-end parser paths must produce the same documented values for comparable supported schemas before benchmark results are accepted.

- [ ] **Step 2: Measure native kernel, dynamic materialization and end-to-end parser costs separately**
- [ ] **Step 3: Run fuzz/sanitizer coverage for schema lowering, CBind binding, dynamic containers and streaming**
- [ ] **Step 4: Run full Release CTest, install/export consumer, C/C++ header consumers and generated static/shared schema consumers**
- [ ] **Step 5: Publish migration notes for removed `TbeTypedDescriptor`/`TBE_TYPED_*`/DataBind reflection/container APIs**
- [ ] **Step 6: Remove remaining compatibility facades only after the documented replacement behavior is covered**
- [ ] **Step 7: Update #8 checklist and close the master only when every completion criterion in the spec is satisfied**
