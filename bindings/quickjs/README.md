# QuickJS binding

QuickJS starts directly as a CMeta-driven runtime binding.

Do not copy the historical generated Lua/TBE binding model. JavaScript object
properties, functions and ownership must be projections of canonical CMeta
metadata.

TypeScript declaration generation belongs to the DataBind/compiler projection
layer and is not part of this runtime module.

Existing native libraries need no CMeta declarations. External binding macros and
C++ type deduction generate the shared metadata and exact adapters. Include
`salts/bindings/quickjs/native.hpp` for typed calls from C++ into JS functions.
TypeScript is compiled to JavaScript by the application's own build tool.

## Runtime projection

- strings become JavaScript strings and bytes become owned `ArrayBuffer` copies;
- structs become plain objects whose property names come from CMeta fields;
- sequences and sets become arrays;
- maps become arrays of `{key, value}` entries, preserving non-string and
  repeated keys without inventing a JavaScript-object key policy;
- native reads use CMeta temporary storage and publish only after full success;
- reflected functions and interface methods invoke through `cmeta_invokable`.

Converted values own their JavaScript storage. Native object proxies and function
closures borrow their native owners, descriptors and providers until the last
retained JS reference is released; extracted functions also count as references.

`salts_quickjs_push_module` in `salts/bindings/quickjs/module.h` registers a whole
native export table as one owned `JSValue`. It does not modify globals or the ES
module registry; publication belongs to the host. Release that value before
destroying its context. On failure the result stays undefined; VM allocation or
property errors may also leave a pending JS exception for the host to consume.
See the shared [module contract](../README.md) for examples and the producer API
that emits a TS interface from the same CMeta table.
