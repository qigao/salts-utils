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
