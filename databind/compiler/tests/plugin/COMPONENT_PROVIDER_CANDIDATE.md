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

## Current-master owner refresh (2026-10-23)

The root PR branch is based on SaltsUtils master
`2a8745db216e6bab7dbd043d6e0a0f19d4f1f7ac`. The root source-code
qualification was recorded on HEAD
`b100f410b2cad70894930bdd55635e9b0bb8a00f` (27 commits ahead and zero
commits behind master); the later documentation-only refresh does not change
compiler/runtime code. The exact-source Provider gate pins current Salts master
`a6f49357b8886cecf2badd4896b04b4e0547f1be`, replacing the previous pin
`a51947d241a60bc92e92efa8fd6de1d0bb2fbac2` and the older pre-ServiceHost
source `23032bfe124df327317ce9b4d132054e99b2c0cb`. It builds and installs the
canonical `Salts::Configurator`, `Salts::ServiceHost`, and
`Salts::ComponentProviderABI` from that exact source, verifies installed ABI
epoch/layout markers and exported targets, then compiles the generated
Provider DSO and host against those installed exports.

Exact-head qualification at root HEAD `b100f410b2cad70894930bdd55635e9b0bb8a00f`:
- [Exact-source positive Provider DSO gate #39788382125](https://github.com/qigao/salts-utils/actions/runs/39788382125): SUCCESS.
- [Normal five-platform CI #39788382219](https://github.com/qigao/salts-utils/actions/runs/39788382219): SUCCESS.

The stacked validation-only [PR #688](https://github.com/qigao/salts-utils/pull/688)
also passed its recorded exact-head gates at `8ec8c58b3a798d1920bf8e1d93a8b76994337cc9`:
[macOS/Windows positive and negative Provider DSO tests #39789468916](https://github.com/qigao/salts-utils/actions/runs/39789468916),
[exact-source ServiceHost with ASan/UBSan and TSan #39789469022](https://github.com/qigao/salts-utils/actions/runs/39789469022),
and [normal five-platform CI #39789468953](https://github.com/qigao/salts-utils/actions/runs/39789468953).
These source-built results do not qualify a published SDK.

## Immutable published-SDK gate — still blocked

The published Salts.Native `v2.3.0-rc.17` does not contain
`component-runtime/include/salts/service_host.h`; the
`v2.3.0-rc.18` tag is not present yet. The next release must pass Salts'
tag-scoped six-RID installed ServiceHost ABI, package/manifest provenance,
formal and benchmark gates. Then this consumer must build against that exact
published asset and pass installed C11/C++17 and real Provider DSO
admission/invocation/stop-drain/unload tests.

Release execution remains blocked by [Salts #1267](https://github.com/qigao/salts/issues/1267):
the current Salts `master` reports `protected: false`, branch protection is
disabled, and no repository rulesets are present. Repository administration
must enable and verify protection before an immutable release tag is created.
No release tag, package publication, production switch, or merge is authorized
by this test-only candidate.

**DRAFT / DO NOT MERGE / DO NOT PUBLISH** until the immutable published-SDK
and downstream consumer gates are complete.
