# IDL Component adapter — design-only checkpoint

**Ownership correction:** The new runtime is created in **[Salts #1154](https://github.com/qigao/salts/issues/1154)** on `feature/component-runtime-isolated`, as a copy and migration of existing `component/` and `component-plugin/` source. See [Salts Draft PR #1155](https://github.com/qigao/salts/pull/1155).

This SaltsUtils branch/directory is *not* the new Component Runtime, must not become a second DI container, Configurator, Plugin loader, ServiceHost or parser, and must **not be merged** as such. It contains only a historical exploratory design note, before the corrected module ownership decision.

Current tracking is [SaltsUtils #620](https://github.com/qigao/salts-utils/issues/620), **the follow-on typed IDL adapter** against a qualified Salts runtime ABI. Work remains gated on real static/DSO C11/C++17 conformance and installed SDK qualification in Salts.

## Adapter boundary (after the Salts runtime is qualified)

```text
existing salts-idlc Contract IR / NativeSourceIR
    |
    +--> generated CMeta Interface / ComponentDesc
    +--> generated exact native Provider Binding
    +--> optional existing Plugin export (real DLL)
    '--> generated typed caller projection

               consumes, never re-implements
               Salts::Configurator/ServiceHost/Plugin
```

The current SaltsUtils `ARTIFACTS NATIVE/PLUGIN` path is unchanged. Any new output selection must be opt-in, reuse the existing code generators and compiler pipeline, and introduce no runtime owner or fallback.

Final migration is one consumer per reviewed/qualified PR; preserve existing behavior until that migration.
