# DataBind TBE format backend

This directory owns only TBE-specific representation semantics.

## Ownership

Owned here:

- TBE endian primitives;
- TBE binary wire reads/writes;
- TBE format/backend version metadata.

Not owned here:

- DataBind IDL/schema parsing;
- generic schema diagnostics;
- CMeta native type/function semantics;
- generic DataBind typed/native binding;
- BindingPlan compilation/execution;
- JSON/YAML/CSV/XML adapters.

The current `tbe_typed.*` implementation remains under `databind/runtime/`
until its generic multi-format/native semantics are split from its TBE binary
encode/decode path. Moving that mixed implementation here before the split
would preserve the old ownership error under a new directory.

Build consumers use `Salts::DataBindTbeFormat`. DataBind runtime links it
privately; TBE-specific tests may link it explicitly.
