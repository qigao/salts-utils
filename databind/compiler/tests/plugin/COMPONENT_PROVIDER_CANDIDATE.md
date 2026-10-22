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
by the canonical Salts ServiceHost generation/Scope/drain host.

**Bounded first slice:** one selected Service with exactly one non-throwing
operation. Multiple operations, typed errors, empty/ambiguous Components,
invalid IR, unsafe output names, and unsupported data carriers fail closed.
No partial sidecar artifacts are retained on codegen error. This does not
claim support for all IDL Service shapes.

The **default source-only** qualification runs the generator and checks
complete FR2/ProviderBinding emission and negative shape admission. The
**real DSO qualification is a separate explicit opt-in**:
`-DDATABIND_ENABLE_COMPONENT_PROVIDER_CANDIDATE_DSO=ON`, and requires the
exact source-built upstream `Salts::ComponentProviderABI` and
`Salts::ServiceHost` targets; missing targets are a configuration error,
not permission to invent duplicate ABI headers or a fallback runtime. When
those targets are supplied, C11/C++17 and real Plugin load/lease,
Configurator graph, typed invocation, Scope BUSY→drain and unload are exercised. Candidate
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

## Current-master owner refresh (2026-10-22)

The test-only branch is based on current SaltsUtils master
`ad56e12da102c51206f5b37731456780d248b9f4`. The exact-source Provider gate
now pins current Salts master `a51947d241a60bc92e92efa8fd6de1d0bb2fbac2`
rather than the stale pre-ServiceHost source
`23032bfe124df327317ce9b4d132054e99b2c0cb`. It builds and installs the
canonical `Salts::Configurator`, `Salts::ServiceHost`, and
`Salts::ComponentProviderABI` from that exact source, verifies the installed
ABI epoch/layout markers and exported targets, then compiles the generated
Provider DSO and host against those installed exports.

The official published Salts SDK still does not contain the current canonical
ServiceHost/Configurator ABI. Therefore this remains an exact-source candidate
gate, not an installed immutable-SDK release claim. The PR remains
**DRAFT / DO NOT MERGE / DO NOT PUBLISH**; production defaults and package
publication are unchanged.
