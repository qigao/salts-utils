# DataBind Canonical CMeta Native Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the first supported set of generated DataBind scalar, Struct and Enum records consume the canonical CMeta graph as its only structural source, while wire and validation policy remains in a separate schema overlay.

**Architecture:** A versioned `TbeTypedDescriptor` joins one immutable `cmeta_data_desc` native graph to one `TbeTypedType` wire overlay. Descriptor-based code resolves size, alignment, semantic kind, field order, native name and native offset from CMeta; it reads only external names, wire layout and validation flags from the overlay. The compiler marks a record descriptor-routable only when its entire native graph is in this slice; that generation-time partition is not a runtime fallback. ABI-v1, graphless, malformed or partially supported descriptors fail with `DATA_BIND_ERR_SCHEMA`; no old-kind fallback, forwarding facade, compatibility switch or inferred C ABI is added.

**Tech Stack:** C11, CMake 3.27+, Salts CMeta/CSTL/CSerde, DataBind, generated Mustache C, TinyTest, GitHub Actions ASan/UBSan.

**Spec:** `docs/superpowers/specs/2026-09-12-databind-convergence-design.md`

## Global Constraints

- DataBind remains SaltsUtils' sole public binding engine.
- CMeta owns semantic type identity and native structural reflection.
- The DataBind schema overlay owns external names and aliases, presence/defaults, binary wire offsets and widths, byte order, validation and fingerprints.
- CSTL owns concrete container storage and lifecycle; CSerde/parsers own format tokens and mechanics.
- Compare semantic types with `cmeta_type_equal`; descriptor pointer identity is never a type oracle.
- A migrated descriptor must never delegate to `TbeTypedKind`, `TbeTypedField.offset` or `TbeTypedType.size` as a fallback.
- Parse into semantic-zero temporary storage and publish only after complete success; every error leaves the caller's destination byte-for-byte unchanged.
- Unsupported or inconsistent graphs fail explicitly as `DATA_BIND_ERR_SCHEMA` with the most specific available type/field path.
- This slice covers records whose complete transitive graph contains only non-optional fixed-width signed/unsigned integers, F32/F64, non-flags Enum storage representable through `int64_t`, and nested Struct fields with the same property.
- Bool is excluded because generated storage is currently `uint8_t` while CMeta's canonical Bool storage descriptor is not. Flags and uint64-wide Enum domains are excluded because the v1 enum adapter is signed-64 only. STRING/BYTES lifecycle, fixed buffers, UUID/custom adapters, optional presence and native sequence/set/map storage are also excluded.
- Unsupported generated records keep their existing raw wrappers in this slice but receive no ABI-v2 descriptor/getter. No migrated descriptor may inspect or invoke that raw path. Therefore this plan delivers the first #47 slice and deliberately leaves #47 open.
- #46 remains the dynamic CMeta/CSTL container migration. #48 remains the later file/module split.
- Local `cmake` and `ctest` are unavailable in the planning environment; native proof must come from one immutable exact-head CI SHA.

## Structural Inventory and Ownership Map

| Current metadata | Current readers/producers | Canonical owner after this slice |
| --- | --- | --- |
| `TbeTypedField.kind`, `offset` | `typed_init_value`, `typed_clear_value`, `typed_from_one`, `typed_from_value_at`, `typed_scalar_json`, `typed_one_json`, `typed_validate_descriptor_at`, `typed_validate_layout_at`, binary readers/writers; `compiler_core.c`; `c_typed_source.mustache` | `cmeta_data_field_desc.value->kind`, `cmeta_data_field_desc.offset`, matching `cmeta_field_desc`, and `cmeta_type_desc` |
| `TbeTypedField.object_type` | recursive raw init/clear/from/to-json/schema/binary helpers; generator typed-object annotations | native nesting comes from a child `cmeta_data_desc`; migrated wire recursion uses a separately named `nested_overlay` pointer, while `object_type` remains raw/deferred only |
| `TbeTypedField.element_kind`, `element_size`, `fixed_count` | collection/fixed-array lifecycle, conversion and binary helpers; generator annotations | deferred; a future explicit CMeta range/container contract, outside this slice |
| `TbeTypedField.map_entry_size`, `map_key_offset`, `map_value_offset`, `map_value_kind`, `map_value_type` | map lifecycle and conversion helpers; generator annotations | deferred to #46/native-container contract; never inferred here |
| `TbeTypedField.name` | schema lookup, errors and output naming | schema-overlay row external name; joined by stable field order/ID to CMeta native field |
| `wire_kind`, `element_wire_kind`, `map_value_wire_kind`, `wire_offset`, `wire_size`, `optional_bit`, `flags` | schema validation and binary/text orchestration | schema overlay |
| `TbeTypedType.size`, native use of `fields`/`field_count` | allocation, bounds, traversal and recursion | root `cmeta_data_desc.storage_type` and `cmeta_data_struct_shape` |
| `TbeTypedType.presence_offset` | optional native bitmap access | deferred optional-native contract; not consumed by a migrated descriptor in this slice |
| `TbeTypedType.name`, `fixed_block_size`, wire use of `presence_size`, `wire_big_endian` | schema diagnostics and wire layout | schema overlay |
| `typed_scalar_cmeta_mappings`, `tbe_typed_kind_from_cmeta_data` | converts CMeta back into the parallel typed-kind system | delete when the last migrated descriptor caller is removed |
| `typed_cmeta_validate_record`, `tbe_typed_cmeta_graph_validate` | validates CMeta against old typed structural facts | replace with CMeta-first descriptor validation; CMeta is authoritative |

---

### Task 1: Add the Public CMeta-Authority RED

**Files:**
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public.c`
- Modify: `tbe/data_bind/CMakeLists.txt`

**Interfaces:**
- Consumes: generated `Sample_cmeta_data`, generated typed parse APIs, public `cmeta_data_desc`, `cmeta_data_struct_shape`, `cmeta_data_field_desc`, `cmeta_type_desc` and `cmeta_type_identity`.
- Produces: a public C regression proving semantic equality is accepted and a bad native offset is rejected atomically.

- [ ] **Step 1: Expose the generated descriptor in the fixture expectation**

Add a compile-time use of this exact generated declaration to the test:

```c
const TbeTypedDescriptor *descriptor = Sample_typed_descriptor();
if (descriptor == NULL || descriptor->native_data == NULL) return 16;
```

The initial RED may fail to compile because `Sample_typed_descriptor` and `native_data` do not exist.

- [ ] **Step 2: Write the semantic-copy success case**

Deep-copy the root descriptor, root struct shape and field array. For the `count` field, also copy its `cmeta_data_desc`, `cmeta_type_desc` and `cmeta_type_identity`, reconnect the copied pointers, and assert:

```c
if (&count_data_copy == count_field->value) return 17;
if (!cmeta_type_equal(count_data_copy.storage_type,
                      count_field->value->storage_type)) return 18;

descriptor_copy.native_data = &root_data_copy;
status = tbe_typed_descriptor_parse(codec, "Sample", &descriptor_copy,
                                    DATA_BIND_FORMAT_JSON,
                                    json, strlen(json), 0u, &actual, &error);
if (status != DATA_BIND_OK || actual.count != 7) return 19;
```

This test must use distinct descriptor addresses. It rejects implementations that compare CMeta descriptors by pointer.

- [ ] **Step 3: Write the mismatched-offset atomic rejection**

Create a second copied graph, change only the copied semantic `count` field offset so it disagrees with `shape->layout`, seed the destination with a non-zero valid `Sample_t`, and preserve a byte copy:

```c
bad_fields[count_index].offset = offsetof(Sample_t, state);
before = actual;
descriptor_copy.native_data = &bad_root_data;
status = tbe_typed_descriptor_parse(codec, "Sample", &descriptor_copy,
                                    DATA_BIND_FORMAT_JSON,
                                    json, strlen(json), 0u, &actual, &error);
if (status != DATA_BIND_ERR_SCHEMA) return 20;
if (strstr(error.path, "Sample.count") == NULL) return 21;
if (memcmp(&actual, &before, sizeof(actual)) != 0) return 22;
```

The replacement offset is aligned and remains inside `sizeof(Sample_t)`, so
the test proves an exact field/layout mismatch is rejected rather than merely
tripping the root bounds check.

- [ ] **Step 4: Register a focused CTest label**

Keep the existing `test_tbe_typed_cmeta_public` target and add:

```cmake
set_tests_properties(test_tbe_typed_cmeta_public PROPERTIES
  LABELS "databind;cmeta;typed-contract")
```

- [ ] **Step 5: Verify RED**

Run where CMake is available:

```bash
cmake --build build/linux-gcc-debug --target test_tbe_typed_cmeta_public -j2
ctest --test-dir build/linux-gcc-debug --no-tests=error \
  -R '^test_tbe_typed_cmeta_public$' --output-on-failure
```

Expected through Task 3: compile failure for the missing descriptor getter or
a runtime failure because the descriptor path still treats the old typed graph
as authoritative. Task 4 is the first task that makes this generated-output
contract GREEN.

- [ ] **Step 6: Commit the RED**

```bash
git add tbe/data_bind/test_tbe_typed_cmeta_public.c tbe/data_bind/CMakeLists.txt
git commit -m "test(databind): require canonical CMeta typed authority"
```

### Task 2: Make the Versioned Descriptor Require Canonical Native Data

**Files:**
- Modify: `tbe/data_bind/tbe_typed.h`
- Modify: `tbe/data_bind/tbe_typed_internal.h`
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/data_bind/test_tbe_typed_descriptor_boundary.c`
- Modify: `tbe/data_bind/test_tbe_typed.c`

**Interfaces:**
- Consumes: a validated CMeta root with `CMETA_DATA_STRUCT` and an existing `TbeTypedType` wire overlay.
- Produces: ABI-v2 `TbeTypedDescriptor`, recursive CMeta-first preflight through `typed_descriptor_native_record`, and public descriptor lifecycle/binary entry points.

- [ ] **Step 1: Write ABI rejection tests**

Add cases to `test_tbe_typed_descriptor_boundary.c` for a v1-sized descriptor, `native_data == NULL`, non-Struct root data, invalid struct shape, and mismatched overlay/CMeta field counts. Each must return `DATA_BIND_ERR_SCHEMA`; parsing into a seeded destination must not change it.

Use one valid empty native record as the baseline, then copy and mutate one boundary at a time:

```c
static const cmeta_type_identity BOUNDARY_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.BoundaryStorage");
static const cmeta_type_desc BOUNDARY_CMETA_TYPE = {
    "BoundaryStorage", sizeof(BoundaryStorage), _Alignof(BoundaryStorage),
    CMETA_T_OBJECT, NULL, NULL, &BOUNDARY_ID};
static const cmeta_struct_desc BOUNDARY_LAYOUT = {
    "BoundaryStorage", sizeof(BoundaryStorage), _Alignof(BoundaryStorage),
    NULL, 0u};
static const cmeta_data_struct_shape BOUNDARY_SHAPE = {
    &BOUNDARY_LAYOUT, NULL, 0u};
static const cmeta_data_desc BOUNDARY_DATA = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.BoundaryStorage.data", "BoundaryStorage", CMETA_DATA_STRUCT,
    &BOUNDARY_CMETA_TYPE, &BOUNDARY_SHAPE, NULL, NULL, NULL};
```

For the field-count rejection, set a copied overlay's `field_count` to `1u` with one valid overlay row while leaving the copied shape/layout counts at zero. For the non-Struct rejection, copy `BOUNDARY_DATA` and change only `kind` to `CMETA_DATA_BOOL`.

- [ ] **Step 2: Replace the descriptor boundary in one breaking cutover**

Change the public declaration to:

```c
enum { TBE_TYPED_DESCRIPTOR_ABI_VERSION = 2 };

typedef struct TbeTypedDescriptor {
  size_t struct_size;
  uint32_t abi_version;
  const TbeTypedType *overlay;
  const cmeta_data_desc *native_data;
} TbeTypedDescriptor;

#define TBE_TYPED_DESCRIPTOR_INIT(OVERLAY, NATIVE_DATA) \
  { sizeof(TbeTypedDescriptor), TBE_TYPED_DESCRIPTOR_ABI_VERSION, \
    (OVERLAY), (NATIVE_DATA) }
```

Include the public CMeta data header from `tbe_typed.h`. Do not retain a union, legacy initializer or alternate ABI-v1 acceptance branch.

Add `const TbeTypedType *nested_overlay` to `TbeTypedField`. Descriptor-based Struct recursion may read only this overlay association; native child kind, storage and offset still come from the paired child `cmeta_data_desc`. The existing `object_type` member remains private to raw deferred paths and must be `NULL` in generated migrated rows.

Remove the automatic `BINDING##_descriptor` declaration from `TBE_TYPED_PRIVATE_DEFINE_STRUCT`. The header-only raw binding macros continue to produce `TbeTypedType` metadata only for explicitly deferred paths; they do not synthesize graphless descriptors. In `test_tbe_typed.c`, give `MacroWire` an explicit canonical CMeta graph and construct its ABI-v2 descriptor with `TBE_TYPED_DESCRIPTOR_INIT(&MACRO_WIRE_BINDING, &MACRO_WIRE_CMETA_DATA)`. Update the incompatible-boundary case to mutate `abi_version` or `native_data`, not a removed ABI-v1 field.

- [ ] **Step 3: Add one CMeta-first resolver**

Declare internally:

```c
typedef struct TypedNativeRecord {
  const cmeta_data_desc *data;
  const cmeta_data_struct_shape *shape;
  const TbeTypedType *overlay;
} TypedNativeRecord;

static DataBindStatus typed_descriptor_native_record(
    const TbeTypedDescriptor *descriptor,
    TypedNativeRecord *out,
    DataBindError *error);
```

The resolver must preflight the complete graph without mutating caller storage.
Use an ancestor-pointer stack of 33 `cmeta_data_desc` entries for levels 0 through
32: accept an acyclic root-to-leaf path of depth 32, reject depth 33, and reject a descriptor
that reappears in its current ancestor chain. Validate each field locally
before descending: Struct kind, storage size/alignment, shape/layout presence,
equal semantic/layout/overlay field counts, the semantic field's matching
layout row, `cmeta_type_equal(layout->type, field->value->storage_type)`, exact
offset/size/alignment, and root bounds. Build the native path from the current
record and field name so a field-local mismatch reports `Sample.count`; do not
call the generic `cmeta_data_desc_valid` until after these specific checks,
because its pathless failure must not erase a more precise diagnostic. Join
overlay row `i` to semantic field `i`; never repair either side.

- [ ] **Step 4: Add descriptor lifecycle and binary APIs**

Declare these exact public functions so generated wrappers no longer bypass the descriptor:

```c
DATA_BIND_API DataBindStatus tbe_typed_descriptor_init(
    const TbeTypedDescriptor *descriptor, void *object, DataBindError *error);
DATA_BIND_API DataBindStatus tbe_typed_descriptor_clear(
    const TbeTypedDescriptor *descriptor, void *object,
    DataBindError *error);
DATA_BIND_API DataBindStatus tbe_typed_descriptor_serialize_binary(
    const TbeTypedDescriptor *descriptor, const void *object,
    uint8_t **out, size_t *out_len, DataBindError *error);
DATA_BIND_API DataBindStatus tbe_typed_descriptor_serialize_binary_into(
    const TbeTypedDescriptor *descriptor, const void *object,
    uint8_t *output, size_t capacity, size_t *out_len,
    DataBindError *error);
```

All four must call `typed_descriptor_native_record` before reading object
storage. `tbe_typed_descriptor_clear` returns `DATA_BIND_ERR_SCHEMA` and leaves
storage unchanged when preflight fails; a generated legacy-void `*_clear`
wrapper for a supported static descriptor explicitly discards only that status
with `(void)`. ABI-v1 and graphless descriptors fail; there is no call to a raw
`TbeTypedType` fallback.

- [ ] **Step 5: Verify the descriptor boundary**

```bash
cmake --build build/linux-gcc-debug --target \
  test_tbe_typed_descriptor_boundary -j2
ctest --test-dir build/linux-gcc-debug --no-tests=error \
  -R '^test_tbe_typed_descriptor_boundary$' \
  --output-on-failure
```

Expected: ABI and hand-authored graph boundary tests pass. The generated public
test from Task 1 remains RED until Task 4 emits `Sample_typed_descriptor`.

- [ ] **Step 6: Commit**

```bash
git add tbe/data_bind/tbe_typed.h tbe/data_bind/tbe_typed_internal.h \
  tbe/data_bind/tbe_typed.c tbe/data_bind/test_tbe_typed_descriptor_boundary.c \
  tbe/data_bind/test_tbe_typed.c
git commit -m "refactor(databind): require CMeta on typed descriptors"
```

### Task 3: Convert Scalar, Struct and Enum Through the CMeta Graph

**Files:**
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/data_bind/test_tbe_typed.c`
- Modify: `tbe/data_bind/test_tbe_typed_descriptor_boundary.c`

**Interfaces:**
- Consumes: `TypedNativeRecord` and overlay row `i` joined to `cmeta_data_field_desc` `i`.
- Produces: descriptor-native init, clear, JSON/value conversion, schema validation and binary conversion for fixed-width integer/float, Struct and adapter-backed non-flags Enum storage.

- [ ] **Step 1: Add behavior REDs for every migrated semantic class**

Add descriptor-based round trips for signed/unsigned integer widths, F32/F64,
nested Struct and Enum. Define the Enum fixture with a complete hand-authored
`cmeta_data_enum_ops` whose callbacks read and assign the exact fixture enum
typedef through `int64_t`; do not cast an arbitrary object through a wire kind.
For each class, mutate the legacy `TbeTypedField.kind`, `offset`, nested
`object_type`, and root `TbeTypedType.size` in a copied overlay while keeping
CMeta valid; the descriptor path must still use CMeta and produce the same
result. Add explicit schema-error cases for `CMETA_DATA_BOOL`, a missing Enum
operations provider, and uint64-wide Enum domains. Mutating wire fields must still change or reject
wire behavior, proving the overlay remains authoritative only for schema/wire
policy.

- [ ] **Step 2: Introduce semantic dispatch without `TbeTypedKind`**

Implement descriptor-only helpers with CMeta arguments:

```c
static DataBindStatus typed_native_init_value(
    const cmeta_data_desc *data, void *storage,
    const char *path, DataBindError *error);
static void typed_native_clear_value(
    const cmeta_data_desc *data, void *storage);
static DataBindStatus typed_native_from_value(
    const cmeta_data_desc *data, const TbeTypedType *overlay,
    const DataBindValue *value, void *storage,
    const char *path, DataBindError *error);
static json_value_t *typed_native_to_json(
    const cmeta_data_desc *data, const TbeTypedType *overlay,
    const void *storage, const char *path, DataBindError *error);
```

Dispatch on `cmeta_data_desc.kind`. For scalar storage, reuse or rename the
existing `typed_cmeta_scalar_matches` predicate and require all of: equal data
kind, `cmeta_type_equal` semantic identity, equal `cmeta_type_kind`, size and
alignment, plus equal integer/float bit shape. Do not map the result back to
`TbeTypedKind`. For Enum, require a complete storage-matching
`cmeta_data_enum_ops` and call only `cmeta_data_enum_is_zero`,
`cmeta_data_enum_read`, `cmeta_data_enum_assign` and
`cmeta_data_enum_restore_zero`; the overlay's wire kind cannot define native
signedness or storage. For Struct, recurse through the semantic field
descriptors and the paired row's `nested_overlay`. Reject Bool, an Enum without
a complete operations provider, uint64-wide Enum domains and every other deferred kind as
`DATA_BIND_ERR_SCHEMA` during preflight, before mutation.

- [ ] **Step 3: Make temporary allocation CMeta-owned**

In descriptor parse paths, allocate and zero exactly `native_data->storage_type->size`, validate `storage_type->align`, initialize through `typed_native_init_value`, convert, then publish. Cleanup must use `typed_native_clear_value`. Remove descriptor-path reads of `overlay->size`, `field->offset`, `field->kind` and `field->object_type`.

- [ ] **Step 4: Make schema and binary access use native CMeta fields**

Pass the resolved semantic field descriptor into schema and binary helpers. Compute every native address as:

```c
void *field_storage = (uint8_t *)object + native_field->offset;
```

Continue reading `name`, `wire_kind`, `wire_offset`, `wire_size`, byte order and validation flags from the overlay row. A schema name or wire mismatch is an overlay error; a native offset/type/layout mismatch is a CMeta graph error.

- [ ] **Step 5: Delete the reverse-authority path from migrated calls**

Remove descriptor-path calls to `typed_validate_descriptor_at`, `tbe_typed_kind_from_cmeta_data`, `typed_cmeta_validate_record` and `tbe_typed_cmeta_graph_validate`. Raw `TbeTypedType` APIs may remain only for explicitly deferred kinds; descriptor-based scalar/Struct/Enum code must not call them.

- [ ] **Step 6: Run focused and full DataBind tests**

```bash
cmake --build build/linux-gcc-debug --target \
  test_tbe_typed test_tbe_typed_descriptor_boundary \
  test_data_bind_schema_reflection_contract -j2
ctest --test-dir build/linux-gcc-debug --no-tests=error \
  -R '^(test_tbe_typed|test_tbe_typed_descriptor_boundary|test_data_bind_schema_reflection_contract)$' \
  --output-on-failure
```

Expected: these hand-authored runtime tests pass under ASan/UBSan while Task 1
remains RED; Bool, provider-less or uint64-wide Enum, optional,
container and custom cases fail explicitly rather than selecting old metadata.

- [ ] **Step 7: Commit**

```bash
git add tbe/data_bind/tbe_typed.c tbe/data_bind/test_tbe_typed.c \
  tbe/data_bind/test_tbe_typed_descriptor_boundary.c
git commit -m "refactor(databind): drive typed conversion from CMeta"
```

### Task 4: Generate One Joined Descriptor and Route Public Wrappers Through It

**Files:**
- Modify: `tbe/tbe_compiler/compiler_core.c`
- Modify: `tbe/tbe_compiler/templates/c_typed_source.mustache`
- Modify: `tbe/tbe_compiler/templates/c_structs.mustache`
- Modify: `tbe/tbe_compiler/templates/c_lua_bind.mustache`
- Modify: `tbe/tbe_compiler/test_cmeta_field_projection.c`
- Modify: `tbe/tbe_compiler/test_tbe_compiler.c`
- Modify: `tools/lua/salts_lua.h`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public_cpp.cpp`

**Interfaces:**
- Consumes: generated immutable `*_CMETA_DATA` plus generated wire-overlay rows.
- Produces: `typed_cmeta_runtime_supported`, generated enum operations, public `const TbeTypedDescriptor *<Record>_typed_descriptor(void)` for supported records, and descriptor-routed generated lifecycle/parse/serialize/Lua wrappers for that same set.

- [ ] **Step 1: Write generated-output RED assertions**

Extend compiler/template tests to require:

```c
static const TbeTypedDescriptor Sample_TYPED_DESCRIPTOR =
    TBE_TYPED_DESCRIPTOR_INIT(&Sample_TYPED_TYPE, &Sample_CMETA_DATA);

const TbeTypedDescriptor *Sample_typed_descriptor(void) {
  return &Sample_TYPED_DESCRIPTOR;
}
```

Also assert supported generated wrappers call `tbe_typed_descriptor_init`,
`tbe_typed_descriptor_clear`, `tbe_typed_descriptor_parse`,
`tbe_typed_descriptor_serialize`, `tbe_typed_descriptor_serialize_binary` and
`tbe_typed_descriptor_serialize_binary_into`. Assert the unsupported
`LoginMessage` fixture has no `LoginMessage_typed_descriptor` declaration and
retains its raw wrapper calls, proving generation-time partition rather than a
runtime fallback.

- [ ] **Step 2: Classify the complete graph at generation time**

After field and enum annotations exist, compute
`typed_cmeta_runtime_supported` for every composite, group and message with a
tri-state DFS (`unvisited`, `visiting`, `supported`/`unsupported`). A record is
supported only when every transitive field is non-optional and is one of:

```text
int8/uint8/int16/uint16/int32/uint32/int64/uint64/float/double
non-flags enum whose native domain is representable by int64_t
composite/group/message already classified as supported
```

Mark a cycle, Bool, string/bytes/fixed buffer, UUID/custom, list/set/map,
optional field, flags enum or uint64-wide enum unsupported. Set
`typed_cmeta_runtime_supported` only after all children return supported; never
guess support from the root's immediate fields. Extend
`test_cmeta_field_projection.c` with one supported nested fixture and one
fixture for each excluded family, and assert the exact marker presence/absence.

- [ ] **Step 3: Generate CMeta-owned enum operations**

For every non-flags enum accepted by the classifier, emit four static callbacks
that access the exact generated enum typedef, then attach one operations table:

```c
static bool State_cmeta_is_zero(const void *object) {
  return object != NULL && *(const State_t *)object == (State_t)0;
}
static cmeta_status State_cmeta_read(const void *object, int64_t *out) {
  if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  *out = (int64_t)*(const State_t *)object;
  return CMETA_OK;
}
static cmeta_status State_cmeta_assign(void *object, int64_t value) {
  if (object == NULL) return CMETA_INVALID_ARGUMENT;
  *(State_t *)object = (State_t)value;
  return CMETA_OK;
}
static void State_cmeta_restore_zero(void *object) {
  if (object != NULL) *(State_t *)object = (State_t)0;
}
static const cmeta_data_enum_ops State_CMETA_ENUM_OPS = {
  sizeof(cmeta_data_enum_ops), CMETA_DATA_ENUM_OPS_ABI_VERSION,
  &State_CMETA_TYPE, State_cmeta_is_zero, State_cmeta_read,
  State_cmeta_assign, State_cmeta_restore_zero
};
```

Set `State_CMETA_DATA.enum_ops = &State_CMETA_ENUM_OPS`. The runtime facade
checks declaration membership before assign/read succeeds, so the generated
callbacks do not create an overlay-owned enum policy. Continue emitting the
current invalid zero data descriptor for flags and uint64-wide enums in this
slice.

- [ ] **Step 4: Emit the public descriptor getter only for supported records**

Add this declaration beside each generated CMeta getter in `c_structs.mustache`:

```c
TBE_GENERATED_API const TbeTypedDescriptor *<Record>_typed_descriptor(void);
```

Guard the declaration and definition with
`typed_cmeta_runtime_supported`. Emit one definition for each supported
composite, group and message. Remove Lua-only gating from descriptor access;
Lua output calls the same public getter and must not expose a second descriptor.

- [ ] **Step 5: Make generation order CMeta-first**

Move `*_TYPED_DESCRIPTOR` initialization after `*_CMETA_DATA` is defined. Delete generated calls to `tbe_typed_cmeta_graph_validate`; the descriptor boundary validates its CMeta root. Do not synthesize a descriptor when the generator cannot emit a supported CMeta scalar/Struct/Enum graph.

- [ ] **Step 6: Stop consuming typed structural fields for supported rows**

For a supported generated record, descriptor execution may consume only overlay
values named by Task 3. Rename the nested association annotation from
`typed_object_descriptor` to `typed_nested_overlay` for supported fields and
emit it only into `.nested_overlay`; `.object_type` is `NULL`. Existing
`kind`, native `offset`, root `size` and `object_type` members remain populated
only because the same `TbeTypedType` representation still serves unsupported
raw wrappers; Task 3's mutation tests prove the supported descriptor path does
not read them. Keep `typed_wire_kind`, `wire_offset`, `field_size_bytes`, byte
order, external names and flags authoritative in the overlay. Add generator
assertions that changing native field layout affects
`*_CMETA_LAYOUT_FIELDS`/`*_CMETA_FIELDS`, while a supported descriptor result
does not depend on the parallel typed native offset.

- [ ] **Step 7: Route every supported generated wrapper through the descriptor**

Split template expansion by `typed_cmeta_runtime_supported`. For the supported
branch, init, clear, all parse formats, JSON/YAML/CSV/XML serialization and both
binary serialization forms receive `&name##_TYPED_DESCRIPTOR`; the void
`name##_clear` wrapper calls `(void)tbe_typed_descriptor_clear(..., NULL)`.
For the unsupported branch, emit no descriptor and call the existing raw APIs
directly: `tbe_typed_init`, `tbe_typed_clear`, `tbe_typed_parse_ex`,
`tbe_typed_serialize_ex`, `tbe_typed_serialize_binary` and
`tbe_typed_serialize_binary_into` with `&name##_TYPED_TYPE`. No wrapper chooses
a path at runtime.

- [ ] **Step 8: Route supported Lua adapters through the same descriptor**

Add header-only Lua helpers with these exact signatures to `tools/lua/salts_lua.h`:

```c
static inline DataBindStatus c11_lua_push_tbe_typed_descriptor(
    lua_State *L, const TbeTypedDescriptor *descriptor,
    const void *object, size_t max_depth);
static inline DataBindStatus c11_lua_read_tbe_typed_descriptor(
    lua_State *L, int index, const TbeTypedDescriptor *descriptor,
    void *object, size_t max_depth, size_t max_dynamic_items);
```

These helpers preflight ABI-v2 `native_data`, traverse Struct fields from the
CMeta shape/offsets, read/write fixed-width scalar storage by the same complete
semantic/shape match as Task 3, and use only `cmeta_data_enum_read`/`assign` for
Enum. `read` builds semantic-zero temporary root storage of exactly the CMeta
root size, publishes only after the full Lua table succeeds, and leaves the
destination unchanged on failure. `max_depth` bounds Struct recursion;
`max_dynamic_items` remains unused for this no-container slice and is rejected
when the descriptor contains a deferred kind. The helpers read external Lua
keys from the paired overlay row but never its native kind/offset/size.

In `c_lua_bind.mustache`, supported records call these descriptor helpers and
unsupported records retain the current raw calls with no runtime branching.
Extend the existing “should generate typed C to Lua adapters” case in
`test_tbe_compiler.c` with a numeric-only supported schema: assert it contains
`*_typed_descriptor()` and both descriptor Lua helper names. Continue asserting
the unsupported `LoginMessage` output uses `*_typed_type()` and the raw helper
names.

- [ ] **Step 9: Verify C and C++ public consumers and generator output**

```bash
cmake --build build/linux-gcc-debug --target \
  test_tbe_compiler test_tbe_compiler_cmeta_fields test_tbe_typed_cmeta_public \
  test_tbe_typed_cmeta_public_cpp tbe_cmeta_graph_public_fixture -j2
ctest --test-dir build/linux-gcc-debug --no-tests=error \
  -R '^(test_tbe_compiler|test_tbe_compiler_cmeta_fields|test_tbe_typed_cmeta_public|test_tbe_typed_cmeta_public_cpp)$' \
  --output-on-failure
```

Expected: generated C and C++ consumers compile only against installed/public
headers and all four tests pass. Task 1's public generated-output RED turns
GREEN here.

- [ ] **Step 10: Commit**

```bash
git add tbe/tbe_compiler/compiler_core.c \
  tbe/tbe_compiler/templates/c_typed_source.mustache \
  tbe/tbe_compiler/templates/c_structs.mustache \
  tbe/tbe_compiler/templates/c_lua_bind.mustache \
  tbe/tbe_compiler/test_cmeta_field_projection.c \
  tbe/tbe_compiler/test_tbe_compiler.c tools/lua/salts_lua.h \
  tbe/data_bind/test_tbe_typed_cmeta_public.c \
  tbe/data_bind/test_tbe_typed_cmeta_public_cpp.cpp
git commit -m "refactor(tbe): emit CMeta-backed typed descriptors"
```

### Task 5: Lock the Cutover Boundary and Capability Matrix

**Files:**
- Modify: `tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md`
- Modify: `tbe/data_bind/README.md`
- Modify: `tbe/tbe_compiler/CLI_OPTIONS.md`
- Modify: `tbe/data_bind/tbe_typed_internal.h`
- Modify: `tbe/data_bind/tbe_typed.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_mapping.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_graph.c`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Modify: `tbe/schema/test/test_databind_only_dependency_contract.cmake`

**Interfaces:**
- Consumes: migrated generated descriptor paths from Tasks 2-4.
- Produces: checked-in supported/deferred matrix and a permanent no-reverse-authority contract.

- [ ] **Step 1: Document the exact capability matrix**

Record these rows without claiming deferred support:

| Native semantic storage | This slice | Structural source | Overlay source |
| --- | --- | --- | --- |
| Fixed-width integer, F32/F64 | supported | CMeta data/type descriptor and exact bit shape | external name, wire scalar kind/offset/width, validation |
| Non-flags Enum with int64-representable domain | supported | CMeta enum shape, declared storage type and enum operations | external name, wire integer kind/offset/width, validation |
| Nested non-optional Struct | supported | CMeta struct shape/layout/fields | external field names, wire layout and validation |
| Bool backed by generated `uint8_t` | deferred | canonical Bool-storage ABI or provider required | wire Bool policy remains overlay |
| Flags and uint64-wide Enum | deferred | unsigned/flags-capable CMeta enum contract required | wire integer policy remains overlay |
| Optional/presence | deferred | explicit native-presence contract required | presence/default policy remains overlay |
| STRING/BYTES/fixed buffer/UUID/custom | deferred | lifecycle/adapter contract required | wire policy remains overlay |
| Sequence/set/map | deferred to native-container contract/#46 coordination | CMeta range plus CSTL provider required | container wire policy remains overlay |

Update the existing-struct section in `CLI_OPTIONS.md`:
`TBE_TYPED_DEFINE_STRUCT*` creates raw typed metadata for deferred paths, not an
ABI-v2 descriptor. A descriptor-routed existing struct must provide an explicit
validated canonical CMeta graph and initialize
`TBE_TYPED_DESCRIPTOR_INIT(&overlay, &native_data)`; graphless descriptor use is
an error, not an upgrade path. State explicitly that #47 remains open while
these header-only/raw APIs and the unsupported generated families remain.

- [ ] **Step 2: Remove obsolete internal declarations after the last caller**

Delete `tbe_typed_kind_from_cmeta_data` and
`tbe_typed_cmeta_graph_validate` from `tbe_typed_internal.h` and their
implementations after `rg` confirms zero production/template callers. Rewrite
`test_tbe_typed_cmeta_mapping.c` to exercise the complete CMeta scalar matcher
through ABI-v2 descriptor behavior instead of calling the deleted reverse
mapping. Rewrite `test_tbe_typed_cmeta_graph.c` so `Sample`, Depth32/Depth33,
Bool and WideDomain expectations call generated CMeta getters or supported
typed descriptors without including generated `.c` or invoking the deleted
validator. Update `tbe/data_bind/CMakeLists.txt` source lists/registrations to
match; do not delete coverage merely to make the symbol scan pass. If a raw
deferred-kind caller remains, keep its private helper under a name that accepts
only `TbeTypedType`, never a migrated `TbeTypedDescriptor`; do not retain the
public reverse validator.

- [ ] **Step 3: Extend the dependency contract**

Construct forbidden reverse-authority identifiers by concatenation in the CMake policy test and reject active production/template references to `tbe_typed_cmeta_graph_validate`. Exclude the policy file itself as already done; do not exclude generated templates.

- [ ] **Step 4: Verify the source boundary**

```bash
rg -n 'tbe_typed_cmeta_graph_validate|typed_cmeta_validate_record' \
  tbe/data_bind tbe/tbe_compiler/templates tbe/tbe_compiler/compiler_core.c
rg -n 'descriptor->(type)|TBE_TYPED_DESCRIPTOR_INIT\([^,)]*\)' \
  tbe/data_bind tbe/tbe_compiler
rg -n 'tbe_typed_kind_from_cmeta_data' \
  tbe/data_bind tbe/tbe_compiler/templates tbe/tbe_compiler/compiler_core.c
```

Expected: no active reverse-authority validator, no ABI-v1 descriptor member access, and no one-argument descriptor initializer.

- [ ] **Step 5: Run the repository boundary test**

```bash
ctest --test-dir build/linux-gcc-debug/tbe/schema --no-tests=error \
  -R '^test_databind_only_dependency_contract$' --output-on-failure
```

Expected: pass without scanning top-level pinned `salts/` or `vcpkg/` dependency checkouts.

- [ ] **Step 6: Commit**

```bash
git add tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md \
  tbe/data_bind/README.md tbe/tbe_compiler/CLI_OPTIONS.md \
  tbe/data_bind/tbe_typed_internal.h \
  tbe/data_bind/tbe_typed.c \
  tbe/data_bind/test_tbe_typed_cmeta_mapping.c \
  tbe/data_bind/test_tbe_typed_cmeta_graph.c tbe/data_bind/CMakeLists.txt \
  tbe/schema/test/test_databind_only_dependency_contract.cmake
git commit -m "docs(databind): lock the canonical typed runtime boundary"
```

### Task 6: Verify One Immutable Exact Head

**Files:**
- Modify only when a failure is traced to a file owned by Tasks 1-5.

**Interfaces:**
- Consumes: the exact committed head after Task 5.
- Produces: focused CTest, sanitizer, installed-consumer and four-workflow evidence for the same SHA.

- [ ] **Step 1: Run focused native tests**

```bash
cmake --build build/linux-gcc-debug --target \
  test_tbe_typed test_tbe_typed_descriptor_boundary \
  test_tbe_typed_cmeta_public test_tbe_typed_cmeta_public_cpp \
  test_tbe_compiler_cmeta_fields test_data_bind_schema_reflection_contract -j2
ctest --test-dir build/linux-gcc-debug --no-tests=error \
  -R 'typed|cmeta|data_bind.*reflection|databind_only' \
  --output-on-failure
```

- [ ] **Step 2: Run full TBE/DataBind and installed consumers**

```bash
cmake --build build/linux-gcc-debug --target tbe/all install -j2
ctest --test-dir build/linux-gcc-debug/tbe --no-tests=error --output-on-failure
ctest --test-dir build/linux-gcc-debug --no-tests=error \
  -R 'data_bind|package_consumer|generated' --output-on-failure
```

- [ ] **Step 3: Require exact-head GitHub Actions**

Record one SHA and successful run IDs for all four workflows:

```text
DataBind direct parsers
TBE schema boundaries
TBE enum conformance
TBE descriptor safety
```

The descriptor workflow must include ASan/UBSan, full TBE/DataBind runtime, direct parser dependencies, installed native parser consumer and Jinja regressions. Earlier ancestor runs do not satisfy this gate.

- [ ] **Step 4: Reconcile #47 without closing it**

Add the exact SHA/run IDs and mark only the generated supported-record
scalar/Struct/non-flags-Enum checklist complete. Keep #47 open for Bool,
flags/wide Enum, header-only existing structs, unsupported generated/raw
records, optional, owned scalar, custom-adapter and native-container contracts.
Do not open a fallback or compatibility follow-up.

## Plan Self-Review

- Spec coverage: sole DataBind engine, CMeta structural authority, overlay separation, CSTL/parser boundaries, rollback and explicit failure are each assigned to a task.
- Placeholder scan: every code step names concrete symbols, files, failures and verification commands.
- Type consistency: every descriptor task uses ABI-v2 `overlay` plus
  `native_data`; every wrapper for a compiler-marked supported record consumes
  the same descriptor, while an unsupported record receives no descriptor.
- Scope check: Bool, flags/wide Enum, header-only existing structs, unsupported
  generated records, optional, owned scalar, custom and container work remain
  explicitly outside this first slice; #46, #47 remainder and #48 stay open.
