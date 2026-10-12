# Component IDL — isolated static authoring candidate

Tracking: [SaltsUtils #621](https://github.com/qigao/salts-utils/issues/621), parent [#620](https://github.com/qigao/salts-utils/issues/620), runtime owner [Salts #1154](https://github.com/qigao/salts/issues/1154).

This is a compile-time-only adapter over canonical `IdlContract`, not another parser, DI runtime, Plugin loader or service registry. The actual candidate Configurator/ServiceHost is Salts-owned (Draft #1155). This module is OFF by default, uninstalled/unexported, and does not modify existing `salts-idlc` NATIVE/PLUGIN/WASM/TYPES outputs.

P1 explicitly admits Components containing 1–16 Services, each with 1–16 sync Request → Response operations, with required scalar `uint32` records and no typed errors or ownership transfers. It generates CMeta typed Interface declarations, the original immutable qualified Component ID and a native `salts_component_provider_binding` helper. All executable create/activate/deactivate and borrowed Interface projection authority remain explicit inputs from business C code. The first slice uses static native R2 Interface methods (no invented dynamic FunctionAbi), no DSO admission and no fallbacks. Unsupported shapes must fail before any output.

Generated types come from the **existing** `salts_idl_target(ARTIFACTS TYPES)` frontend. The P1 fixture calls the canonical Salts::IDL parser rather than implementing a second language. Once this path is qualified, #620 can extend exact reflected execution and real Plugin Provider export in a separate increment.

**DO NOT MERGE / DO NOT PUBLISH** before executable CI evidence.
