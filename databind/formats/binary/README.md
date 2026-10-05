# DataBind Binary Format

This directory owns the canonical DataBind Binary wire/layout representation.
The public/runtime format identity and compiler/config spelling are `binary`;
the concise schema annotation namespace is `@bin.*`.

Binary wire helpers, version metadata and compiler-side layout overlay live here.
BinaryLayoutIR is validated by the compiler and emitted as BinaryLayoutPlan
providers. Runtime readers/writers expose CSerde tokens to MessagePlan/native
execution. Binary owns wire representation; CMeta providers own native storage
and lifecycle. Unsupported layouts fail before decoding or publishing output.

## Shared lowering for runtime-known schemas (#489)

The compiler and generic runtime must produce the same immutable execution
plan from Contract IR and the Binary format plan. BinaryLayoutIR and its
lowering therefore live in this private format module. The existing private
`databind_binary_contract` dependency supplies both consumers; runtime does not
depend on idlc or its template/CLI helpers. Generated plans remain static and
perform no schema-name lookup during execution.

Duplicating the compiler lowering in the generic runtime would create two
admission authorities. Inferring wire layout from CMeta/native offsets would
change the wire ABI. The chosen shared lowering preserves field ranges, state
bitmaps, endian order, fixed-array counts and GROUP/VAR_DATA framing. CMeta
continues to own native lifecycle, and Contract/MessagePlan owns logical state,
defaults and validation.

Lowering is a single-threaded control operation. An owned graph retains its IR
strings and exact-sized plan tables until all readers/writers close. It admits
at most 4096 types and 65536 fields, checks allocation arithmetic, rejects cycles
and bounds active container depth to the existing Binary limit. A failure
destroys the candidate graph and publishes no plan. Admission changes no wire
bytes or native C array ABI; unsupported shapes remain explicit errors.

Validation compares generated and runtime-known byte output in both byte
orders, including ABSENT/NULL/VALUE, nested records, fixed arrays and tails.
Generic Binary parse and object serialization now build this graph and use the
same canonical reader/writer as generated providers. The old `EF_*` plans,
measure pass and separate wire reader/writer have been deleted. A staging graph
outlives every borrowed execution lease and is destroyed after they close.
Decoded CSTL value owners receive canonical schema identities before publication
and remain independent of both the input bytes and the codec lifetime.
Runtime format projection includes the requested record and its transitive
record dependencies. An unsupported unrelated declaration does not invalidate
that root; an unsupported selected root or dependency rejects the entire plan.
Generated native artifacts use the same root projection to admit each message.
They expose an explicit schema error for rejected Binary shapes; allocation
failures abort generation. A requested Binary transport must build its format
plan successfully. Generation never clears a failed format plan and continues
with an empty substitute.

Execution requires plan ABI 2 and complete current layout, field and element
records. ABI 1 providers are rejected even when their record sizes match the
current sizes. Old record prefixes are rejected before reading missing
representation metadata or opening a lease; no implicit FIXED representation
or zero enum flags are supplied. Consumers must regenerate their Binary
providers with the current compiler. The byte grammar remains unchanged.

## Generated MessagePlan preparation

Generated Binary entry points acquire a codec-owned MessagePlan by the immutable
generated native-artifact address. A first acquisition prepares that exact plan;
later acquisitions perform only a bounded address lookup. Applications can call
`data_bind_message_plan_acquire_generated` during startup to keep all compilation
outside execution. Neither decode nor encode compiles schema facts after a warm
acquisition. CSerde field-name matching consumes the prepared field tables; it
does not query the schema or a provider registry.

Per-call compilation repeats schema lookup and allocation. Embedding one global
generated semantic plan would ignore a caller codec's constraints. Preparation
per codec preserves that codec's Contract/ValidationPlan facts while leaving
generated Binary wire providers and CMeta lifecycle metadata unchanged. The plan
copies the binding record; metadata remains borrowed. A generated module must
outlive codecs that prepared its artifacts, including shared-library unload.

Preparation admits 4096 artifacts and 65536 total field records per codec, through
the named `DATA_BIND_MESSAGE_PLAN_MAX_PREPARED` and
`DATA_BIND_MESSAGE_PLAN_MAX_PREPARED_FIELDS` build definitions. The fixed slot
table uses a CSTL typed Vec, constructed outside the codec mutex. On a 64-bit
host its pointer pairs occupy `4096 * 2 * sizeof(void*) = 65536` bytes, plus Vec
metadata; owned field names and validation rules belong to each admitted plan.
Lookup is O(P) for P prepared artifacts. This is a bounded lookup contract, not
a measured throughput claim.

Concurrent cold callers build candidates outside the lock. Publication under
the mutex selects one complete plan and consumes its capacity budget once;
losing candidates are destroyed outside the lock. There is no eviction, schema
mutation, replacement or alternate execution path. Full returns LIMIT and a
failed candidate leaves existing plans usable. Plans remain immutable, with
independent execution workspaces, until quiescent `data_bind_free` destroys them.
Generated CMeta initialization uses Salts once publication so concurrent callers
cannot observe partially filled field tables.

Formal tests cover concurrent first use, stable warm plans without resolver
calls, copied-binding lifetime, distinct codec constraints, failed preparation,
the exact plan limit, and repeated Binary ownership round trips.

## Counted collections and cursor fields

The historical generic wire grammar writes list/set values and string-key maps
with a u32 item count at their declared position. A fixed scalar or array may
follow a collection, so its offset depends on the encoded collection size.
Moving every collection behind a fixed block would change existing wire bytes;
rejecting subsequent fixed fields would remove supported generic behavior.

The private format plan and BinaryLayoutIR represent the fixed prefix, counted
collections, and exact-width cursor fields separately. Element signedness and
width still come from canonical CMeta semantics; the count is a wire quantity.
Map key/value and ordered-set ownership remain with Contract/CMeta, not Binary.
This metadata adds no native offsets, host allocation policy, or public type
system. Build failure destroys the candidate IR and publishes nothing.

COUNTED and CURSOR_FIXED execute through that same plan. Scalar, string/bytes and
fixed-record collection values are admitted; string-key maps emit canonical MAP
tokens. Record elements derive their extent from their own validated wire IR.
Formal tests compare big-endian record collection bytes with a literal oracle,
and generic tests cover mixed field order, insertion order, duplicate-key
updates, state prefixes and bounded output. Variable-record elements remain
explicit admission errors.

The Binary reader/writer defaults to a 16 MiB complete payload and 65536 items
per counted collection. These hard bounds are named library build definitions
`DATA_BIND_BINARY_LAYOUT_MAX_PAYLOAD_BYTES` and `DATA_BIND_BINARY_LAYOUT_MAX_ITEMS`;
reaching either returns LIMIT before output publication. Generic value traversal
also uses its existing total node/view budgets. Generic allocated output copies
the finished writer buffer once; bounded output checks its full required length
before copying any bytes into the caller's destination.
Generated bounded output follows the same rule: an insufficient destination
receives no bytes, returns LIMIT, and reports the complete required length.
