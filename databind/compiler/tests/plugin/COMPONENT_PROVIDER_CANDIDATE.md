# Isolated IDL → ComponentProvider (SaltsUtils #620)

This is an explicitly opt-in **test-only** projection from the **existing**
IdlContract + canonical ServiceNativeIR to a CMeta `FR2` service Interface,
typed `CMETA_IMPLEMENTS` vtable and `salts_component_provider_binding`.
It does not introduce a new IDL grammar, Plugin loader, lifecycle authority,
FFI thunk, fallback path, or public installed artifact.

The original `salts_idl_target(ARTIFACTS PLUGIN)` codegen and its typed
function exports are unmodified. A separate candidate-only fixture generator
produces `contract_plugin.component.h/.c` from the selected canonical
`WasmRuntime.Calculator` Component and its single `Calc.Add` operation,
and compiles those into a separate real DSO (one `cmeta_plugin_query`).

The service Interface method is:
`int invoke(void *self, const AddRequest *, AddResponse *)`.
It is fully reflected as FR2 with explicit request INPUT|BORROWED and
response OUTPUT|BORROWED semantics, an exact native FunctionAbi, typed CMeta
identities and compile-time function-pointer proof. The canonical
`cmeta_component_desc.stable_id` remains `WasmRuntime.Calculator`.
Its original `salts.component.provider.v1` binding is leased and exercised
by the existing Salts ComponentPlugin generation/Scope/drain host.

**Bounded first slice:** one selected Service with exactly one non-throwing
operation. Multiple operations, typed errors, empty/ambiguous Components,
invalid IR, unsafe output names, and unsupported data carriers fail closed.
No partial sidecar artifacts are retained on codegen error. This does not
claim support for all IDL Service shapes.

The qualification builds the generated real DSO and independently checks
ISO C11/ISO C++17 descriptor compatibility, Plugin load/lease, Component
resolution, typed invocation, Scope BUSY→drain, and release/unload. Candidate
runtime ownership stays in Salts. Public `salts-idlc` flags and CMake
`salts_idl_target` semantics remain unchanged until a separate integration
review grants an additive opt-in artifact entry.

**DRAFT / DO NOT MERGE OR PUBLISH.** No SDK or production CHttp consumer
may switch based solely on this fixture.
