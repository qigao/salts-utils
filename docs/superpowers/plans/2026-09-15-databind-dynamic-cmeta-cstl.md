# DataBind Canonical Dynamic CMeta/CSTL Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace DataBind-private dynamic object/list/set/map storage with canonical CMeta semantic identity and CSTL-owned container storage while preserving DataBind as the sole binding/conversion engine and preserving current public dynamic behavior.

**Architecture:** Keep `DataBindValue` opaque and public. Internally, attach every value to an immutable retained dynamic-type graph expressed only in CMeta terms (`cmeta_data_kind`, `cmeta_type_identity`, canonical provider descriptors where available), while CSTL handles describe the physical bytes they actually store. Ordered `vec_t` storage preserves object/list/set/map traversal order; `hash_set_t` and `hash_map_t` provide set membership and map lookup indexes without becoming owning or ordering authorities. No native typed path may materialize a dynamic root.

**Tech Stack:** C11, Salts CMeta/CSTL pinned by the existing TBE workflows to `2804c2eb809fed4cb89f917f0a8cd00f482abb64`, TinyTest, CMake/CTest, ASan/UBSan, public C/C++ consumer gates.

**Spec:** `docs/superpowers/specs/2026-09-15-databind-dynamic-cmeta-cstl-design.md`

## Global Constraints

- DataBind remains SaltsUtils' sole public binding/conversion engine.
- No CBind delegation, second binder, forwarding facade, compatibility storage engine, fallback path, or feature flag.
- CMeta is the only semantic/structural identity authority; do not add a DataBind-private kind enum or generic/type universe.
- Schema overlay continues to own external names/aliases, optional/default policy, wire layout, validation, fingerprints, and compatibility policy.
- CSTL owns concrete object/sequence/set/map capacity, growth, lookup/membership storage, mutation generations, and container lifecycle.
- `DataBindValue` remains the public dynamic-value brand for #46; do not add `CObject` or `CDynamicValue`.
- `DataBindObject`/`DataBindRecord` may remain only as wrappers over the same owning `DataBindValue` model; no separate storage/conversion implementation.
- Native typed conversion from #47 must not create a `DataBindValue` tree unless a caller explicitly requests dynamic output.
- A CSTL handle's `element_type`/`key_type`/`value_type` must describe the bytes physically stored in that handle. The canonical semantic child identity lives in the retained dynamic-type graph; never lie to CSTL by binding `int32` metadata to a slot that physically stores `DataBindValue *`.
- Public ordering stays deterministic. Objects and sequences preserve schema/wire order. Sets preserve first semantic insertion order. Maps preserve insertion/wire order for indexed access and serialization.
- Every change is test-first. No slice keeps both old and new storage as runtime fallback.
- Base branch checkpoint for this plan is merge commit `0b3876018e5bdaed70fecbc0d0863cb394dd6044`; rebase/merge drift must be reviewed before implementation continues.

---

### Task 1: Add the canonical dynamic identity and owning-lifetime seam

**Files:**
- Modify: `tbe/data_bind/data_bind.h`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/data_bind_internal.h`
- Modify: `tbe/data_bind/data_bind_cmeta.h`
- Modify: `tbe/data_bind/data_bind_cmeta.c`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Create: `tbe/data_bind/test_data_bind_dynamic_cstl.c`

**Interfaces:**
- Consumes: `schema_cmeta_builtin_data()`, `schema_cmeta_field_resolve()`, `cmeta_type_identity_equal()`, existing parsed schema `Node` graph, existing `DataBindValueKind` public projection.
- Produces: immutable `db_dynamic_graph_t` / `db_dynamic_type_t` metadata, `data_bind_value_type_identity()`, internal node retain/release primitives, and physical boxed-slot CMeta traits used by later CSTL containers.

- [ ] **Step 1: Add the focused test target and write the RED identity/lifetime cases**

Add to `tbe/data_bind/CMakeLists.txt`:

```cmake
cmake_add_test(test_data_bind_dynamic_cstl
  SOURCES test_data_bind_dynamic_cstl.c
  LIBS Salts::DataBind Salts::DataBindCMeta Salts::TinyTest
  INCLUDES ${CMAKE_SOURCE_DIR}/tbe/schema/include
  FOLDER "tbe/data_bind/tests")
set_tests_properties(test_data_bind_dynamic_cstl PROPERTIES
  LABELS "databind;cmeta;cstl;dynamic")
```

Start `test_data_bind_dynamic_cstl.c` with production-path tests that create the same schema twice, parse the same root type twice, and require semantic identity equality without pointer equality assumptions:

```c
it("publishes stable CMeta identity across codecs") {
  DataBind *a = NULL, *b = NULL;
  DataBindValue *left = NULL, *right = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  const cmeta_type_identity *left_id;
  const cmeta_type_identity *right_id;

  check_equal(data_bind_create_from_text(schema_text, strlen(schema_text), &a, &error),
              DATA_BIND_OK);
  check_equal(data_bind_create_from_text(schema_text, strlen(schema_text), &b, &error),
              DATA_BIND_OK);
  check_equal(data_bind_parse_json(a, "Envelope", json_text, strlen(json_text), &left, &error),
              DATA_BIND_OK);
  check_equal(data_bind_parse_json(b, "Envelope", json_text, strlen(json_text), &right, &error),
              DATA_BIND_OK);

  left_id = data_bind_value_type_identity(left);
  right_id = data_bind_value_type_identity(right);
  check_not_null(left_id);
  check_not_null(right_id);
  check(cmeta_type_identity_equal(left_id, right_id));

  data_bind_value_free(right);
  data_bind_value_free(left);
  data_bind_free(b);
  data_bind_free(a);
}
```

Add a codec-independent lifetime case:

```c
it("keeps dynamic identity alive after codec destruction") {
  DataBind *codec = NULL;
  DataBindValue *root = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  const cmeta_type_identity *identity;

  check_equal(data_bind_create_from_text(schema_text, strlen(schema_text), &codec, &error),
              DATA_BIND_OK);
  check_equal(data_bind_parse_json(codec, "Envelope", json_text, strlen(json_text), &root, &error),
              DATA_BIND_OK);
  identity = data_bind_value_type_identity(root);
  check_not_null(identity);

  data_bind_free(codec);
  check_not_null(data_bind_value_type_identity(root));
  check(cmeta_type_identity_equal(identity, data_bind_value_type_identity(root)));
  data_bind_value_free(root);
}
```

Add a canonical scalar child check: the `int32` child identity must compare equal to the canonical fixed-width provider identity selected by `schema_cmeta_builtin_data("int32")`.

- [ ] **Step 2: Run the focused test and verify the intended RED**

Run:

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target test_data_bind_dynamic_cstl -j2
ctest --test-dir build -R '^test_data_bind_dynamic_cstl$' --output-on-failure
```

Expected: compile failure because `data_bind_value_type_identity()` does not exist yet. No production behavior should fail before the missing API is reached.

- [ ] **Step 3: Add the public identity query without exposing private storage**

Append to `data_bind.h` near the existing `data_bind_value_kind()` accessors:

```c
/**
 * Return the canonical CMeta semantic identity associated with this dynamic
 * value. The identity is borrowed from the owning root's immutable dynamic
 * type graph and remains valid until that root is released.
 */
DATA_BIND_API const cmeta_type_identity *
data_bind_value_type_identity(const DataBindValue *value);
```

Do not expose `db_dynamic_graph_t`, CSTL handles, physical slot descriptors, or schema `Node *` pointers publicly.

- [ ] **Step 4: Introduce one CMeta-only internal dynamic type graph**

In `data_bind.c`, define the private metadata with no parallel kind system:

```c
typedef struct db_dynamic_type db_dynamic_type_t;
typedef struct db_dynamic_graph db_dynamic_graph_t;

typedef struct db_dynamic_field_type {
  char *stable_name;
  const db_dynamic_type_t *value_type;
} db_dynamic_field_type_t;

struct db_dynamic_type {
  cmeta_data_kind kind;
  const cmeta_data_desc *canonical_data;
  cmeta_type_identity owned_identity;
  const cmeta_type_identity *identity;
  char *owned_stable_id;
  db_dynamic_field_type_t *fields;
  size_t field_count;
  const db_dynamic_type_t *element_type;
  const db_dynamic_type_t *key_type;
  const db_dynamic_type_t *value_type;
};

struct db_dynamic_graph {
  size_t references;
  db_dynamic_type_t *nodes;
  size_t node_count;
  char **owned_strings;
  size_t owned_string_count;
};
```

Rules implemented by `db_dynamic_graph_build()`:

```c
static DataBindStatus db_dynamic_graph_build(
    const Node *schema_root,
    const uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE],
    const char *root_type,
    db_dynamic_graph_t **out_graph,
    const db_dynamic_type_t **out_root_type,
    DataBindError *error);
static db_dynamic_graph_t *db_dynamic_graph_retain(db_dynamic_graph_t *graph);
static void db_dynamic_graph_release(db_dynamic_graph_t *graph);
```

The builder must:

1. Reuse provider-owned identities for schema scalars with canonical providers.
2. Use `schema_cmeta_field_resolve()` for every field kind; never infer a DataBind-private kind.
3. Represent storage-unselected schema semantics with CMeta identities owned by this graph, using deterministic stable IDs derived from the schema fingerprint plus canonical type expression, not descriptor addresses.
4. Reuse the same graph node for recursive references and reject cycles/depth according to existing schema limits rather than recursively allocating forever.
5. Never copy pointers into codec-owned scratch/parser documents into the published graph.

- [ ] **Step 5: Attach type metadata to dynamic nodes and add root-only graph ownership**

Extend the private `DataBindValue` layout with metadata, without changing its public opacity:

```c
struct DataBindValue {
  size_t references;
  const db_dynamic_type_t *type;
  db_dynamic_graph_t *owned_graph; /* non-NULL only for published owning roots */
  DataBindValueKind kind;          /* public/API projection only */
  union {
    /* existing scalar and temporary legacy container members during Task 1 */
  } data;
};
```

Introduce:

```c
static DataBindValue *dbv_retain(DataBindValue *value);
static void dbv_release(DataBindValue *value);
static void dbv_destroy_node(DataBindValue *value);
```

`dbv_release()` destroys a node only when its internal owner count reaches zero. `data_bind_value_free()` remains the public owning-root release: it destroys the complete value tree first and releases `owned_graph` last. Borrowed accessors never retain.

- [ ] **Step 6: Define physical boxed-slot CMeta traits for later CSTL use**

Add private storage-only descriptors. These describe actual bytes (`DataBindValue *` and owned field/map wrappers), not schema semantic types:

```c
typedef struct db_owned_value_slot {
  DataBindValue *value;
} db_owned_value_slot_t;

static bool db_owned_value_slot_copy(void *dst, const void *src) {
  db_owned_value_slot_t *out = dst;
  const db_owned_value_slot_t *in = src;
  out->value = dbv_retain(in->value);
  return in->value == NULL || out->value != NULL;
}

static void db_owned_value_slot_move(void *dst, void *src) {
  db_owned_value_slot_t *out = dst;
  db_owned_value_slot_t *in = src;
  out->value = in->value;
  in->value = NULL;
}

static void db_owned_value_slot_destroy(void *slot) {
  db_owned_value_slot_t *owned = slot;
  dbv_release(owned->value);
  owned->value = NULL;
}
```

The matching `cmeta_type_desc` must declare `COPY | MOVE | DESTROY` and exact `sizeof(db_owned_value_slot_t)` / alignment. Do not mark it as the child's semantic identity.

- [ ] **Step 7: Make parse entry points build and retain the graph transactionally**

At the existing dynamic parse boundary, build the graph before binding the root. On successful parse, attach one retained graph to the published root. On failure, release the unpublished tree then release the graph. Update `data_bind_value_clone()` so a successful clone retains the immutable graph but deep-clones every value node/storage object.

- [ ] **Step 8: Run focused identity/lifetime tests plus existing public API tests**

Run:

```bash
cmake --build build --target test_data_bind_dynamic_cstl test_data_bind_public_api test_data_bind_cmeta -j2
ctest --test-dir build -R '^(test_data_bind_dynamic_cstl|test_data_bind_public_api|test_data_bind_cmeta)$' --output-on-failure
```

Expected: PASS. Also run `test_data_bind_pool` because `DataBindValue` construction/release changed.

- [ ] **Step 9: Commit the seam**

```bash
git add tbe/data_bind/data_bind.h \
        tbe/data_bind/data_bind.c \
        tbe/data_bind/data_bind_internal.h \
        tbe/data_bind/data_bind_cmeta.h \
        tbe/data_bind/data_bind_cmeta.c \
        tbe/data_bind/CMakeLists.txt \
        tbe/data_bind/test_data_bind_dynamic_cstl.c
git commit -m "refactor(databind): add canonical dynamic identity seam"
```

At this checkpoint, legacy container arrays still exist temporarily, but there is no fallback decision: Tasks 2-4 replace each storage family and delete its old implementation in the same gate.

---

### Task 2: Cut object and sequence storage over to CSTL Vec

**Files:**
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/data_bind_cmeta.c`
- Modify: `tbe/data_bind/test_data_bind_dynamic_cstl.c`
- Modify: `tbe/data_bind/test_data_bind.c`
- Modify: `tbe/data_bind/test_data_bind_public_api.c`

**Interfaces:**
- Consumes: `db_owned_value_slot_t`, `dbv_retain()`, `dbv_release()`, the Task 1 dynamic graph, `vec_t`, `stl_vec_container_desc`.
- Produces: `db_object_storage_t` and `db_sequence_storage_t`, with all public object/list accessors routed through CSTL Vec.

- [ ] **Step 1: Add RED tests for ordered object/list storage and hard limits**

Add tests that parse an object with three fields and a list with three values and assert:

```c
check_equal(data_bind_value_field_count(root), 3u);
check_string(data_bind_value_field_name(root, 0u), "first");
check_string(data_bind_value_field_name(root, 1u), "second");
check_string(data_bind_value_field_name(root, 2u), "third");

const DataBindValue *list = data_bind_value_get(root, "values");
check_equal(data_bind_value_count(list), 3u);
check_equal(data_bind_value_as_int(data_bind_value_at(list, 0u)), 10);
check_equal(data_bind_value_as_int(data_bind_value_at(list, 1u)), 20);
check_equal(data_bind_value_as_int(data_bind_value_at(list, 2u)), 30);
```

Add a clone test verifying the clone's ordered fields/list values survive freeing the source first.

Add a limit-path case that reaches CSTL `STL_CAPACITY_EXCEEDED` through an existing DataBind item/result limit and must surface as `DATA_BIND_ERR_LIMIT` with no published root.

- [ ] **Step 2: Run the focused tests and record the RED reason**

The behavior tests may already pass because legacy arrays preserve order. Therefore add a white-box storage assertion through a private helper declared only in `data_bind_internal.h` and consumed only by the in-tree test:

```c
typedef enum db_internal_storage_kind {
  DB_INTERNAL_STORAGE_SCALAR = 0,
  DB_INTERNAL_STORAGE_VEC,
  DB_INTERNAL_STORAGE_ORDERED_SET,
  DB_INTERNAL_STORAGE_ORDERED_MAP
} db_internal_storage_kind_t;

db_internal_storage_kind_t
data_bind_internal_storage_kind(const DataBindValue *value);
```

This symbol is not installed or declared in `data_bind.h`. RED requires OBJECT and LIST to return `DB_INTERNAL_STORAGE_VEC`; legacy arrays return `SCALAR`/unsupported until migrated.

Run:

```bash
cmake --build build --target test_data_bind_dynamic_cstl -j2
ctest --test-dir build -R '^test_data_bind_dynamic_cstl$' --output-on-failure
```

Expected: focused failure only on the new internal storage-kind assertions.

- [ ] **Step 3: Replace the private array structs with CSTL storage records**

Replace `data_bind_value_field_array_t` / `data_bind_value_array_t` inside the private union with:

```c
typedef struct db_field_slot {
  char *name;
  DataBindValue *value;
} db_field_slot_t;

typedef struct db_object_storage {
  vec_t fields;
} db_object_storage_t;

typedef struct db_sequence_storage {
  vec_t values;
} db_sequence_storage_t;
```

Define `db_field_slot` CMeta traits:

- copy: duplicate `name`, retain `value`;
- move: transfer both pointers and zero source;
- destroy: free `name`, release `value`.

Initialize physical handles explicitly:

```c
static stl_status db_vec_init(vec_t *vec,
                              const cmeta_type_desc *slot_type,
                              size_t limit) {
  memset(vec, 0, sizeof(*vec));
  vec->cmeta.descriptor = &stl_vec_container_desc;
  vec->element_type = slot_type;
  return vec_init(vec, limit);
}
```

Do not bind the Vec to the semantic child descriptor when the physical bytes are `db_owned_value_slot_t` or `db_field_slot_t`.

- [ ] **Step 4: Route object/list builders through `vec_push()` and delete reserve/growth code**

Replace `dbv_object_reserve()`, `dbv_array_reserve()`, `dbv_array_push()`, and direct `realloc()` growth with:

```c
static DataBindStatus dbv_sequence_push(DataBindValue *sequence,
                                        DataBindValue *child) {
  db_owned_value_slot_t slot = {child};
  stl_status status = vec_push(&sequence->data.sequence.values, &slot);
  return db_status_from_stl(status);
}
```

and an equivalent `dbv_object_set()` that pushes `db_field_slot_t` into the field Vec. After the final caller is migrated, delete the old `count/capacity/items` structs and their reserve helpers in this same commit.

- [ ] **Step 5: Route public accessors, clone, serialization, and recursive walkers through Vec**

Update at least:

```text
data_bind_value_field_count
data_bind_value_field_name
data_bind_value_field_at
data_bind_value_get
data_bind_value_count
data_bind_value_at
dbv_clone_tree
apply_mapped_names
data_bind_value_to_json
data_bind_value_to_xml
binary collection/object writers and readers
```

Use only `vec_size()` and `vec_at_const()` for object/list indexed access. Do not cache a second count/capacity.

- [ ] **Step 6: Give object/list CMeta ranges real CSTL generation versions**

In `data_bind_cmeta.c`, set:

```c
range.version = db_value_range_generation(owner);
range.current_version = db_value_range_generation;
```

where the DataBind target exposes a non-installed internal accessor returning `vec_generation()` for object/list storage. `cmeta_range_next()` must then return `CMETA_GEN_MUTATED` if a future internal mutation occurs after Range creation.

- [ ] **Step 7: Run focused and regression tests**

```bash
cmake --build build --target \
  test_data_bind_dynamic_cstl test_data_bind test_data_bind_public_api \
  test_data_bind_record test_data_bind_cmeta test_data_bind_cflow -j2
ctest --test-dir build -R '^(test_data_bind_dynamic_cstl|test_data_bind|test_data_bind_public_api|test_data_bind_record|test_data_bind_cmeta|test_data_bind_cflow)$' --output-on-failure
```

Expected: PASS; AddressSanitizer later must show no leaked retained child nodes after Vec destruction.

- [ ] **Step 8: Commit the object/list cutover**

```bash
git add tbe/data_bind/data_bind.c \
        tbe/data_bind/data_bind_cmeta.c \
        tbe/data_bind/test_data_bind_dynamic_cstl.c \
        tbe/data_bind/test_data_bind.c \
        tbe/data_bind/test_data_bind_public_api.c
git commit -m "refactor(databind): move dynamic object list storage to CSTL Vec"
```

---

### Task 3: Implement deterministic semantic sets with ordered Vec plus HashSet

**Files:**
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/data_bind_cmeta.c`
- Modify: `tbe/data_bind/test_data_bind_dynamic_cstl.c`
- Modify: `tbe/data_bind/test_data_bind.c`

**Interfaces:**
- Consumes: Task 1 semantic type metadata and boxed-slot traits; Task 2 Vec helper; Core `hash_set_t` and CMeta `EQUAL/HASH` traits.
- Produces: `db_set_storage_t`, semantic uniqueness, stable first-insertion order, `CMETA_RANGE_UNIQUE | CMETA_RANGE_ORDERED` traversal.

- [ ] **Step 1: Add RED set semantics tests**

Use a schema with a `set<int32>` and input containing semantic duplicates. Require first-insertion ordering and uniqueness:

```c
const DataBindValue *set = data_bind_value_get(root, "ids");
check(data_bind_value_kind(set) == DATA_BIND_VALUE_SET);
check_equal(data_bind_value_count(set), 3u);
check_equal(data_bind_value_as_int(data_bind_value_at(set, 0u)), 3);
check_equal(data_bind_value_as_int(data_bind_value_at(set, 1u)), 1);
check_equal(data_bind_value_as_int(data_bind_value_at(set, 2u)), 2);
check(data_bind_internal_storage_kind(set) == DB_INTERNAL_STORAGE_ORDERED_SET);
```

Add clone independence and an unsupported-element-traits case that must return `DATA_BIND_ERR_SCHEMA`, not silently fall back to list semantics.

- [ ] **Step 2: Run the RED**

```bash
cmake --build build --target test_data_bind_dynamic_cstl -j2
ctest --test-dir build -R '^test_data_bind_dynamic_cstl$' --output-on-failure
```

Expected: legacy sequence-backed Set either preserves duplicates or reports the wrong storage kind.

- [ ] **Step 3: Add the non-owning semantic key reference descriptor**

Define a physical HashSet key wrapper:

```c
typedef struct db_value_ref_key {
  const DataBindValue *value;
} db_value_ref_key_t;
```

Its CMeta traits are `TRIVIAL_COPY | TRIVIAL_DESTROY | EQUAL | HASH`. Equality/hash callbacks must:

1. Reject NULL/mismatched semantic identities.
2. Compare identity through `cmeta_type_identity_equal()`.
3. Dispatch scalar/custom comparison/hash through canonical CMeta traits/providers.
4. Recursively compare/hash object/sequence/set/map through canonical DataBind/CMeta traversal with configured recursion limits.
5. Never use pointer identity as semantic equality.

The HashSet key descriptor describes only the `db_value_ref_key_t` bytes; the semantic child type remains `set_value->type->element_type`.

- [ ] **Step 4: Introduce ordered set storage and transactional insert**

Replace the Set use of sequence storage with:

```c
typedef struct db_set_storage {
  vec_t ordered_values;
  hash_set_t membership;
  uint64_t generation;
} db_set_storage_t;
```

Initialize both handles with canonical CSTL descriptors and physical slot/key descriptors. Insert transaction:

```text
1. Build/validate child value.
2. Probe HashSet with non-owning db_value_ref_key_t.
3. If already present: release the newly built duplicate; succeed without append.
4. If absent: append owning slot to ordered Vec.
5. Insert a non-owning reference to the Vec-owned heap node into HashSet.
6. If HashSet insertion fails, erase the just-appended Vec slot and return the mapped error.
7. Advance one logical set generation only after both components commit.
```

Because Vec owns `DataBindValue *` nodes on the heap, Vec reallocation does not invalidate HashSet references to those nodes.

- [ ] **Step 5: Route Set public access, clone, serialization, and Range through ordered Vec**

`data_bind_value_count()`, `data_bind_value_at()`, JSON/XML/binary serialization, clone, and `data_bind_cmeta_range_init(DATA_BIND_CMETA_RANGE_VALUES)` must use `ordered_values`, never HashSet slots.

For Set ranges add:

```c
range.flags |= CMETA_RANGE_UNIQUE | CMETA_RANGE_ORDERED;
range.version = db_value_range_generation(owner);
range.current_version = db_value_range_generation;
```

- [ ] **Step 6: Delete the legacy sequence-backed Set path**

Remove every branch that treats `DATA_BIND_VALUE_SET` as an alias of `array_val`/sequence storage. There must be no conditional fallback from HashSet capability errors to Vec-only uniqueness.

- [ ] **Step 7: Run focused Set and CMeta tests**

```bash
cmake --build build --target test_data_bind_dynamic_cstl test_data_bind_cmeta test_data_bind -j2
ctest --test-dir build -R '^(test_data_bind_dynamic_cstl|test_data_bind_cmeta|test_data_bind)$' --output-on-failure
```

Expected: semantic duplicates collapse deterministically, traversal order is first-insertion order, clone is independent, unsupported hash/equality fails explicitly.

- [ ] **Step 8: Commit the Set cutover**

```bash
git add tbe/data_bind/data_bind.c \
        tbe/data_bind/data_bind_cmeta.c \
        tbe/data_bind/test_data_bind_dynamic_cstl.c \
        tbe/data_bind/test_data_bind.c
git commit -m "refactor(databind): back dynamic sets with CSTL membership"
```

---

### Task 4: Implement deterministic maps with ordered Vec plus HashMap index

**Files:**
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/data_bind_record.c`
- Modify: `tbe/data_bind/data_bind_cmeta.c`
- Modify: `tbe/data_bind/test_data_bind_dynamic_cstl.c`
- Modify: `tbe/data_bind/test_data_bind_record.c`
- Modify: `tbe/data_bind/test_data_bind_public_api.c`

**Interfaces:**
- Consumes: `db_value_ref_key_t` semantic hash/equality from Task 3, ordered Vec helpers, Core `hash_map_t`.
- Produces: `db_map_storage_t`, canonical key/value ownership, deterministic indexed order, transactional key->entry index maintenance.

- [ ] **Step 1: Add RED deterministic map tests**

Require indexed order and lookup to agree after parse and clone:

```c
const DataBindValue *map = data_bind_value_get(root, "attrs");
DataBindMapEntry e0 = data_bind_value_map_entry_at(map, 0u);
DataBindMapEntry e1 = data_bind_value_map_entry_at(map, 1u);
check_string(e0.key, "z");
check_string(e1.key, "a");
check(data_bind_internal_storage_kind(map) == DB_INTERNAL_STORAGE_ORDERED_MAP);
```

Add a same-key replacement/duplicate-wire case using the existing documented map semantics and assert that indexed order remains deterministic. Add clone/free-source-first coverage.

- [ ] **Step 2: Run the focused RED**

Expected: legacy private `data_bind_value_map_array_t` fails the storage-kind assertion.

- [ ] **Step 3: Define owning map-entry physical storage and index value type**

Use:

```c
typedef struct db_map_entry_slot {
  DataBindValue *key_value;
  char *public_key_text;
  DataBindValue *value;
} db_map_entry_slot_t;

typedef struct db_map_index_value {
  size_t ordered_index;
} db_map_index_value_t;

typedef struct db_map_storage {
  vec_t ordered_entries;
  hash_map_t index;
  uint64_t generation;
} db_map_storage_t;
```

`db_map_entry_slot` copy/move/destroy traits retain/release both dynamic key/value nodes and duplicate/free `public_key_text`. `db_map_index_value_t` is trivial physical storage.

Do not make `public_key_text` the semantic key authority. It exists only because the current public `DataBindMapEntry` API projects a `const char *` key for supported schemas.

- [ ] **Step 4: Build canonical key values before indexing**

For each map key accepted by current schema/binder behavior, construct the canonical dynamic key node using the map key's `db_dynamic_type_t`. String keys continue to expose their text through `public_key_text`; any key family not supported by current schema semantics must keep returning the existing explicit schema/type error rather than being approximated by string hashing.

The HashMap key is `db_value_ref_key_t`; HashMap value is `db_map_index_value_t`.

- [ ] **Step 5: Make insert/replace/erase atomic across Vec and HashMap**

Insertion algorithm:

```text
1. Build key and value nodes completely.
2. Probe HashMap by semantic key.
3. For a new key, append one owning entry to ordered Vec, then insert ref->index.
4. If index insertion fails, erase the appended Vec entry and return the mapped error.
5. For replacement semantics, build replacement value first; update the owning Vec entry only after success; index/order remain unchanged.
6. Any erase removes HashMap index first only after the target Vec entry is known; after Vec compaction, repair affected indices before publishing the mutation.
7. Advance one logical map generation only after the full operation commits.
```

No HashMap bucket/slot order is observable.

- [ ] **Step 6: Route all map consumers through the new storage**

Update:

```text
data_bind_value_count
data_bind_value_map_entry_at
record map views/accessors
JSON/XML/CSV/binary serialization
clone/deep-free
mapped-name recursion
CMeta map-entry Range
```

Use ordered Vec for indexed traversal; use HashMap only for lookup/membership.

- [ ] **Step 7: Delete `data_bind_value_map_array_t` and private map reserve/growth helpers**

Remove the old map array and every `realloc()`/manual capacity path after its last caller is gone. Do not leave it behind as a fallback for unsupported keys.

- [ ] **Step 8: Run map, record, public API, and CMeta regressions**

```bash
cmake --build build --target \
  test_data_bind_dynamic_cstl test_data_bind_record test_data_bind_public_api \
  test_data_bind_cmeta test_data_bind -j2
ctest --test-dir build -R '^(test_data_bind_dynamic_cstl|test_data_bind_record|test_data_bind_public_api|test_data_bind_cmeta|test_data_bind)$' --output-on-failure
```

- [ ] **Step 9: Commit the Map cutover**

```bash
git add tbe/data_bind/data_bind.c \
        tbe/data_bind/data_bind_record.c \
        tbe/data_bind/data_bind_cmeta.c \
        tbe/data_bind/test_data_bind_dynamic_cstl.c \
        tbe/data_bind/test_data_bind_record.c \
        tbe/data_bind/test_data_bind_public_api.c
git commit -m "refactor(databind): back dynamic maps with CSTL index storage"
```

---

### Task 5: Close CMeta traversal, facade, generation, and native-isolation gaps

**Files:**
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/data_bind.h`
- Modify: `tbe/data_bind/data_bind_record.c`
- Modify: `tbe/data_bind/data_bind_cmeta.c`
- Modify: `tbe/data_bind/data_bind_cmeta.h`
- Modify: `tbe/data_bind/test_data_bind_dynamic_cstl.c`
- Modify: `tbe/data_bind/test_data_bind_cmeta.c`
- Modify: `tbe/data_bind/test_data_bind_record.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_graph.c`
- Modify: `tbe/data_bind/test_tbe_typed_cmeta_public.c`

**Interfaces:**
- Consumes: all CSTL-backed dynamic storage from Tasks 2-4 and #47 native typed runtime.
- Produces: fully versioned CMeta ranges over canonical storage, one dynamic owning model behind Object/Record wrappers, and an executable proof that native typed paths allocate no dynamic nodes.

- [ ] **Step 1: Add RED Range version/invalidation integration tests**

For object/list/set/map values, require each successful `data_bind_cmeta_range_init()` to publish:

```c
check_not_null(range.current_version);
check(range.version != 0u);
check_equal(range.version, range.current_version(range.object));
```

Add a private in-tree mutation probe in `data_bind_internal.h` used only by `test_data_bind_dynamic_cstl.c`:

```c
DataBindStatus data_bind_internal_test_touch_generation(DataBindValue *value);
```

Its implementation must mutate only the logical generation counter for an already published container; it must not alter semantic content. Test:

```c
cmeta_range_cursor cursor = {0};
DataBindValueRef ref = {0};
check_equal(data_bind_internal_test_touch_generation(container), DATA_BIND_OK);
check_equal(cmeta_range_next(&range, &cursor, &ref), CMETA_GEN_MUTATED);
```

The helper stays non-installed and is removed later if a real mutation API provides the same test seam.

- [ ] **Step 2: Make all DataBind CMeta ranges use logical container generation**

For simple Vec-backed object/list values, return `vec_generation()`. For composite Set/Map storage, return the DataBind logical generation that advances exactly once per committed mutation of the ordered store and membership/index state.

`data_bind_cmeta_range_init()` flags:

```text
OBJECT fields: SIZED | ORDERED | REUSABLE
LIST values:   SIZED | ORDERED | REUSABLE
SET values:    SIZED | ORDERED | UNIQUE | REUSABLE
MAP entries:   SIZED | ORDERED | REUSABLE
```

Keep the existing borrowed `DataBindValueRef` / `DataBindFieldRef` / `DataBindMapEntryRef` public range element types unless a direct CSTL range can preserve identical public element semantics. #48, not #46, owns target/module removal decisions.

- [ ] **Step 3: Prove `DataBindObject` and `DataBindRecord` are only owning facades**

Keep their implementation as one envelope:

```c
struct DataBindObject {
  char *type_name;
  DataBindValue *value;
  uint8_t schema_fingerprint[DATA_BIND_SCHEMA_FINGERPRINT_SIZE];
};
```

All object/record constructors, getters, serializers, and free paths must delegate to the same CSTL-backed `DataBindValue`. Remove any duplicate deep-copy/container mutation logic encountered here. Do not rename or remove the public types in #46.

- [ ] **Step 4: Add the native dynamic-allocation isolation RED**

Use the existing public pool statistics as an observable counter. Disable reuse at a quiescent test boundary, capture allocation count, perform a supported #47 native typed parse/serialize, and require the dynamic allocation count not to increase:

```c
size_t before_alloc = 0u, before_reused = 0u;
size_t after_alloc = 0u, after_reused = 0u;

data_bind_set_value_pool_enabled(0);
data_bind_get_value_pool_stats(&before_alloc, &before_reused);

check_equal(tbe_typed_parse_json_descriptor(codec, descriptor, json, len,
                                            &native_value, &error), DATA_BIND_OK);

data_bind_get_value_pool_stats(&after_alloc, &after_reused);
check_equal(after_alloc, before_alloc);
check_equal(after_reused, before_reused);
```

Use the exact supported descriptor fixture already exercised by `test_tbe_typed_cmeta_graph.c`; do not route this proof through a raw/unsupported generated wrapper.

- [ ] **Step 5: Fix any remaining native dynamic-root detours**

If the RED shows a dynamic allocation, trace the first `dbv_new`/`DataBindValue` construction on the descriptor path and remove only that detour. Native descriptor parse/serialize must consume the canonical native CMeta graph and overlay directly, as #47 established.

- [ ] **Step 6: Remove redundant legacy CMeta adaptation logic made unreachable by the storage cutover**

Delete only helpers whose last callers disappeared in Tasks 2-4. Keep `Salts::DataBindCMeta` and its installed header for this issue unless every public symbol has a replacement and a separate migration/removal gate explicitly approves deletion. No compatibility alias is added.

- [ ] **Step 7: Run focused dynamic/native integration tests**

```bash
cmake --build build --target \
  test_data_bind_dynamic_cstl test_data_bind_cmeta test_data_bind_record \
  test_tbe_typed_cmeta_graph test_tbe_typed_cmeta_public -j2
ctest --test-dir build -R '^(test_data_bind_dynamic_cstl|test_data_bind_cmeta|test_data_bind_record|test_tbe_typed_cmeta_graph|test_tbe_typed_cmeta_public)$' --output-on-failure
```

- [ ] **Step 8: Commit the runtime closure**

```bash
git add tbe/data_bind
git commit -m "refactor(databind): close dynamic CMeta traversal ownership"
```

Review the staged diff before committing; exclude unrelated TBE/compiler/schema edits.

---

### Task 6: Publish migration evidence and run #46 closure gates

**Files:**
- Modify: `tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md`
- Modify: `tbe/data_bind/README.md`
- Modify: `docs/superpowers/specs/2026-09-15-databind-dynamic-cmeta-cstl-design.md` only if implementation revealed a factual mismatch; do not rewrite approved boundaries.
- Modify: `.github/workflows/...` only if an existing changed-path gate provably omits the new test; do not weaken any gate.

**Interfaces:**
- Consumes: completed CSTL-backed dynamic runtime.
- Produces: exact capability/migration documentation and the evidence needed to close #46 and unblock #48.

- [ ] **Step 1: Update the capability matrix from "deferred to #46" to implemented facts**

For sequence/set/map rows, record separately:

```text
semantic authority: CMeta cmeta_data_kind / retained dynamic identity graph
physical object/list storage: CSTL Vec of DataBind-owned owning slots
physical set storage: ordered Vec + HashSet non-owning membership index
physical map storage: ordered Vec + HashMap non-owning key->index
public order: object/schema order; sequence insertion order; set first-insertion order; map insertion/wire order
fallback: none
native typed container support: unchanged unless separately proven by a provider slice
```

Do not claim #46 automatically enables generated/native list/set/map descriptors; dynamic storage and native storage are separate capability rows.

- [ ] **Step 2: Document lifetime and invalidation rules in `README.md`**

State explicitly:

1. An owning dynamic root may outlive its `DataBind *codec`.
2. Accessor-returned child pointers and strings are borrowed from the owning root.
3. Releasing the root invalidates all borrowed descendants/views.
4. A Range/view captures container generation and returns `CMETA_GEN_MUTATED` after structural mutation.
5. Set/map iteration order is deterministic and never HashSet/HashMap bucket order.
6. `data_bind_value_clone()` produces independent owning storage while semantic type metadata may be retained/shared immutably.

- [ ] **Step 3: Prove no private container engine remains**

Run source searches and require zero production matches for the removed private growable container types/helpers:

```bash
git grep -nE 'data_bind_value_(array|field_array|map_array)_t|dbv_(array|object|map)_reserve' -- tbe/data_bind ':!tbe/data_bind/test_*'
```

Expected: no matches.

Search for prohibited binder/fallback additions:

```bash
git grep -nE 'CBind|fallback|compat.*container|legacy.*container' -- tbe/data_bind/data_bind.c tbe/data_bind/data_bind_internal.h
```

Expected: no new production route matching those concepts; comments that explicitly say "no fallback" are acceptable after manual review.

- [ ] **Step 4: Run the complete DataBind/TBE test set in Debug**

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass. Record the exact test count from this head; do not reuse earlier #56 counts.

- [ ] **Step 5: Run sanitizer configuration**

Use the repository's existing sanitizer preset/workflow configuration. At minimum execute the same ASan+UBSan DataBind/TBE tests exercised by the TBE descriptor/schema-boundary CI. Expected: zero sanitizer findings, especially no leaks/double-free/use-after-free in boxed slot copy/move/destroy, Set rollback, Map rollback, clone, and codec-before-root destruction.

- [ ] **Step 6: Run public and installed consumer gates**

Require green evidence for:

```text
public C header consumer
public C++17 header consumer
generated static consumer
generated shared consumer
installed Salts::DataBind consumer
DataBindCMeta/CFlow consumers affected by Range storage
```

No test may include `data_bind_internal.h` except the in-tree white-box dynamic test.

- [ ] **Step 7: Run changed-path benchmark/conformance correctness gates**

Run correctness/smoke portions for `benchmark_data_bind_pool` and the existing dynamic JSON/path workloads. Performance numbers are informational here; semantic correctness is required before #9 performs broader benchmarking.

- [ ] **Step 8: Commit documentation and final cleanup**

```bash
git add tbe/data_bind/CMETA_SCHEMA_CAPABILITIES.md \
        tbe/data_bind/README.md \
        tbe/data_bind/CMakeLists.txt \
        tbe/data_bind/test_data_bind_dynamic_cstl.c
git commit -m "docs(databind): publish dynamic CMeta CSTL migration evidence"
```

Include any production/test files changed by the final cleanup only after reviewing the exact staged diff.

- [ ] **Step 9: Push exact head, update Draft PR/#46, and wait for fresh exact-head CI**

The PR body must state the exact head SHA and list fresh runs for the changed-path gates. Do not call #46 GREEN while any required run is queued/in-progress/red.

On failure, inspect the first real failing job/log and fix only that root cause. Do not restore private arrays, CBind delegation, compatibility providers, or fallback behavior to make CI green.

- [ ] **Step 10: Close #46 only after fresh final-head evidence is GREEN**

Before closure verify all #46 acceptance criteria against the final tree:

```text
canonical CMeta identity: yes
CSTL object/list/set/map storage: yes
stable borrowed traversal + generation: yes
codec-independent owning root: yes
native path dynamic allocation: no
private DataBind container engine: absent
fallback/CBind/second binder: absent
full CTest + sanitizers + public/install consumers: green
```

Only then mark #46 completed. #48 may start from that merged head and perform the module split by ownership boundary.
