# DataBind Canonical Dynamic CMeta/CSTL Runtime Design

## Status and scope

This design implements issue #46 after #47 was completed by PR #56. #47 made
DataBind's native typed runtime consume canonical CMeta graphs while preserving
DataBind as SaltsUtils' sole binding and conversion engine. #46 applies the same
ownership discipline to runtime-schema dynamic values.

At design approval time, the dynamic implementation was not yet canonical. The
implemented #46 runtime now stores object/list/set/map state in CSTL handles and
publishes each binary or text direct/all/path/stream dynamic root with one
immutable retained recursive CMeta identity graph. Every reachable object,
sequence, set, map, key, child, Enum/custom or supported scalar points at its
matching semantic node; synthetic all/path/stream list roots have their own
sequence node. Clone copies value/container storage, retains the graph once at
the cloned root, and remains valid after codec or source release.

`DataBindValueKind` has deliberately not disappeared: it remains the public and
physical-storage union discriminator required for compatibility and destruction.
It is checked against the graph node at publication boundaries, but it is not a
parallel semantic type graph. Only `cmeta_range` created by
`data_bind_cmeta_range_init()` captures generation and can return
`CMETA_GEN_MUTATED`; ordinary borrowed Record/List/Map views expose lifecycle
invalidation, not generation-status reporting.

#46 closes only when owning dynamic values use canonical CMeta semantic identity
and traversal, and concrete collection storage/lifecycle comes from CSTL rather
than DataBind-private arrays or maps.

This is a storage-authority migration, not a public binder replacement and not
the #48 module split. The following are hard constraints:

- DataBind remains the sole public binding/conversion engine.
- No CBind delegation, second binder, forwarding facade, fallback path, feature
  flag, or legacy-container engine may be introduced.
- CMeta owns semantic/structural identity; the schema overlay owns wire/schema
  policy; CSTL owns concrete dynamic container storage and lifecycle.
- Native typed conversion must not materialize a dynamic root unless the caller
  explicitly requests dynamic output.
- `DataBindValue` remains the public dynamic-value brand for #46. Do not add
  `CObject`, `CDynamicValue`, or a second generic object API.
- `DataBindObject` and `DataBindRecord` are not given new independent semantics.
  Public removal, if still desirable, is an explicit migration gate after the
  unified value model covers their behavior.

The older master tracker text in #8 predates the later sole-DataBind-engine
boundary. For #46, the accepted #46/#47 ownership model and the merged #47
implementation are authoritative: CMeta and CSTL provide structure/storage
protocols; DataBind owns conversion, rollback, orchestration, limits, and public
errors.

## Ownership boundary

### CMeta

CMeta owns the dynamic value's canonical semantic type information:

- scalar, Struct, Enum/custom, sequence, set, and map semantic kinds;
- immutable type identities and recursive dynamic type graphs;
- element/key/value type descriptors;
- equality/hash/copy/move/destroy traits required by concrete CSTL providers;
- Range-compatible traversal shape;
- semantic comparison of type identity through `cmeta_type_equal`, never raw
  descriptor-address equality.

CMeta does not own wire names, aliases, defaults, validation, format rules, or
schema fingerprints.

### CSTL

CSTL owns concrete runtime collection storage and lifecycle:

- vector growth and destruction;
- set hashing/equality and uniqueness;
- map lookup/index storage;
- element/key/value lifecycle through bound CMeta descriptors;
- capacity/resource limits exposed by the chosen container handle;
- mutation generations used to invalidate borrowed traversals/views.

DataBind must not retain a parallel `count/capacity/items` implementation after
the corresponding CSTL-backed path is complete.

### Schema overlay

The schema overlay remains independent from the immutable CMeta graph and owns:

- external field names and aliases;
- optional/required state and defaults;
- validation policy;
- binary offsets, widths, byte order, and format annotations;
- collection wire semantics;
- schema fingerprints and compatibility policy.

Applying a schema to a dynamic value adds policy. It does not create a second
structural type graph or rewrite a CMeta descriptor.

### DataBind

DataBind continues to own:

- runtime-schema parse and serialize orchestration;
- dynamic/native conversion;
- transactional construction and publication;
- public limits and diagnostic translation;
- owning-root lifetime and borrowed-child API semantics;
- schema association/fingerprint handling;
- format/query/stream selection.

## Canonical dynamic value model

`DataBindValue` remains opaque publicly. Internally, a value is a typed node over
canonical CMeta metadata plus storage appropriate to its semantic kind. The
public `DataBindValueKind` enumeration remains an API projection for existing
callers; it is not the structural source of truth and must not select a private
container implementation.

Conceptually, an owning root contains:

```text
DataBindValue root
  -> retained dynamic-type graph
  -> value storage
       scalar/custom inline or provider-owned state
       Struct/object field-entry Vec
       sequence Vec
       set ordered-value Vec + HashSet membership index
       map ordered-entry Vec + HashMap index
  -> optional schema association / fingerprint
```

The exact private C layout may differ, but these authority boundaries may not.

### Dynamic type graph lifetime

Dynamic descriptors cannot borrow codec-owned schema memory if a returned owning
value may outlive its `DataBind *codec`. Therefore each owning root retains an
immutable dynamic-type graph whose lifetime is independent of the codec.

The graph is reference-counted at the owning-root level:

- codec/schema compilation may hold one reference while parsing;
- every published owning root retains one reference;
- `data_bind_value_clone()` retains or independently clones the immutable graph
  as appropriate, but never borrows from the source root without a retained
  lifetime;
- child values and borrowed views do not independently retain the graph;
- releasing the final owning root releases the graph only after every contained
  value/container has been destroyed.

This makes a published dynamic value safe to inspect and destroy after
`data_bind_free(codec)`.

The graph must not contain pointers back into mutable parser documents, `Node`
trees, stream handles, or codec-owned scratch memory.

## Scalar and custom values

Existing scalar/custom API behavior remains, but structural identity comes from
the bound CMeta descriptor rather than `DataBindValueKind` alone.

Trivial scalar values use canonical descriptors directly. Managed values such as
strings/bytes/custom domains use provider-defined CMeta lifecycle where such a
provider exists. #46 does not invent missing domain providers that belong to #5;
an unsupported canonical provider remains an explicit schema/type error.

`NULL` remains a value/presence state, not an invented standalone native CMeta
storage type.

## Struct/object storage

Dynamic Struct/object values preserve schema field order because public indexed
field access and deterministic serialization rely on it. Their concrete storage
is therefore a CSTL `vec_t` of owned field entries, not a hash map and not a
DataBind-private growable array.

Each field entry contains the external/schema field name required by the dynamic
API and an owning child value. Structural field identity/type comes from the
canonical dynamic CMeta graph. The schema name is overlay data and must not be
written back into the CMeta structural descriptor.

A lookup accelerator may be added only if profiling justifies it and it uses a
CSTL associative container. It must remain an index over the ordered field Vec,
not a second authoritative field store.

## Sequence storage

Dynamic sequences use a self-describing CSTL `vec_t` bound to the canonical CMeta
element descriptor. The Vec is the sole owner of collection capacity/growth and
its element lifecycle contract.

DataBind may provide private helper functions that translate `stl_status` into
`DataBindStatus`, but it must not reimplement reserve/growth semantics around a
second `items/count/capacity` array.

Public indexed access remains deterministic and preserves insertion/wire order.

## Set storage and deterministic order

Dynamic sets use exactly two CSTL components:

- an ordered `vec_t` that owns dynamic values in first-insertion/wire order;
- a `hash_set_t` membership index whose keys are non-owning references to the
  Vec-owned values and whose CMeta hash/equality traits delegate to the
  canonical element descriptor.

The Vec is the sole owner of element values. The HashSet owns only membership
index state and must never own a second deep copy of each dynamic value. Insert,
clone, erase, and destruction helpers update both components transactionally.

This deliberately changes the legacy implementation detail that a set is merely
a list tagged as `DATA_BIND_VALUE_SET`, while preserving deterministic indexed
access and serialization order. The first semantic occurrence wins; later
semantic duplicates are not appended.

If an element type lacks the CMeta equality/hash traits required by `hash_set_t`,
the schema/value combination is unsupported and fails explicitly. The runtime
must not silently fall back to sequence semantics, pointer identity, a different
set provider, or a private DataBind membership table.

`data_bind_value_at()` and CMeta Range traversal over a set use the ordered Vec,
never HashSet slot order. This keeps observable traversal stable while
`hash_set_t` supplies canonical uniqueness/membership behavior.

## Map storage and deterministic order

Dynamic maps need both canonical lookup and stable indexed/serialization order.
A raw HashMap alone does not provide the latter, so map storage uses two CSTL
components with one authority split:

- an ordered `vec_t` of owning map entries in insertion/wire order;
- a `hash_map_t` index from key to ordered-entry index.

The ordered Vec owns key/value entries. The HashMap owns only lookup index state
and never a second copy of the owning dynamic value. Insert, replacement, erase,
clone, and destruction helpers update both components transactionally.

This preserves existing `data_bind_value_map_entry_at(index)` semantics and
format determinism while removing the private DataBind map engine.

Map keys use canonical CMeta equality/hash for their declared dynamic key type.
No string-only implementation detail may become the new structural contract.
Where existing public APIs expose string keys, that is an overlay/API projection
on the supported schema subset rather than a license to hard-code every map as a
private string map.

## Schema association

An owning dynamic root may be schema-bound or, where supported, structurally
valid without an attached schema overlay. Schema association never owns the
canonical dynamic CMeta graph.

For a schema-bound root, retain only the overlay data needed after codec
lifetime, such as copied/retained external naming policy and fingerprint. Do not
retain a raw pointer to the codec's parsed schema tree.

Applying overlay rules validates that the overlay and canonical graph are
compatible. Failure does not mutate either authority.

## Construction, publication, and rollback

Every parse/conversion path follows the same transactional rule established by
#47:

1. Resolve/retain the canonical dynamic type graph.
2. Construct a semantic-zero unpublished root and bind its CSTL handles to the
   required CMeta descriptors.
3. Parse/convert fields into temporary storage.
4. Let CMeta traits and CSTL providers own element/container cleanup as the tree
   grows.
5. Validate schema overlay constraints and configured limits.
6. Publish the root only after the complete operation succeeds.
7. On any failure, destroy temporary CSTL/provider state and release the retained
   graph; return no partial root.

For APIs that replace an existing owning destination, the existing destination
remains unchanged until the replacement root is complete.

Cleanup must preserve the primary error. Lifecycle defects remain visible under
ASan/UBSan and focused ownership tests.

## Clone semantics

`data_bind_value_clone()` remains a deep owning clone:

- the result has independent mutable/storage lifetime;
- managed scalar values are copied through canonical CMeta copy semantics;
- Struct/sequence/map containers are rebuilt through CSTL operations;
- set ordered storage and membership index are rebuilt together, preserving
  first-insertion order and semantic uniqueness;
- no child pointer, parser document, codec scratch pointer, or CSTL storage block
  is shared between source and clone;
- the immutable dynamic type graph may be shared only through a retained owning
  reference.

A clone failure returns `DATA_BIND_ERR_OOM` or the relevant lifecycle/status
translation, leaves the source unchanged, and publishes no partial clone.

## Borrowed children, ranges, and invalidation

The public dynamic API remains primarily immutable: accessors return borrowed
children/views owned by the root. Those pointers remain valid until the owning
root is released unless the containing storage is explicitly mutated through an
API that documents invalidation.

CSTL handles already expose mutation generation counters. #46 standardizes their
use for CMeta ranges created by `data_bind_cmeta_range_init()`:

- such a borrowed range/cursor captures the containing storage generation;
- structural mutation increments the provider generation;
- a later traversal with a stale generation fails rather than reading moved or
  erased storage;
- releasing the root invalidates every borrowed child/range/view;
- scalar reads that do not depend on a container remain unaffected by unrelated
  container mutations.

Ordinary Record/List/Map borrowed views do not carry a captured generation and
cannot return `CMETA_GEN_MUTATED`; they follow the owner lifetime and accessor
invalidation contract instead.

For composite set/map storage, any mutation that can move ordered Vec storage or
change membership/index state advances one logical container generation. Public
borrowed views do not expose separate Vec and HashSet/HashMap generations.

The current `data_bind_cmeta_range_init()` compatibility surface may remain while
public callers need it, but its traversal must ultimately be backed by the
canonical CSTL/CMeta storage. It must not reconstruct authority from
`DataBindValueKind` plus legacy arrays.

## Public API and migration policy

#46 does not add compatibility aliases or a second branded value API.

The existing public `DataBindValue` accessors continue to project the canonical
runtime model. `DataBindValueKind` remains stable where possible as a compatibility
classification, not a storage selector.

`DataBindObject` and `DataBindRecord` may remain temporarily only as thin owning
facades over the same `DataBindValue` model. They may not keep a separate object
representation, container engine, or conversion path. Any deliberate public
removal requires:

- proof that the unified value/root model covers its semantics;
- a migration note;
- public C and C++ compile gates;
- install/export consumer evidence;
- an explicit ABI-version decision.

No alias/fallback layer is added to prolong obsolete private storage.

## Native-path isolation

#47's native typed path remains structurally independent from dynamic-root
storage. A native parse/serialize operation walks the canonical native CMeta
graph and schema overlay directly.

#46 must add a regression that demonstrates a supported native typed operation
does not allocate or publish a `DataBindValue` dynamic tree. Shared scalar,
validation, parser, or overlay helpers are allowed; shared dynamic storage is
not required for native conversion.

## Limits and status translation

CSTL provider limits and DataBind's public depth/item/byte limits are enforced
before publication. Provider statuses are translated without collapsing public
semantics:

- allocation failure -> `DATA_BIND_ERR_OOM`;
- configured capacity/depth/item/byte exhaustion -> `DATA_BIND_ERR_LIMIT`;
- invalid graph/provider/schema composition -> `DATA_BIND_ERR_SCHEMA`;
- input semantic mismatch -> `DATA_BIND_ERR_TYPE_MISMATCH`;
- invalid public call contract -> `DATA_BIND_ERR_INVALID_ARG`.

A provider capability failure is never retried through legacy private storage.

## Migration slices

Implementation proceeds as small exact-head gates rather than one monolithic
rewrite.

### Slice A: canonical dynamic node/type/lifecycle seam

Add the private owning-root/type-graph seam, canonical CMeta descriptors/traits
for dynamic nodes/entries, and codec-independent lifetime tests. Do not migrate
all containers in the same commit.

### Slice B: Struct and sequence Vec cutover

Move object field storage and list storage to CSTL Vec. Remove the corresponding
private reserve/push/free arrays when their final caller disappears. Route CMeta
Range traversal directly over the new storage.

### Slice C: deterministic set cutover

Move set storage to ordered-value CSTL Vec plus CSTL HashSet membership index.
Add semantic equality/hash traits, first-insertion ordering, duplicate,
clone/invalidation/failure tests, and remove the legacy sequence-backed set path
in the same gated cutover.

### Slice D: deterministic map storage

Move maps to ordered-entry Vec plus CSTL HashMap index. Preserve indexed access,
clone, serialization order, rollback, and key/value ownership. Remove the private
map array/index logic in the same gated cutover.

### Slice E: facade/removal and dynamic/native isolation closure

Make `DataBindObject`/`DataBindRecord` consume the unified owning-root model,
remove redundant CMeta adapters/helpers after their final caller disappears,
publish migration notes for any deliberate public removal, and prove native
paths do not materialize dynamic roots.

Each slice begins with a focused behavioral RED on the production path. No slice
may keep both old and new storage as runtime fallback.

## Verification

Focused tests must cover at least:

- canonical CMeta identity for every supported dynamic semantic kind;
- codec destruction before dynamic-root inspection/destruction;
- deep clone independence and failure cleanup;
- object field order and lookup;
- sequence ordering and limit failures;
- set semantic uniqueness, first-insertion order, equality/hash requirements,
  and clone behavior;
- deterministic map indexed/serialization order plus lookup correctness;
- borrowed Range/view generation invalidation after mutation;
- release of all temporary CSTL/provider state on parse/conversion failure;
- schema overlay remaining independent from the immutable dynamic CMeta graph;
- supported native typed parse/serialize performing no dynamic-root allocation;
- no private DataBind container reserve/growth/hash engine after cutover.

Closure gates include:

- focused DataBind dynamic/CMeta/CSTL tests;
- full TBE/DataBind CTest;
- ASan and UBSan configurations;
- public C and C++ header consumers;
- generated static/shared consumers where dynamic/native interaction is covered;
- installed consumer and dependency-closure checks;
- benchmark/conformance checks relevant to changed dynamic paths.

## #46 completion criteria

#46 may close only when:

- every supported owning dynamic value exposes canonical CMeta identity;
- object/sequence/set/map storage is owned by CSTL, not DataBind-private
  growable arrays or maps;
- Range/reflection traverses canonical CMeta/CSTL-backed storage;
- child/range lifetime and mutation invalidation are explicit and tested;
- runtime-schema parsing can publish an owning dynamic root without generated C;
- native struct conversion does not materialize a dynamic tree unless explicitly
  requested;
- failures release temporary CSTL/provider state and preserve prior destinations;
- `DataBindObject`/`DataBindRecord`, if still present, share the same owning value
  model and have no separate storage/conversion semantics;
- no compatibility facade, hidden fallback, CBind delegation, or second binder is
  present;
- full CTest, sanitizers, public C/C++ headers, installed consumers, and the exact
  changed-path CI gates are green.

Only after #46 is merged may #48 split `data_bind.c` by the now-proven ownership
boundaries.
