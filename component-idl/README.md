# Component IDL — isolated static authoring candidate

Tracking: [SaltsUtils #621](https://github.com/qigao/salts-utils/issues/621), parent [#620](https://github.com/qigao/salts-utils/issues/620), runtime owner [Salts #1154](https://github.com/qigao/salts/issues/1154).

This is a compile-time-only adapter over canonical `IdlContract`, not another parser, DI runtime, Plugin loader or service registry. The actual candidate Configurator/ServiceHost is Salts-owned (Draft #1155). This module is OFF by default, uninstalled/unexported, and does not modify existing `salts-idlc` NATIVE/PLUGIN/WASM/TYPES outputs.

P1 explicitly admits Components containing 1–16 Services, each with 1–16 sync Request → Response operations, with required scalar `uint32` records and no typed errors or ownership transfers. It generates CMeta typed Interface declarations, the original immutable qualified Component ID and a native `salts_component_provider_binding` helper. All executable create/activate/deactivate and borrowed Interface projection authority remain explicit inputs from business C code. The first slice uses static native R2 Interface methods (no invented dynamic FunctionAbi), no DSO admission and no fallbacks. Unsupported shapes must fail before any output.

Generated types come from the **existing** `salts_idl_target(ARTIFACTS TYPES)` frontend. The P1 fixture calls the canonical Salts::IDL parser rather than implementing a second language. Once this path is qualified, #620 can extend exact reflected execution and real Plugin Provider export in a separate increment.

**DO NOT MERGE / DO NOT PUBLISH** before executable CI evidence.


## P1 C++17 carrier boundary

The candidate generates a C++17-safe **opaque** request/response pointer
declaration using the same admitted `IdlContract` (not a second record
layout). The original `salts-idlc ARTIFACTS TYPES` header is included only
by C11 consumers: its C11-only compound-literal init/clear/move helpers are
**not** rewritten or silently adjusted for MSVC C++. C++17 can include and
type-check the generated Interface/Provider ABI, but a native C11 business
module remains the owner of concrete record storage/lifecycle in this P1
slice. Direct C++17 construction of IDL records is **not yet supported**
and must be separately qualified before claiming a full C++ Service client.

The candidate macOS CI matches the AppleClang-compiled Salts.Native /
TinyTest SDK on this one branch, rather than compiling with GCC 15's
incompatible emulated thread-local storage representation.

## P2.0 (stacked, not a public artifact)

`feature/component-idl-dso-provider` adds an opt-in real Provider DSO built
from this exact generated ComponentDesc, with a separate C11 host executable
using only `Salts::ComponentPlugin` and `Salts::Plugin` from the released
Salts SDK. The DSO uses the **original** `salts_component_provider` Interface
and canonical `CMETA_PLUGIN_DECLARE` (not the different Plugin FUNCTION
export contract). The first slice deliberately writes business factory,
projection and Plugin export declarations as ordinary C code to prove real
DSO lifetimes, before their later generated adapter replaces boilerplate.

Acceptance includes typed call across a real lease, two complete generations,
N still callable while N+1 is published, BUSY drain, exact lease count 2→1→0,
explicit stop/quiescence and final unload. Provider DSO never links the host
registry/ServiceHost runtime. The original P1 branch remains unchanged and
no candidate header/target is installed or published.

## P2.1 explicit IDL-first Component Provider INTERFACE export

An explicitly selected `--provider-plugin` codegen output produces a
**separate** `calc_component_plugin.h` from the already-admitted canonical
`IdlContract`. This does not modify `salts-idlc ARTIFACTS TYPES/NATIVE/PLUGIN/WASM`
or reinterpret Plugin FUNCTION exports.

The business C11 DSO still provides real typed Service implementation,
ObjectRef creation, immutable Salts Provider Binding and borrowed Interface
projection. Instead of handwritten `get_binding` vtable, Plugin INTERFACE
export X-list and `CMETA_PLUGIN_DECLARE`, it invokes the generated
`sidlc_..._DECLARE_PROVIDER_PLUGIN(name,(1,0,0),binding)` exactly once.
The generated `PROVIDER_EXPORT_ID` is the canonical qualified Component
identity; ServiceHost uses it to admit the real DSO under an existing Plugin
lease. No candidate SDK target is installed, and no loader/registry/GC or
automatic ownership is added. Multiple provider TUs must use distinct plugin
DSOs; a Plugin query is defined once per DSO.


## P2.2 negative provider admission (stacked, opt-in)

The separate negative admission host dynamically loads the valid IDL provider
and three **real** malformed shared libraries that present (a) a mismatched
original Salts Plugin Component contract ID, (b) a mismatched exact
SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION, or (c) a well-formed Plugin
FUNCTION export under the Component's canonical export ID instead of the
required Provider INTERFACE. A fourth negative source selects a missing export
from the original good DSO.

On each failed N+1 build, the original Salts::ComponentPlugin runtime must
reject publication, release the attempted Plugin lease exactly once, and
preserve an already published N plus its stable, borrowed service scope.
Explicit close/drain must return BUSY before scope release; final stop,
quiescence and unload are performed by the **existing** Salts::Plugin
registry. No second loader, retry, provider ABI or default IDL artifact path.

## P2.3 independent Linux sanitizer qualification

The feature/component-idl-sanitizer stacked branch executes all five candidate CTests, including actual valid and rejected Provider DSOs, five times under ASan+UBSan and TSan. It builds independent instrumented graphs using the existing Linux CI CMake preset and CTest, without modifying SaltsUtils runtime or shipping another artifact.

Coverage boundary: this instruments SaltsUtils candidate source/Host/DSOs and first-party SaltsUtils dependencies compiled here, but consumes prebuilt Salts.Native 2.3.0-rc.14. It is NOT a full-source Salts/Plugin TSan proof; that needs separate Salts source instrumentation before downstream runtime migration.
