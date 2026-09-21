# Native workspace measurement

Refs: qigao/turbodb#58, qigao/turbodb#56, SaltsUtils #99.

DataBind remains a SaltsUtils component. Its only consumer target is
`Salts::Databind`, obtained through `find_package(SaltsUtils CONFIG REQUIRED)`
from the explicit SaltsUtils installation. This change adds no independent
package, alternate target, consumer binder, adapter, or fallback.

## Scope of this slice

The two additive functions in `data_bind_native.h` allow a caller to validate a
native-v1 graph and provision its private workspace **before opening a native
cursor or dispatching source I/O**. Existing options, diagnostics, ABI version,
lifecycle and decode policies are unchanged. Measurement reuses their production
graph validator and calls neither a reader nor provider lifecycle functions.

1. `data_bind_native_probe_workspace_size(max_depth, &bytes)` checks the pointer
   count multiplication and alignment padding. It returns enough temporary
   traversal storage for an arbitrary base address. Zero depth and overflow
   return `DATA_BIND_ERR_LIMIT`; the output remains unchanged on failure.
2. Allocate that probe storage with the caller's existing allocator, and set the
   ordinary native options. `data_bind_native_measure()` validates the complete
   supported graph, including cycles, layout, canonical providers, paths, depth
   and node limits. It publishes the requirements only after complete success.
3. Release the probe; allocate the measured private decode workspace with the
   reported alignment. Keep the same graph and limits. The runtime still
   validates each init/clear/decode; this is sizing information, not a retained
   plan or bypass of admission. Allocation failure stays a caller-side OOM and
   must precede native dispatch.

The probe and control records are borrowed. Immutable descriptors must remain
valid and disjoint from mutable storage. No pointer is retained. Measurement
requires only traversal storage, not an already allocated staging row. Thus the
caller does not have to guess a large buffer or retry with growing allocations.

## Resource accounting

Let `P = sizeof(const cmeta_data_desc *)`, `A` be its alignment, `D` the native
`max_depth`, `R` the root native size and `F` the sum of field counts on the
largest simultaneously active Struct path. Returned requirements are:

- `traversal_bytes = lifecycle_bytes = D * P`.
- `staging_bytes = R`; `field_tracking_bytes = F`.
- `decode_bytes = align_up(R, A) + D * P + F`.
- `workspace_alignment = lcm(root_alignment, A)`.

All arithmetic is checked. The LCM deliberately avoids introducing an assumption
that every shallow-valid canonical alignment is a power of two. Sizes are exact
for a base aligned to `workspace_alignment`. An allocator that does not provide
that alignment must check addition of `workspace_alignment - 1`, overallocate,
and align within its owned allocation. The caller retains the original pointer
for freeing. Do not call aligned-allocation APIs without satisfying their own
alignment and size constraints.

For an already aligned probe base, `D * P` bytes suffice. The separate bootstrap
query returns `D * P + A - 1` so an unaligned probe is also safe. Source tests
exercise each pointer-alignment residue and fail exactly one byte below its
actual traversal requirement.

Requirements exclude logical payload, provider-specific heap capacity/headers,
the caller's output row, and recursive C stack frames. `max_depth`/`max_items`
remain necessary bounds on the recursive validator. Provider allocation and
source data failures still occur at decode time and retain failure-atomic
rollback. Measurement cannot predict data-dependent allocation success.

## Native-v1 limits are not ORM limits

| Native option | Meaning | Zero behavior |
| --- | --- | --- |
| `workspace_bytes` | Private traversal, staging and field tracking capacity | Insufficient for a valid nonzero-depth probe |
| `max_depth` | Descriptor/value depth, root is 1; scalar leaves count | LIMIT |
| `max_items` | Whole-graph descriptor nodes / whole-value visited nodes | LIMIT |
| `max_owned_bytes` | Aggregate logical string/bytes payload per decode, not heap capacity | Only zero-length owned payload is permitted |

The supported graph remains scalar, canonical enum, owned buffer and Struct.
Unsupported container/optional/variant graphs still fail explicitly; measurement
does not expand the capability matrix.

TurboDB's previous row contract instead used active Struct field **bitmaps** for
caller scratch, container-only depth, a per-container item bound and a
**per-value** buffer bound. In particular, a two-scalar flat row with
`scratch_bytes=1`, `max_depth=1` is not represented by direct native-v1 option
assignments. Raising the fixture budget, setting a large multiplier, or calling
these requirements the caller's scratch would silently change that contract.

This slice intentionally does **not** make that translation. The remaining #58
work must preserve those meanings with an explicit SaltsUtils-owned capability,
check it before cursor configuration/open/next, retain no-partial-publication
and ownership behavior, and test exact/one-over per-value and nested bounds.
TurboDB's dependency pin, fixtures, five root-suite failures and #59/#60 gates
are not changed or declared fixed by this prerequisite.

## Validation

The existing C reader/preflight target contains the sizing tests; the existing
C++17 target checks layout, signature, C linkage and calls both new functions.
There is no new workflow, standalone project, test framework or source-policy
substitute for native execution. Original tests and their assertions remain.

Acceptance still requires the normal exact-head SaltsUtils installed-package
CI, followed by the unchanged exact-tuple TurboDB Windows/Linux Debug/Release
matrix once a complete row-budget translation is integrated. Local source-only
ASan/UBSan results are not those installed-package or cross-platform results.
