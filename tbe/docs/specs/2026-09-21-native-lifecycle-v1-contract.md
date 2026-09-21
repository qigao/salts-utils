# DataBind plain-CMeta native lifecycle v1 contract

Issue: #99
Consumer join: qigao/turbodb#52

## Scope

This slice defines lifecycle operations for native storage described only by a canonical
`cmeta_data_desc`. It deliberately does not require `TbeTypedDescriptor`, schema text,
format parsers, CBind, DataBindValue, or generated wire metadata.

The intended public C ABI is:

```c
DataBindStatus data_bind_native_init(
    const DataBindNativeOptions *options,
    const cmeta_data_desc *shape,
    void *destination,
    size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);

DataBindStatus data_bind_native_clear(
    const DataBindNativeOptions *options,
    const cmeta_data_desc *shape,
    void *destination,
    size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic);
```

Both operations reuse the direct-reader v1 options, workspace, depth/item budgets,
diagnostics, graph validation, storage-identity rules, Struct range checks, and alias
preflight.

## `init`

`init` converts caller-provided raw native storage into the descriptor-defined semantic
zero state.

- The complete descriptor graph is preflighted before mutation.
- Invalid scalar width/native kind, malformed/overlapping Struct layout, unsupported
  descriptor kinds, invalid destination size/alignment, exhausted graph budgets, or
  control/workspace aliasing fail before mutation.
- STRING/BYTES use the canonical v2 provider `init_zero`; plain byte-zero is not a
  substitute for provider semantic zero.
- Struct initialization recursively initializes fields after full graph preflight.
- On callback failure, already initialized provider fields are restored so the object is
  safe to clear/retry; the function must not report success unless the complete object is
  semantic zero.

## `clear`

`clear` restores a live native value to descriptor-defined semantic zero.

- The complete descriptor graph is preflighted before mutation.
- Scalars are restored to their canonical zero.
- STRING/BYTES use provider `restore_zero`.
- Struct fields are cleared recursively; parent-wide `memset` must not overwrite a
  provider-defined semantic-zero state.
- Repeated clear is valid and idempotent for valid providers.
- Provider cleanup failure is reported; success means the complete object satisfies the
  descriptor-defined zero state.

## Ownership and alias rules

The caller owns options, workspace, descriptor, destination, and diagnostic. DataBind
retains none of them. Workspace and destination must not overlap. A diagnostic that
overlaps mutable workspace or destination is rejected without writing through the
diagnostic pointer.

## Consumer purpose

TurboDB currently byte-zeros row output before binding. That is not a general semantic
zero operation for owned `tstr`/provider storage. This lifecycle boundary is the
DataBind-owned replacement for that behavior; TurboDB must not duplicate recursive CMeta
lifecycle traversal.
