# DataBind Canonical CMeta Runtime Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete issue #47 by moving every non-container native typed runtime path to canonical CMeta structure and lifecycle operations while preserving a schema-only wire overlay.

**Architecture:** Extend the ABI-v2 descriptor introduced by PR #55 through four independently gated capability slices: fixed native values, complete enum domains, owned scalar lifecycle, and optional presence. Each slice begins with a real generated/public RED, adds only explicit CMeta providers, routes supported wrappers through descriptor APIs, and removes parallel typed structural metadata when its last caller disappears. Native list/set/map storage remains rejected and belongs to #46.

**Tech Stack:** C11, CMake/CTest, Salts CMeta and CSTL APIs, TinyTest, Mustache-generated C, Lua adapters, GitHub Actions ASan/UBSan gates.

**Spec:** `docs/superpowers/specs/2026-09-14-databind-cmeta-runtime-completion-design.md`

## Global Constraints

- DataBind is the sole SaltsUtils binding engine.
- CMeta owns native semantic identity, field names, nesting, offsets, size, alignment, and lifecycle/conversion operations.
- The schema overlay owns names/aliases, presence/defaults, binary offsets/widths/byte order, validation, and fingerprints.
- No CBind dependency, compatibility facade, fallback engine, feature flag, inferred storage, or dynamic-root detour.
- Unsupported records publish no descriptor and fail atomically as `DATA_BIND_ERR_SCHEMA` with the most specific available path.
- Type, range, allocation, depth, item, and byte-limit errors retain their distinct statuses.
- Native list/set/map work is excluded and remains tracked by #46.
- Do not begin #48 file decomposition during this plan.

---

### Task 1: Lock the completion capability matrix

**Files:**
- Modify: `tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_mapping.c`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/test_cmeta_field_projection.c`

**Interfaces:**
- Consumes: ABI-v2 `TbeTypedDescriptor` and `typed_cmeta_runtime_supported` from PR #55.
- Produces: one executable matrix of eligible and rejected non-container storage families used by later tasks.

- [ ] **Step 1: Add failing matrix assertions**

Add table-driven cases that distinguish generated storage from schema semantics:

```c
static const ExpectedRuntimeCapability EXPECTED[] = {
    { "bool", "uint8_t", CMETA_DATA_BOOL, EXPECT_EXPLICIT_ADAPTER },
    { "uuid", "salts_uuid_t", CMETA_DATA_CUSTOM, EXPECT_EXPLICIT_ADAPTER },
    { "bytes[16]", "uint8_t[16]", CMETA_DATA_BYTES, EXPECT_BOUNDED_ADAPTER },
    { "string", "tstr", CMETA_DATA_STRING, EXPECT_LIFECYCLE },
    { "bytes", "tbe_bytes_t", CMETA_DATA_BYTES, EXPECT_LIFECYCLE },
    { "optional int32", "presence + int32_t", CMETA_DATA_SINT, EXPECT_OVERLAY_PRESENCE },
    { "list<int32>", "vec_t", CMETA_DATA_SEQUENCE, EXPECT_DEFERRED_CONTAINER },
};
```

Assert that currently deferred non-container rows lack `typed_cmeta_runtime_supported`, and that list/set/map stay rejected.

- [ ] **Step 2: Run the focused test and record RED**

Run:

```bash
cmake --build build/linux-gcc-debug --target test_tbe_typed_cmeta_mapping test_tbe_compiler_cmeta_fields -j2
ctest --test-dir build/linux-gcc-debug --output-on-failure \
  -R '^(test_tbe_typed_cmeta_mapping|test_tbe_compiler_cmeta_fields)$'
```

Expected: FAIL only on newly required explicit capability classifications.

- [ ] **Step 3: Add explicit compiler capability annotations**

In `compiler_core.c`, represent each candidate with a named requirement rather than treating every scalar projection as sufficient:

```c
typedef enum tbe_compiler_native_requirement {
  TBE_NATIVE_FIXED_VALUE,
  TBE_NATIVE_ENUM_DOMAIN,
  TBE_NATIVE_OWNED_LIFECYCLE,
  TBE_NATIVE_OVERLAY_PRESENCE,
  TBE_NATIVE_DEFERRED_CONTAINER
} tbe_compiler_native_requirement_t;
```

Do not mark a record runtime-supported until every transitive field requirement has an installed canonical provider.

- [ ] **Step 4: Make the focused matrix GREEN**

Run the Step 2 commands. Expected: all selected tests pass and container rows remain rejected.

- [ ] **Step 5: Update the checked-in matrix and commit**

```bash
git add tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md \
  tbe/data_bind/test_tbe_typed_cmeta_mapping.c \
  tbe/tbe_compiler/compiler_core.c \
  tbe/tbe_compiler/test_cmeta_field_projection.c
git commit -m "test(databind): lock remaining CMeta runtime capabilities"
```

### Task 2: Add exact fixed-value CMeta providers

**Files:**
- Modify: `tbe/data_bind/tbe_typed.h`
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/templates/c_typed_source.mustache`
- Modify: `tbe/data_bind/test_cmeta_graph.schema`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_graph.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public_cpp.cpp`

**Interfaces:**
- Consumes: explicit fixed-value capability rows from Task 1.
- Produces: exact generated Bool-octet, UUID, and bounded-byte native descriptors with semantic-zero/copy operations.

- [ ] **Step 1: Write generated/public RED cases**

Require descriptors for records containing only the new fixed-value families and assert exact storage identity:

```c
const TbeTypedDescriptor *descriptor = FixedValues_typed_descriptor();
check_not_null(descriptor);
check_equal(tbe_typed_descriptor_validate(descriptor, &error), DATA_BIND_OK);
check_equal(shape->fields[0].value->storage_type->size, sizeof(uint8_t));
check_equal(shape->fields[1].value->storage_type->size, sizeof(salts_uuid_t));
check_equal(fixed_bytes_extent(shape->fields[2].value), 16u);
```

Add negative copies with wrong Bool size, UUID alignment, and byte extent. Assert `DATA_BIND_ERR_SCHEMA`, exact field path, and unchanged destination.

- [ ] **Step 2: Verify RED through the production compiler fixture**

```bash
cmake --build build/linux-gcc-debug --target test_tbe_typed_cmeta_graph \
  test_tbe_typed_cmeta_public test_tbe_typed_cmeta_public_cpp -j2
ctest --test-dir build/linux-gcc-debug --output-on-failure \
  -R '^test_tbe_typed_cmeta_(graph|public|public_cpp)$'
```

Expected: generated descriptor symbols are missing or fixed-provider preflight returns schema error.

- [ ] **Step 3: Implement fixed-value providers and traversal**

Add a fixed-value operation contract that never infers storage from the overlay:

```c
typedef struct TbeCMetaFixedOps {
  size_t struct_size;
  void (*zero)(void *storage);
  DataBindStatus (*copy)(void *destination, const void *source,
                         DataBindError *error);
  size_t extent;
} TbeCMetaFixedOps;
```

Generated Bool binds to its octet storage provider; UUID binds to the installed `salts_uuid_t` provider; fixed bytes bind to an extent-specific descriptor emitted by the compiler. Preflight checks kind, size, alignment, extent, and operations.

- [ ] **Step 4: Route parse, serialize, init, clear, binary, and Lua wrappers**

Use descriptor entry points for every newly supported record. The overlay supplies wire offsets and widths only. Remove the fixed-family runtime branches that obtain host kind or size from `TbeTypedField` after their callers are gone.

- [ ] **Step 5: Verify GREEN and commit**

Run Step 2 plus:

```bash
ctest --test-dir build/linux-gcc-debug --output-on-failure \
  -R '^(test_tbe_typed|test_tbe_typed_descriptor_safety|test_tbe_compiler)$'
```

```bash
git add tbe/data_bind tbe/tbe_compiler
git commit -m "feat(databind): consume fixed native values through CMeta"
```

### Task 3: Support signed, unsigned, flags, and wide enum domains

**Files:**
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/data_bind/tbe_typed.h`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/templates/c_typed_source.mustache`
- Modify: `tbe/tbe_compiler/test/enum_conformance.py`
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_graph.c`
- Modify: `tbe/data_bind/test_tbe_typed_descriptor_boundary.c`

**Interfaces:**
- Consumes: canonical CMeta enum descriptors and schema-overlay wire widths.
- Produces: width- and signedness-preserving enum operations, including flags masks and `uint64_t` domains.

- [ ] **Step 1: Add enum-domain RED cases**

Cover `int8_t`, `uint8_t`, `int64_t`, `uint64_t`, flags, maximum unsigned value, unknown enum value, and invalid flag bits. Require no signed narrowing:

```c
uint64_t value = UINT64_MAX;
check_equal(WideEnum_assign(&object, value, &error), DATA_BIND_OK);
check_equal(WideEnum_read(&object, &value, &error), DATA_BIND_OK);
check_equal(value, UINT64_MAX);
```

Assert invalid values preserve the destination and return range/type status rather than schema error.

- [ ] **Step 2: Run enum conformance and observe RED**

```bash
cmake --build build/linux-gcc-debug --target tbe_compiler \
  test_tbe_compiler test_tbe_typed_cmeta_graph \
  test_tbe_typed_descriptor_boundary -j2
ctest --test-dir build/linux-gcc-debug --output-on-failure \
  -R '^(test_tbe_compiler|test_tbe_typed_cmeta_graph|test_tbe_typed_descriptor_boundary)$'
python3 tbe/tbe_compiler/test/enum_conformance.py \
  --compiler build/linux-gcc-debug/bin/tbe_compiler \
  --source "$PWD" --salts-include /opt/salts/debug/include
```

Expected: current signed-64 adapter rejects or truncates the new unsigned/flags cases.

- [ ] **Step 3: Extend canonical enum operations**

Represent domain explicitly:

```c
typedef enum TbeCMetaEnumDomain { TBE_ENUM_SIGNED, TBE_ENUM_UNSIGNED } TbeCMetaEnumDomain;
typedef struct TbeCMetaEnumOps {
  TbeCMetaEnumDomain domain;
  uint8_t width;
  bool is_flags;
  uint64_t declared_mask;
  DataBindStatus (*read_bits)(const void *, uint64_t *, DataBindError *);
  DataBindStatus (*assign_bits)(void *, uint64_t, DataBindError *);
} TbeCMetaEnumOps;
```

Generated operations use the actual enum storage type. Do not cast `uint64_t` through `int64_t`. Non-flags validate declared constants; flags validate the declared mask.

- [ ] **Step 4: Delete parallel native enum inference**

Remove runtime branches that select native enum storage from `wire_kind`. Keep only overlay wire width/byte-order decoding, then pass canonical bits to CMeta enum assignment.

- [ ] **Step 5: Verify GREEN and commit**

Run Step 2 and the complete TBE compiler test. Then:

```bash
git add tbe/data_bind tbe/tbe_compiler
git commit -m "feat(databind): preserve canonical enum storage domains"
```

### Task 4: Move owned strings, bytes, and custom scalars to CMeta lifecycle

**Files:**
- Modify: `tbe/data_bind/tbe_typed.h`
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/templates/c_typed_source.mustache`
- Modify: `tbe/data_bind/test_tbe_typed_descriptor_safety.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_graph.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public_cpp.cpp`

**Interfaces:**
- Consumes: canonical storage identity and DataBind allocation/limit reporting.
- Produces: explicit zero/clone/move/clear/from-value/to-value operations for owned scalar storage.

- [ ] **Step 1: Write lifecycle and rollback RED cases**

Use a counting allocator/provider to prove exact cleanup without mocks of DataBind traversal:

```c
check_equal(OwnedRecord_parse_json(codec, failing_json, &destination, &error),
            DATA_BIND_ERR_LIMIT);
check_equal(provider.allocations, provider.clears);
check_equal(destination.name, original_name);
check_equal(destination.payload.data, original_payload);
```

Cover success replacement, allocation failure after an earlier field succeeds, type mismatch, byte limit, nested owned fields, and a custom scalar with and without an installed adapter.

- [ ] **Step 2: Verify lifecycle RED**

```bash
cmake --build build/linux-gcc-debug --target test_tbe_typed_descriptor_safety \
  test_tbe_typed_cmeta_graph test_tbe_typed_cmeta_public \
  test_tbe_typed_cmeta_public_cpp -j2
ctest --test-dir build/linux-gcc-debug --output-on-failure \
  -R '^test_tbe_typed_(descriptor_safety|cmeta_graph|cmeta_public|cmeta_public_cpp)$'
```

Expected: descriptor preflight rejects missing lifecycle operations and generated owned records have no descriptor.

- [ ] **Step 3: Add canonical lifecycle operations**

```c
typedef struct TbeCMetaLifecycleOps {
  size_t struct_size;
  DataBindStatus (*init_zero)(void *, DataBindError *);
  DataBindStatus (*clone)(void *, const void *, DataBindError *);
  void (*clear)(void *);
  DataBindStatus (*from_value)(void *, const DataBindValue *, DataBindError *);
  DataBindStatus (*to_value)(const void *, DataBindValue **, DataBindError *);
} TbeCMetaLifecycleOps;
```

Require the complete operation set for owned storage. Unknown custom scalars remain unsupported. Conversion uses CMeta field offsets and operations, never `TbeTypedKind` host dispatch.

- [ ] **Step 4: Implement transactional object publication**

Initialize a temporary through CMeta, populate it recursively, clear it on any failure, and only then clear and replace the destination. Serialization borrows storage. Preserve the first failure code and path during cleanup.

- [ ] **Step 5: Remove superseded owned-type helpers, verify, and commit**

Delete private string/bytes/custom init, clear, and conversion branches after `rg` proves no descriptor-backed caller remains. Run Step 2 plus ASan/UBSan focused tests.

```bash
git add tbe/data_bind tbe/tbe_compiler
git commit -m "feat(databind): use CMeta lifecycle for owned scalars"
```

### Task 5: Compose optional presence with canonical value storage

**Files:**
- Modify: `tbe/data_bind/tbe_typed.h`
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/templates/c_typed_source.mustache`
- Modify: `tbe/data_bind/test_tbe_typed_optional_binary.c`
- Modify: `tbe/data_bind/test_tbe_typed_optional_edges.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_graph.c`
- Modify: `tbe/data_bind/test_tbe_typed_descriptor_boundary.c`

**Interfaces:**
- Consumes: CMeta value lifecycle from Tasks 2–4 and overlay presence/default metadata.
- Produces: descriptor-backed optional parse/serialize with atomic value and presence publication.

- [ ] **Step 1: Add optional composition RED cases**

Cover absent/no-default, absent/default, present, invalid default, owned-value allocation failure, nested optional, overlapping bitmap/value storage, and rollback:

```c
check_equal(OptionalOwned_parse_json(codec, "{}", 2, &object, &error), DATA_BIND_OK);
check(!OptionalOwned_has_name(&object));
check(tstr_empty(&object.name));

check_equal(OptionalOwned_parse_json(codec, bad_json, bad_len, &object, &error),
            DATA_BIND_ERR_TYPE_MISMATCH);
check_equal(object._presence, original_presence);
check_equal(tstr_cmp(object.name, original_name), 0);
```

- [ ] **Step 2: Verify optional RED**

```bash
cmake --build build/linux-gcc-debug --target test_tbe_typed_optional_binary \
  test_tbe_typed_cmeta_graph test_tbe_typed_descriptor_boundary -j2
ctest --test-dir build/linux-gcc-debug --output-on-failure \
  -R '^(test_tbe_typed_optional_binary|test_tbe_typed_cmeta_graph|test_tbe_typed_descriptor_boundary)$'
```

Expected: generated optional records lack descriptors or preflight rejects presence composition.

- [ ] **Step 3: Add overlay-only presence description**

Keep presence coordinates out of CMeta:

```c
typedef struct TbeTypedPresence {
  size_t bitmap_offset;
  size_t bitmap_size;
  size_t bit_index;
} TbeTypedPresence;
```

Preflight validates bounds and non-overlap against the canonical record shape. The native value field continues to come from CMeta.

- [ ] **Step 4: Implement default conversion and atomic presence publication**

Convert defaults through the same canonical CMeta adapter as parsed input. Set the temporary presence bit only after successful value conversion. Publish the complete temporary object only after all fields succeed.

- [ ] **Step 5: Remove optional native-structure duplication and commit**

Remove `presence_offset`, native optional kind/offset inference, and private optional init/clear access from descriptor-backed paths after their last callers disappear. Keep only overlay presence coordinates.

```bash
git add tbe/data_bind tbe/tbe_compiler
git commit -m "feat(databind): compose optional presence with CMeta storage"
```

### Task 6: Prove every generated wrapper uses the descriptor path

**Files:**
- Modify: `tbe/tbe_compiler/templates/c_typed_source.mustache`
- Modify: `tbe/tbe_compiler/templates/c_lua_bind.mustache`
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public_cpp.cpp`
- Modify: `tbe/data_bind/CMakeLists.txt`

**Interfaces:**
- Consumes: all non-container capabilities from Tasks 2–5.
- Produces: generated lifecycle, format, binary, and Lua wrappers with no reachable graphless runtime fallback.

- [ ] **Step 1: Add source and link-time RED gates**

Require each supported wrapper body to call `tbe_typed_descriptor_*`. Add a test translation unit that deliberately omits legacy raw runtime symbols; it must still link. Add forbidden-source scans for descriptor-to-raw fallback.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build/linux-gcc-debug --target test_tbe_compiler \
  test_tbe_typed_cmeta_public test_tbe_typed_cmeta_public_cpp -j2
ctest --test-dir build/linux-gcc-debug --output-on-failure \
  -R '^(test_tbe_compiler|test_tbe_typed_cmeta_public|test_tbe_typed_cmeta_public_cpp)$'
```

Expected: at least one newly supported wrapper still references a raw typed entry point.

- [ ] **Step 3: Cut wrappers over and remove last structural callers**

Generate descriptor-only wrappers for supported records. Unsupported container records expose no descriptor and retain only explicitly compile-time raw APIs pending #46. Delete native kind, host offset/size, nesting, and lifecycle fields from `TbeTypedField`/`TbeTypedType` only when `rg` and compilation prove no descriptor path reads them.

- [ ] **Step 4: Verify source identity and commit**

Run Step 2, `git diff --check`, and forbidden scans for CBind, compatibility, fallback, and removed reverse-graph helpers.

```bash
git add tbe/data_bind tbe/tbe_compiler
git commit -m "refactor(databind): make generated native wrappers descriptor-only"
```

### Task 7: Verify installed and sanitizer consumers

**Files:**
- Modify: `cron/tests/package_consumer/CMakeLists.txt`
- Modify: `cron/tests/package_consumer/main.c`
- Modify: `.github/workflows/tbe-30-green.yml`
- Modify: `.github/workflows/tbe-34-enum-conformance.yml`
- Modify: `.github/workflows/tbe-32-33-schema-boundaries.yml`
- Modify: `.github/workflows/databind-direct-parsers.yml`

**Interfaces:**
- Consumes: descriptor-only non-container runtime from Task 6.
- Produces: installed-package and exact-head evidence across public consumers and sanitizer builds.

- [ ] **Step 1: Add installed-consumer RED coverage**

Build generated C into a static library, consume it from C and C++, exercise fixed, enum, owned, and optional records, and assert container descriptor symbols are absent.

- [ ] **Step 2: Run local install/consumer and sanitizer gates**

```bash
cmake --build build/linux-gcc-debug --target install -j2
cmake -S cron/tests/package_consumer -B build/package-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/package-consumer -j2
ctest --test-dir build/package-consumer --output-on-failure
ctest --test-dir build/linux-gcc-debug --output-on-failure
```

Expected before fixture completion: installed consumer fails to compile or link a newly required descriptor case.

- [ ] **Step 3: Complete only required export dependencies**

Expose the CMeta/lifecycle headers and targets required by public descriptors. Do not export CBind or private compiler/runtime helpers.

- [ ] **Step 4: Run full local verification and commit**

```bash
git diff --check
cmake --build build/linux-gcc-debug --target tbe/all -j2
ctest --test-dir build/linux-gcc-debug --output-on-failure
```

```bash
git add cron/tests/package_consumer .github/workflows tbe
git commit -m "test(databind): gate complete canonical CMeta runtime"
```

### Task 8: Publish exact-head evidence and close #47

**Files:**
- Modify: `tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md`
- Modify: `docs/superpowers/specs/2026-09-14-databind-cmeta-runtime-completion-design.md` only if implementation required an approved clarification

**Interfaces:**
- Consumes: complete GREEN implementation and exact-head GitHub Actions results.
- Produces: reviewable PR evidence and an accurate #47 closure record.

- [ ] **Step 1: Reconcile the capability matrix and migration notes**

Confirm every non-container row names its canonical CMeta descriptor and operations. Confirm list/set/map rows explicitly say `deferred to #46`. Remove no limitation that is still observable.

- [ ] **Step 2: Run final forbidden and ownership scans**

```bash
rg -n "CBind|Salts::CBind|compat|fallback" tbe cron/tests/package_consumer .github/workflows
rg -n "object_type|element_kind|map_value_kind|presence_offset" \
  tbe/data_bind/tbe_typed.c tbe/tbe_compiler/templates/c_typed_source.mustache
git diff --check
git status --short
```

Classify every remaining hit as overlay-only, compile-time raw container metadata for #46, or a defect. Do not waive unexplained hits.

- [ ] **Step 3: Push the review branch and wait for exact-head CI**

Required successful workflows on the same head SHA:

- DataBind direct parsers;
- TBE schema boundaries;
- TBE enum conformance;
- TBE descriptor safety.

Do not claim GREEN from superseded runs.

- [ ] **Step 4: Update the PR and issues**

The PR body must list the exact head and run IDs, implemented type families, removed structural metadata, and the #46 container exclusion. Close #47 only after merge. Keep #46 and #48 open.

- [ ] **Step 5: Final commit if documentation changed**

```bash
git add tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md \
  docs/superpowers/specs/2026-09-14-databind-cmeta-runtime-completion-design.md
git commit -m "docs(databind): record complete canonical runtime boundary"
```

## Plan self-review

- Spec coverage: all four approved slices, ownership boundaries, error/status preservation, transactional rollback, capability removal, generated/public/install consumers, and exact-head CI have explicit tasks.
- Scope: list/set/map remain in #46; file decomposition remains in #48.
- Type consistency: fixed, enum, lifecycle, and presence contracts flow in that order and later tasks consume earlier providers.
- No task permits fallback, inferred storage, CBind delegation, or dynamic-root materialization.
