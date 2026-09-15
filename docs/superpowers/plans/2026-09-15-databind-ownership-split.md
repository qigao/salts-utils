# DataBind Ownership-Boundary Module Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split DataBind's sole binding engine into private modules whose files match schema, dynamic-value, format, query, streaming, and orchestration ownership without changing public behavior.

**Architecture:** Keep `data_bind` as one shared-library target and move implementation in reviewable slices. Extract schema fingerprinting before #46 because it is already independent; defer the dynamic-value and remaining module cuts until #46 establishes CSTL as the final container owner.

**Tech Stack:** C11, CMake Presets, Salts CMeta/CSTL/parser/CSerde/QueryVM APIs, TinyTest, ASan/UBSan.

**Spec:** `docs/superpowers/specs/2026-09-15-databind-ownership-split-design.md`

## Global Constraints

- DataBind remains the sole public binding engine; no CBind delegation or second dynamic-value brand.
- Preserve public API/ABI, schema/wire formats, errors, limits, ordering, rollback, and cancellation.
- Add no compatibility facade, fallback, feature flag, or duplicate structural/container state.
- Internal headers and new implementation files are not installed.
- Use installed Salts master APIs and the repository's versioned presets/vcpkg manifest.
- Run the smallest relevant test first; exact-head CI is required before a GREEN claim.

---

### Task 1: Extract deterministic schema fingerprint ownership

**Files:**
- Create: `tbe/data_bind/data_bind_schema_internal.h`
- Create: `tbe/data_bind/data_bind_schema.c`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Test: `tbe/data_bind/test_data_bind_public_api.c`

**Interfaces:**
- Consumes: immutable `Node`, Monocypher BLAKE2b.
- Produces: `int data_bind_schema_fingerprint(const Node *root, uint8_t out[DATA_BIND_SCHEMA_FINGERPRINT_SIZE]);`

- [ ] **Step 1: Confirm the characterization test**

Use the existing `test_data_bind_public_api` case "should bind objects to an
equivalent parsed schema and reject same-name mismatches". It requires
equivalent parsed trees to serialize and mismatched overlays to return
`DATA_BIND_ERR_SCHEMA` with no output.

- [ ] **Step 2: Record the baseline**

Run `cmake --preset win-release-user` and
`cmake --build --preset win-release-user --target test_data_bind_public_api` in
`VsDevCmd.bat`. If the known re2c Unicode installation defect stops configure,
record that exact failure and use the already-green merged exact-head Linux
workflow as the baseline; do not add `-D` overrides.

- [ ] **Step 3: Define the internal fingerprint interface**

Create the non-installed header with:

```c
#define DATA_BIND_SCHEMA_FINGERPRINT_SIZE 32U

int data_bind_schema_fingerprint(
    const Node *root,
    uint8_t out[DATA_BIND_SCHEMA_FINGERPRINT_SIZE]);
```

The header includes `node_tree.h` and `<stdint.h>` and has C++ linkage guards.

- [ ] **Step 4: Move the implementation without semantic edits**

Move the fingerprint domain, maximum depth, integer/text/node hashing helpers,
and `data_bind_schema_fingerprint()` to `data_bind_schema.c`. Keep the domain
string `TurboUtils.DataBind.Schema.v1`, digest size 32, maximum depth 64, byte
encoding, traversal order, and NULL encoding unchanged.

- [ ] **Step 5: Link the new private source**

Add `data_bind_schema.c` and `data_bind_schema_internal.h` to the existing
`data_bind` target. Include the internal header from `data_bind.c`; do not add a
target, install rule, alias, or dependency.

- [ ] **Step 6: Verify and commit**

Run `git diff --check`, compile the two changed production objects through the
`win-release-user` preset when configure is available, then run
`test_data_bind_public_api`. Push an exact head and require parser-boundary,
descriptor-safety, and enum-conformance to complete successfully.

Commit:

```text
refactor(databind): isolate schema fingerprint ownership
```

---

### Task 2: Complete the #46 prerequisite

**Files:**
- Consume: `docs/superpowers/plans/2026-09-15-databind-dynamic-cmeta-cstl.md`
- Verify: `tbe/data_bind/data_bind.c`
- Verify: `tbe/data_bind/data_bind_cmeta.c`
- Verify: `tbe/data_bind/data_bind_internal.h`

**Interfaces:**
- Consumes: #46 canonical identity seam.
- Produces: CSTL-backed object/list/set/map storage and versioned CMeta ranges with no private growable container engine.

- [ ] **Step 1: Execute #46 Tasks 2-6 on its own branch**

Use the existing #46 design and plan. Do not mix container storage migration
into the #48 module branch.

- [ ] **Step 2: Verify the prerequisite factually**

Require zero production matches for:

```text
data_bind_value_array_t
data_bind_value_field_array_t
data_bind_value_map_array_t
dbv_array_reserve
dbv_object_reserve
dbv_map_reserve
```

Require object/list/set/map ranges to expose a nonzero generation and mutation
detection, then require fresh full exact-head CI before merging #46.

- [ ] **Step 3: Rebase the #48 branch on the merged #46 head**

Resolve only factual source-move conflicts. Re-run Task 1 verification before
starting Task 3.

---

### Task 3: Extract final dynamic-value storage

**Files:**
- Create: `tbe/data_bind/data_bind_value_internal.h`
- Create: `tbe/data_bind/data_bind_value.c`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/data_bind_internal.h`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Test: `tbe/data_bind/test_data_bind_dynamic_cstl.c`
- Test: `tbe/data_bind/test_data_bind_public_api.c`
- Test: `tbe/data_bind/test_data_bind_pool.c`

**Interfaces:**
- Consumes: final #46 CSTL storage records and semantic dynamic graph.
- Produces: constructors/builders, retain/release, clone, accessors, range generation, and pool controls for `DataBindValue`.

- [ ] **Step 1: Add a link-boundary test target**

Add a test source that includes only public `data_bind.h`, constructs values by
parsing JSON, clones the root, frees the source first, and checks every scalar,
object, list, set, and map accessor on the clone. The test must not include the
new internal header.

- [ ] **Step 2: Move the owner as one unit**

Move `DataBindValue`, its dynamic graph, pool, CSTL physical storage traits,
constructors, clone/release, container builders, public accessors, and pool
statistics. Keep `DataBindObject` in orchestration. Cross-unit builders return
`DataBindStatus` and publish an output only after CSTL operations commit.

- [ ] **Step 3: Remove duplicate private declarations**

Keep the `DataBindValue` definition in exactly one private header. Delete all
old array/reserve declarations; do not preserve them behind conditionals.

- [ ] **Step 4: Verify lifecycle and rollback**

Run dynamic-CSTL, public API, pool, CMeta range, record facade, and ASan tests.
Require clone independence, balanced child release, deterministic set/map
order, limit status preservation, and `CMETA_GEN_MUTATED` after structural
mutation.

- [ ] **Step 5: Commit**

```text
refactor(databind): isolate dynamic value ownership
```

---

### Task 4: Extract schema overlay reflection and validation

**Files:**
- Modify: `tbe/data_bind/data_bind_schema_internal.h`
- Modify: `tbe/data_bind/data_bind_schema.c`
- Modify: `tbe/data_bind/data_bind.c`
- Test: `tbe/data_bind/test_data_bind_schema_reflection_contract.c`
- Test: `tbe/data_bind/test_data_bind_cmeta_reflection.c`

**Interfaces:**
- Consumes: immutable codec schema root and canonical `schema_cmeta` queries.
- Produces: binding-name validation and all `data_bind_schema_*` reflection functions.

- [ ] **Step 1: Extend characterization coverage**

Require reflection of messages, composites, groups, unions, enums, flags,
attributes, aliases, optional/default policy, CMeta kinds, unresolved storage,
size-prefixed outputs, and stale-error clearing.

- [ ] **Step 2: Move overlay helpers and public reflection functions**

Move name collision checks, record/field lookup helpers used only by
reflection, `fill_schema_type`, `fill_schema_field`, size-prefix handling, and
all `data_bind_schema_*` functions. Keep format binding helpers in their format
units even if they inspect overlay fields.

- [ ] **Step 3: Preserve codec encapsulation**

Expose one internal borrowed accessor for the immutable schema root rather than
copying it or publishing `struct DataBind` publicly:

```c
const Node *data_bind_internal_schema_root(const DataBind *codec);
```

- [ ] **Step 4: Verify and commit**

Run schema reflection, CMeta reflection/acceptance, typed graph, and public C/C++
tests. Commit:

```text
refactor(databind): isolate schema overlay reflection
```

---

### Task 5: Extract direct format adapters

**Files:**
- Create: `tbe/data_bind/data_bind_json.c`
- Create: `tbe/data_bind/data_bind_yaml.c`
- Create: `tbe/data_bind/data_bind_xml.c`
- Create: `tbe/data_bind/data_bind_csv.c`
- Create: `tbe/data_bind/data_bind_binary.c`
- Create: `tbe/data_bind/data_bind_format_internal.h`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Test: `tbe/data_bind/test_data_bind.c`
- Test: `tbe/data_bind/test_data_bind_public_api.c`

**Interfaces:**
- Consumes: borrowed schema overlay, transactional value builders, installed parser/CSerde APIs.
- Produces: one internal parse and serialize entry per supported format.

- [ ] **Step 1: Freeze cross-format behavior**

Add or retain tests for JSON, YAML, XML, CSV, and binary semantic equivalence;
exact 64-bit integers; UUID/temporal/decimal/bigint/money/bytes; nested
containers; aliases/output names; malformed input; unsupported representation;
short output buffers; and cleanup after failure.

- [ ] **Step 2: Move JSON and YAML together only at their shared adapter seam**

JSON owns JSON token binding/serialization. YAML owns CYaml parsing and its
document-to-JSON adapter call, then invokes the same internal JSON value binder.
Neither unit reparses schema text.

- [ ] **Step 3: Move XML and CSV independently**

XML retains XML path/name rules and lossless representation checks. CSV retains
header/path/cell rules and ordered row emission. Both return current public
statuses without switching formats.

- [ ] **Step 4: Move binary separately**

Keep wire layout, endian, optional/union restrictions, short-buffer reporting,
and transactional output in `data_bind_binary.c`. Do not infer native offsets.

- [ ] **Step 5: Verify after every format move**

Run the direct parser workflow's focused targets and the full DataBind public
API tests after each source move. Commit JSON/YAML, XML/CSV, and binary as three
reviewable commits.

---

### Task 6: Extract QueryVM selection and diagnostics

**Files:**
- Create: `tbe/data_bind/data_bind_query_internal.h`
- Create: `tbe/data_bind/data_bind_query.c`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Test: query/path cases in `tbe/data_bind/test_data_bind_public_api.c`

**Interfaces:**
- Consumes: parser-owned QueryVM and borrowed dynamic roots.
- Produces: limit translation, owned public diagnostic copies, and transactional selected results.

- [ ] **Step 1: Characterize limits and diagnostics**

Require invalid syntax, instruction/operand/regex/step limits, no-match, first
match, all matches, stale diagnostic clearing, and unchanged outputs on error.

- [ ] **Step 2: Move QueryVM ownership**

Move native limit translation, diagnostic copying, query failure mapping,
program lifecycle, and selection. Do not move streaming parser state.

- [ ] **Step 3: Verify and commit**

Run JSON/YAML query tests, direct parser gates, and public API tests. Commit:

```text
refactor(databind): isolate query selection ownership
```

---

### Task 7: Extract streaming lifecycle

**Files:**
- Create: `tbe/data_bind/data_bind_stream_internal.h`
- Create: `tbe/data_bind/data_bind_stream.c`
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Test: streaming cases in `tbe/data_bind/test_data_bind_public_api.c`

**Interfaces:**
- Consumes: format binders and query selection.
- Produces: all `data_bind_stream_*` APIs and the sole `data_bind_stream_t` state definition.

- [ ] **Step 1: Freeze the state-machine contract**

Require bounded input/result/frame/capture storage, partial chunks, callback
stop/error, cancellation before/after feed, finish exactly once, destroy from
each nonterminal state, and no published partial result on failure.

- [ ] **Step 2: Move the stream state and helpers together**

Move the struct, JSON/YAML/XML SAX state, CSV incremental state, feed-file loop,
publication, cancellation, finish, and destroy paths. Keep allocations bounded
by the existing limits and preserve callback-borrowed lifetimes.

- [ ] **Step 3: Verify terminal cleanup**

Run stream/query/public API tests under ASan/UBSan. Every successful create must
reach finish, cancel, or destroy without leaked parser/value state.

- [ ] **Step 4: Commit**

```text
refactor(databind): isolate streaming lifecycle
```

---

### Task 8: Thin orchestration and publish migration evidence

**Files:**
- Modify: `tbe/data_bind/data_bind.c`
- Modify: `tbe/data_bind/data_bind_internal.h`
- Modify: `tbe/data_bind/CMakeLists.txt`
- Modify: `tbe/data_bind/README.md`
- Modify: `ARCHITECTURE.md`

**Interfaces:**
- Consumes: all extracted private modules.
- Produces: a thin public orchestration unit and documented dependency/ownership graph.

- [ ] **Step 1: Delete zero-caller helpers**

Use CodeGraph plus `rg.exe` and direct reading. Delete only helpers whose last
caller disappeared. Do not remove DataBindCMeta/DataBindCFlow public targets
unless every installed symbol has a documented replacement and migration test.

- [ ] **Step 2: Audit dependencies**

Assign every private dependency to the implementation unit that uses it. Keep
public target dependencies only where installed headers expose their types.
Verify exported `Salts::DataBind`, `Salts::DataBindCMeta`, and
`Salts::DataBindCFlow` consumers.

- [ ] **Step 3: Update architecture and migration documentation**

Document each module's state owner, borrowed views, failure/rollback boundary,
parser/CSerde dependency, and verification target. Record deliberate removals;
do not retain stale CBind or parser-compat text.

- [ ] **Step 4: Run final gates**

Run Release CTest, ASan/UBSan, C/C++ public headers, generated static/shared
consumers, installed consumers, benchmark correctness, and the exact-head
parser-boundary/descriptor-safety/enum-conformance workflows.

- [ ] **Step 5: Close #48 only from a verified exact head**

Update the PR and issue with the final head SHA, run IDs, module ownership map,
public removals, and residual platform risks. Close #48 only when every
acceptance criterion is backed by fresh evidence.

Commit:

```text
docs(databind): publish ownership-boundary split
```
