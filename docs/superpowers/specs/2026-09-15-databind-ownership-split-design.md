# DataBind Ownership-Boundary Module Split Design

## Status and scope

This design implements salts-utils issue #48 from `main` at merge commit
`f62ac0f0a13c5c9bb927cf42ae8d9e9b2d140adc`. It supersedes the stale Task 8
wording in `2026-09-12-databind-convergence.md`, which still refers to CBind and
does not reflect the merged #47 native runtime or completed #46 checkpoint.

The split is behavior-preserving. DataBind remains the sole public binding
engine. This work does not add a facade, compatibility route, fallback engine,
or public ABI. Public declarations remain in `data_bind.h`, `tbe_typed.h`,
`data_bind_cmeta.h`, and `data_bind_cflow.h`.

## Current facts

- `tbe/data_bind/data_bind.c` contains schema hashing and reflection, dynamic
  value storage, JSON/YAML/XML/CSV/binary binding, object orchestration, query
  execution, and streaming state.
- #47 is merged and native typed conversion consumes canonical CMeta graphs.
- #46 is merged: dynamic roots retain recursive CMeta semantic identity graphs;
  object/list/set/map use CSTL-backed storage and CMeta ranges expose generation
  invalidation.
- `data_bind_schema_fingerprint()` has one production caller,
  `data_bind_create_from_root()`. Its output is consumed by dynamic root
  identity and `DataBindObject` schema compatibility checks.
- `test_data_bind_public_api` already distinguishes equivalent parsed schemas
  from same-name mismatches, exercising the fingerprint contract.

## Ownership boundaries

### Schema overlay

`data_bind_schema.c` owns deterministic schema fingerprints and, after the
dynamic storage gate, public schema reflection and binding-name validation. It
may depend on the immutable schema `Node` tree, `schema_cmeta`, and hashing. It
must not construct `DataBindValue` nodes or parse payload formats.

### Dynamic values

`data_bind_value.c` will own `DataBindValue`, its pool, scalar/custom payloads,
CSTL-backed object/list/set/map storage, clone/release, and borrowed accessors.
This split is deferred until #46 removes the private growable arrays so the
module boundary reflects the final storage authority once.

### Format adapters

`data_bind_json.c`, `data_bind_yaml.c`, `data_bind_xml.c`, `data_bind_csv.c`,
and `data_bind_binary.c` will own format mechanics and schema-directed
conversion for their format. They consume internal schema/value interfaces and
installed parser/CSerde APIs. They do not own codec lifetime, public error
policy, query selection, or streaming publication.

### Query and streaming

`data_bind_query.c` owns QueryVM limit translation, diagnostics, and selection.
`data_bind_stream.c` owns the stream state machine, cancellation, incremental
parser state, limits, publication, and cleanup. A successful stream creation
must reach exactly one terminal state: finished, canceled, or destroyed.

### Public orchestration

`data_bind.c` remains the public entry-point unit. It owns codec/object
orchestration, dispatch to internal schema/value/format/query/stream modules,
and public error translation. Native typed conversion remains in
`tbe_typed.c`; record and CMeta/CFlow facades remain in their existing units
unless a later removal gate proves a public replacement.

## Internal interface rules

- Internal headers are not installed and expose only functions needed across
  translation units.
- Opaque public types stay opaque. Private structure definitions are placed in
  one owning internal header only when more than one implementation unit must
  access their fields.
- Internal functions return `DataBindStatus` when they can distinguish public
  errors. Boolean helpers are limited to pure predicates or deterministic
  transformations with no richer failure state.
- All outputs are transactional: failure leaves caller-owned outputs unchanged
  or in their documented zero state.
- Borrowed `Node`, `DataBindValue`, string, range, and parser views never outlive
  their owner and are not retained across mutation or stream callbacks.
- No module maintains a second schema graph, count/capacity mirror, container
  index, or error state.

## Migration sequence

1. Extract deterministic schema fingerprinting. This seam is independent of
   #46 storage and has a single caller.
2. Merge #46 CSTL storage/range work and rebase this branch on that exact main
   head before subsequent source moves.
3. Extract the final dynamic-value owner without preserving private arrays.
4. Extract schema reflection and validation over the immutable overlay.
5. Extract one format at a time, starting with JSON/YAML and then XML/CSV and
   binary, running cross-format tests after every move.
6. Extract QueryVM translation, then the stream lifecycle.
7. Thin public orchestration, delete helpers with zero callers, and review
   DataBindCMeta/DataBindCFlow targets only against explicit public migration
   evidence.

## Compatibility and rollback

There is no public API, ABI, wire, schema, error, limit, or ordering change.
Each slice is a source move plus the smallest internal declaration needed to
link it. If a slice fails verification, revert that slice's commit without
restoring obsolete container or parser compatibility paths.

Public target names and installed headers remain unchanged. New `.c` files are
private sources of the existing `data_bind` shared library.

## Verification

Each slice must run its closest target first and then the adjacent DataBind/TBE
tests. The final branch requires:

- full configured CTest;
- ASan/UBSan descriptor-safety coverage;
- DataBind direct-parser and enum-conformance exact-head workflows;
- public C and C++ header consumers;
- generated static/shared consumers;
- installed consumer and export/dependency checks;
- source verification showing no generated or committed file was modified by
  tests.

Windows currently has two environment/platform limitations that are not caused
by this split: the configured `C:/tools/cpp-dev` re2c 4.6 installation lacks
Unicode stdlib files, and the installed Salts DLL descriptors trigger MSVC
`C2099` in `schema_cmeta.c`. These must remain explicit failures; no source or
preset fallback is permitted. Exact-head Linux CI is the authoritative full
runtime gate until those independent defects are repaired.
