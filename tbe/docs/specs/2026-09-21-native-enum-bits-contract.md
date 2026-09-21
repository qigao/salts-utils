# DataBind canonical enum-bits reader contract

Issue: #99
Consumer join: qigao/turbodb#52

## Scope

This slice extends the plain-CMeta native reader/lifecycle from #106/#107 to canonical
`CMETA_DATA_ENUM` descriptors backed by `cmeta_data_enum_bits_ops`.

The canonical enum domain is authoritative. DataBind must not route through the legacy
`cmeta_data_enum_ops` int64 adapter and must not copy CBind's enum engine.

## Accepted CSerde inputs

For a canonical enum domain:

- `CSERDE_SINT`: convert the signed integer to the domain's width-bit two's-complement
  representation. Negative values are valid only for signed domains and must fit the
  declared width.
- `CSERDE_UINT`: use the exact low-width canonical bits when they fit the declared width.
- `CSERDE_STRING`: match an item by exact `symbol` or exact `text`.

Membership is enforced by the canonical CMeta facade:

- ordinary enums require an exact declared item;
- flags accept only subsets of `declared_mask`, including zero.

No textual numeric parsing, case folding, aliases outside the canonical item table, or
legacy enum-shape fallback is introduced.

## Native lifecycle

`data_bind_native_init` and `data_bind_native_clear` must support canonical enum
providers through their semantic-zero/restore contract. Provider cleanup or assignment
postcondition failure is a runtime error and must leave the staging/native value in
semantic zero where the CMeta facade guarantees rollback.

## Atomicity

Reader decode continues to stage before publication. Invalid membership, unknown string,
width/sign mismatch, or provider assignment failure must not publish a partial destination.

## TurboDB relevance

The current Redis cursor admits `CMETA_DATA_ENUM`. Integer Redis replies become
`CSERDE_SINT`; bulk-string replies become `CSERDE_STRING`. Supporting both numeric
canonical bits and symbol/text strings is therefore required by the active consumer path.

Optional/container/variant decoding remains outside this slice.
