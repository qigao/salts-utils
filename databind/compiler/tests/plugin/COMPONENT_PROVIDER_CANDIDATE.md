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
response OUTPUT|BORROWED semantics, exact native FunctionAbi and compile-time
function-pointer proof. **Its request/response pointer descriptors reuse the
original NativeSourceIR `AddRequest_native_cmeta_type` and
`AddResponse_native_cmeta_type` providers.** The existing Contract-only
FunctionMeta/FunctionAbi remains authoritative; no new atom/type identity is
synthesized from Binary-facing naming or display strings. The canonical
`cmeta_component_desc.stable_id` remains `WasmRuntime.Calculator`.
Its original `salts.component.provider.v1` binding is leased and exercised
by the existing Salts ComponentPlugin generation/Scope/drain host.

**Bounded first slice:** one selected Service with exactly one non-throwing
operation. Multiple operations, typed errors, empty/ambiguous Components,
invalid IR, unsafe output names, and unsupported data carriers fail closed.
No partial sidecar artifacts are retained on codegen error. This does not
claim support for all IDL Service shapes.

The **default source-only** qualification runs the generator and checks
complete FR2/ProviderBinding emission and negative shape admission. The
**real DSO qualification is a separate explicit opt-in**:
`-DDATABIND_ENABLE_COMPONENT_PROVIDER_CANDIDATE_DSO=ON`, and requires the
exact unpublished upstream `Salts::ComponentPluginABI` and
`Salts::ComponentPlugin` targets; missing targets are a configuration error,
not permission to invent duplicate ABI headers or a fallback runtime. When
those targets are supplied, C11/C++17 and real Plugin load/lease,
Component graph, typed invocation, Scope BUSY→drain and unload are exercised. Candidate
runtime ownership stays in Salts. Public `salts-idlc` flags and CMake
`salts_idl_target` semantics remain unchanged until a separate integration
review grants an additive opt-in artifact entry.

**DRAFT / DO NOT MERGE OR PUBLISH.** No SDK or production CHttp consumer
may switch based solely on this fixture.

## Shared NativeSourceIR ISO C++17 syntax gate

The standard Contract-only NativeSourceIR header is shared by both C and
C++ consumers. Generated CMeta type/field descriptors, DataDesc binding
helpers and caller-owned native storage now use portable C11/C++17 aggregate
initialization and `CMETA_ALIGNOF` rather than C11-only `_Alignof`, C99
compound literals or C++20 designated initializers. This changes generated
**source text** (not the ABI, semantic TypeIdentity, Plugin export schema,
execution target or default artifact selection). The isolated real DSO
qualification enforces `-std=c++17 -pedantic-errors` for the generated header.
Existing Native/Plugin generated output is separately requalified as part of
normal CI. No alternate C++ record runtime or type provider is introduced.

## Stacked bounded multi-operation FR2 qualification

The isolated child candidate adds **two to four non-throwing operations in one
canonical Service** without inventing a second Interface per method, runtime
registry, FFI execution path or module Owner. One `ComponentOps.MathProvider`
Component/Service emits methods `invoke_Add` and `invoke_Multiply` under
one `CMETA_INTERFACE` and one standard ProviderBinding/DSO lease. Each FR2
method uses exactly the preexisting NativeSourceIR request/response CMeta
record identity and ordinary C11 function pointer ABI. The single-operation
candidate's original `invoke` spelling, generated source and test remain
unchanged.

- Admission bounds: one Component capability, one selected Service, **1..4**
  distinct named operations; duplicate names/symbols/qualified operations,
  foreign service, fifth operation and any typed-error envelope fail closed.
- Compiler: same canonical ServiceNativeIR, distinct typed method slots,
  `CMETA_IMPLEMENTS`, compile-time function pointer checking and a single
  ProviderBinding export.
- Tests: a real two-operation Plugin Provider DSO under the existing
  Salts-owned ComponentPlugin Scope, two different business results,
  strict ABI contracts, C11/C++17 and lease BUSY→0; negative real typed-error
  Schema rejects with **zero generated output**.
- All new DSO execution remains behind the existing
  `DATABIND_ENABLE_COMPONENT_PROVIDER_CANDIDATE_DSO` source gate; ordinary
  `salts_idl_target` artifacts, public SDK and production CHttp stay
  unchanged.

This is **not** acceptance of arbitrary service collections, typed-error,
or public generated Component registration. The first slice's positive tests
stay unchanged as an independent regression gate.
