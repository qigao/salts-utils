# DataBind Canonical CMeta Runtime Completion Design

## Status and scope

This design completes issue #47 after PR #55 established the strict ABI-v2
descriptor boundary for non-optional fixed-width integers, floating-point
values, signed-64-representable non-flags enums, and nested structs through
depth 32.

#47 closes only when every non-container native typed runtime path consumes a
canonical CMeta graph for structural facts. This includes generated Bool,
UUID, fixed bytes, flags and wide enums, owned strings and bytes, custom scalar
adapters, and optional fields. Native and dynamic list, set, and map storage
remain exclusively in #46. File/module decomposition remains in #48.

DataBind remains the sole SaltsUtils binding engine. The implementation must
not depend on the upstream Salts binder and must not add a compatibility facade,
fallback engine, feature flag, inferred native storage, or dynamic-root detour.

## Ownership boundary

CMeta owns semantic identity and exact native structure:

- native field names;
- semantic kinds;
- field offsets, size, alignment, and nesting;
- scalar and enum storage domains;
- lifecycle and conversion operations for owned or adapted values.

The DataBind schema overlay owns external policy:

- wire names and aliases;
- required/optional state and presence bitmap coordinates;
- defaults and validation;
- binary offsets, widths, byte order, and format policy;
- schema fingerprints.

DataBind owns orchestration:

- native decode and encode;
- semantic-zero temporary construction;
- transactional publication and rollback;
- allocation, depth, item, and byte limits;
- format selection and public error translation.

The schema overlay may validate a CMeta graph, but it must never rename CMeta
fields, replace native offsets, manufacture storage, or infer an arbitrary C
ABI. A supported descriptor joins the two immutable authorities as
`{ struct_size, abi_version, overlay, native_data }`.

## Migration strategy

The migration uses four independently gated capability slices. Each slice
starts with a public or production-generated RED, implements only the missing
canonical operation, routes every supported wrapper through it, and removes
the corresponding parallel typed structural metadata as soon as its final
caller disappears.

### Slice A: fixed native values

Generated Bool, UUID, and fixed bytes become eligible only when their CMeta
descriptors exactly match generated C storage.

- Generated Bool uses an explicit octet-backed canonical adapter. The runtime
  must not apply the `_Bool` descriptor to a `uint8_t` slot.
- UUID requires an exact provider-backed descriptor for `salts_uuid_t`,
  including size, alignment, semantic zero, and copy operations.
- Fixed bytes require a bounded native byte-array descriptor containing the
  exact extent and alignment. A general dynamic bytes descriptor is not an
  acceptable substitute.

The overlay continues to own binary width and offset. CMeta owns the actual
host slot and its initialization/copy semantics.

### Slice B: enum domains

Canonical enum operations expand beyond the current signed `int64_t` adapter.
The enum descriptor must represent whether the storage domain is signed or
unsigned, its width, read and assignment operations, declared-value
validation, and flags-mask validation.

Unsigned 64-bit values must never pass through a narrowing signed cast. Flags
accept only bits contained in the declared mask unless the schema explicitly
defines a different policy. After this slice, native enum storage selection
comes only from CMeta; the overlay retains wire width and byte order.

### Slice C: owned scalar lifecycle

Owned string, dynamic bytes, and custom scalar adapters publish canonical
lifecycle and conversion operations:

- semantic-zero initialization;
- clone or move into temporary storage;
- native-to-DataBind-value conversion;
- DataBind-value-to-native conversion;
- clear after success, failure, or rollback.

Parsing constructs a complete semantic-zero temporary object. Successful
conversion atomically replaces the destination. Any allocation, type, range,
or limit failure clears all temporary ownership and leaves the destination
unchanged. Serialization borrows native storage and never transfers ownership.

A custom scalar is supported only through an explicitly registered canonical
CMeta adapter. Unknown custom kinds remain schema errors.

### Slice D: optional presence

Optionality is a composition of two authorities rather than a new structural
kind:

- CMeta describes the native value slot and its lifecycle.
- The schema overlay describes the presence bitmap location, bit index,
  required/optional state, and default.

Descriptor preflight validates both parts together. It rejects missing or
overlapping presence storage, a value field that aliases the presence bitmap,
an incompatible native value descriptor, and any incomplete nested mapping.
It must not publish a CMeta record graph that silently omits native presence
storage.

When a field is absent, an overlay default is converted through the same CMeta
adapter used for input. Without a default, the value remains semantic zero and
presence remains false. Publication sets the presence bit only after value
conversion succeeds. Rollback restores both owned value storage and presence.

## Runtime data flow

For every supported generated or existing native record:

1. Validate the ABI-v2 descriptor and recursively preflight the canonical CMeta
   graph against the schema overlay.
2. Allocate or prepare a semantic-zero temporary using descriptor-defined
   lifecycle operations.
3. Parse format input through the existing public parser/CSerde path.
4. Traverse native fields by CMeta shape and offsets; consult the overlay only
   for external names, presence/defaults, validation, and wire policy.
5. Convert each value through canonical scalar, enum, struct, or lifecycle
   operations.
6. On success, clear the old destination and atomically publish the temporary.
7. On failure, clear the temporary and return the original specific status
   without modifying the destination.

Serialization follows the same graph and overlay without allocating a second
structural representation. Generated C lifecycle, text, binary, and Lua
wrappers call descriptor APIs for supported records. Unsupported records have
no descriptor and fail explicitly; they do not fall back to graphless runtime
metadata.

## Removal policy

Parallel structural metadata is removed capability by capability, in the same
gated change that removes its last caller. Candidates include native kind,
host offset, native nesting, host size/alignment, enum storage selection, and
private init/clear branches currently carried by `TbeTypedType` or
`TbeTypedField`.

Wire-only fields remain in the overlay. Raw typed metadata may remain only for
types explicitly deferred to #46, and it must not be reachable from a
descriptor-backed runtime operation. No shim may synthesize a descriptor from
the old graph.

## Errors and atomicity

- Missing or invalid graph mappings, ABI mismatch, incomplete shape, and absent
  adapters return `DATA_BIND_ERR_SCHEMA` with the most specific type or field
  path available.
- Input semantic mismatches retain `DATA_BIND_ERR_TYPE_MISMATCH`.
- Numeric and enum domain failures retain their range status.
- Allocation and configured depth, item, or byte limits retain their distinct
  statuses.
- Every failure leaves the destination object, presence bitmap, and all owned
  resources unchanged.
- Cleanup failure must not hide the primary conversion error, but debug and
  sanitizer gates must expose lifecycle defects.

## Capability matrix

The checked-in capability matrix is updated with every slice. Each schema kind
records its canonical CMeta kind, exact native storage requirement, lifecycle
provider, overlay responsibilities, supported formats, and explicit rejection
reason. A type is marked supported only after generated and existing-struct
consumers use the same semantic identity and runtime operations.

Container rows remain deferred to #46 throughout #47. Passing dynamic-value or
container tests does not imply native container support.

## Verification and closure

Every slice must cover the relevant generated C and C++ consumers, existing
structs, JSON/YAML/CSV/XML, binary encoding, Lua adapters, static/shared
linkage, install/export consumers, and negative atomicity cases. Focused tests
run before the complete TBE/DataBind suite under ASan and UBSan.

#47 may close only when:

- all non-container generated and existing-struct native parse, serialize,
  init, clear, and rollback paths obtain structural facts solely from CMeta;
- the overlay contains only schema and wire policy;
- every obsolete parallel typed structural field and helper has no caller and
  is removed;
- missing mappings fail atomically without fallback;
- the capability matrix and migration notes match implemented behavior;
- C/C++ generated consumers, Lua, installed consumers, full CTest, ASan/UBSan,
  and the exact-head schema, enum, descriptor, and direct-parser gates pass;
- list, set, and map native storage remains explicitly unsupported and tracked
  by #46.

After #47 closes, #46 owns container migration and #48 may split DataBind along
the now-proven ownership seams.
