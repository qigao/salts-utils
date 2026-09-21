# Native workspace and row-budget accounting

Refs: qigao/turbodb#58, qigao/turbodb#56, SaltsUtils #99/#111.

DataBind is a SaltsUtils component. Consumers use only `Salts::Databind` from
an explicitly selected SaltsUtils installation. There is no separate package,
root, alternate target, CBind fallback or second decoder.

## Prepare before source dispatch

`data_bind_native_probe_workspace_size(max_depth, &bytes)` returns checked
traversal capacity for any base address. `data_bind_native_measure()` uses that
probe to validate the same canonical graph as native init/decode and publish
workspace requirements only after success. Neither function invokes a reader,
provider lifecycle or allocator, and neither retains pointers. All control
records and immutable descriptor storage must be disjoint from mutable storage.

A caller allocates the probe, measures, frees the probe, and allocates the exact
private workspace with the reported alignment. Every lifecycle/decode call still
validates the graph; the measurement is not a cached admission bypass. Allocation
failure remains OOM, and callers can complete all preparation before opening a
native cursor. Descriptor lifetime and immutability remain caller obligations.

## Actual resource use

Let `P` and `A` be descriptor-pointer size and alignment, `D` the native depth
limit, `R` the root native size, and `F` the maximum sum of `ceil(field_count / 8)`
over simultaneously active Struct frames. The decoder now uses actual one-bit
field tracking, including duplicate and missing-field detection.

- `traversal_bytes = lifecycle_bytes = D * P`.
- `staging_bytes = R`; `field_tracking_bytes = F`.
- `decode_bytes = align_up(R, A) + D * P + F`.
- `workspace_alignment = lcm(root_alignment, A)`.

Arithmetic is checked; the LCM does not assume power-of-two canonical alignment.
These capacities are exact for a suitably aligned base. To align an arbitrary
allocation, check `decode_bytes + workspace_alignment - 1`, align within it, and
retain the original pointer for freeing. An aligned probe needs `D * P`; the
bootstrap query includes `A - 1` extra bytes for an arbitrary probe address.

The requirements exclude provider heap capacity/headers, output storage and C
stack frames. Depth bounds recursive traversal. Static descriptor metadata bounds
its node set; native options also enforce an explicit total node budget. Unknown,
cyclic, overlapping, mismatched and unsupported graphs still fail validation.
Measurement cannot predict payload-dependent allocation success.

`container_depth` counts only Struct frames, zero for scalar/enum/buffer roots;
`descriptor_depth` includes scalar leaves. The requirements record is new in the
unreleased #111 feature, so its added field must be consumed with the matching
header and `DATA_BIND_NATIVE_REQUIREMENTS_INIT`. Existing options/diagnostic
records and their ABI are unchanged.

## Aggregate and per-value limits share one decoder

The original `data_bind_native_decode()` retains its aggregate
`options.max_owned_bytes` policy. The additive
`data_bind_native_decode_bounded(..., max_buffer_bytes, diagnostic)` uses exactly
the same graph validator, native decoder, publication and rollback path. It adds
a per-owned-value limit without weakening the aggregate bound. Before each
provider assignment the limit is `min(aggregate_remaining, max_buffer_bytes)`.
Zero permits empty payload only. Both limits count logical string/bytes payload,
not allocator headers or spare capacity. The sum cannot wrap.

Two three-byte values with aggregate six and per-value three succeed; a single
four-byte value fails even when aggregate capacity remains. An aggregate of five
still rejects two three-byte values. The tests exercise these separately,
including zero-length payload and late-field rollback to semantic zero.

## ORM translation

TurboDB's row contract is preserved rather than mapped by similarly named fields:

| ORM limit | Translation |
| --- | --- |
| `scratch_bytes` | Actual simultaneously active field bitmaps; compare with measured `field_tracking_bytes`. Private traversal/staging is measured and allocated separately, not charged as caller scratch. |
| `max_depth` | Container-only bound. Check addition of one for native leaf-inclusive depth and independently reject measured `container_depth` over the original bound, including empty Struct nesting. |
| `max_container_items` | Per dynamic collection. The supported native-v1 graph has no dynamic collections; static Struct fields are not collection elements. Unsupported collection/optional kinds are rejected rather than silently admitted. |
| `max_buffer_bytes` | Pass unchanged to the bounded decoder as a per-value logical payload limit. Aggregate accounting remains overflow-safe at the address-space ceiling. |

For the supported static graph, ORM measures the canonical node count and uses
that exact count as its runtime total-node limit. It does not multiply the caller's
collection limit or expand its meaning. A two-field scalar row with scratch one,
depth one and collection limit zero is therefore valid, while scratch zero is not.

ORM uses a private prepare/publish pair. Preparation validates and allocates before
backend `open_cursor`, shape configuration or `next`; publish configures and moves
the cursor only on success. Failures leave unconsumed preparation/cursor owned by
the caller. The existing connection reservation and query/transaction holds cover
native creation and cleanup; the binder creates no lock, queue or owner registry.

## Validation boundaries

Existing canonical graph, lifecycle and enum behavior remains tested. Measurement
boundary assertions reflect the smaller *actual* bitmap storage, not inflated
capacity. New per-value/aggregate C cases and the existing C++17 consumer exercise
the shared bounded decoder. ORM retains original budgets and behavioral assertions;
only fixture reflection entries that lacked canonical `size_t`/`tstr` identities
are bound to the actual canonical metadata during setup.

Local source-level sanitizer results do not replace installed-package or complete
Windows/Linux acceptance. #58/#59/#60 remain subject to the original exact-head
consumer matrix, ownership/race/failure regressions, real PostgreSQL and unfiltered
root suite. Dynamic container/optional support is not added by this fix.
