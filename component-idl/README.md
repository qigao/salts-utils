# Component IDL — isolated integration module (design baseline)

Tracking: [SaltsUtils #620](https://github.com/qigao/salts-utils/issues/620)
Core runtime/naming contract: [Salts #1153](https://github.com/qigao/salts/issues/1153)

> This directory records the new module boundary. It is **not a shipped compiler,
> public CMake target, Component runtime, or working API yet**. Do not wire it
> into the default build until an executable static/DSO vertical slice exists.
> Development branch: `feature/component-idl-module` — DO NOT MERGE / PUBLISH
> without the gates below.

## Purpose

Give a business DLL author an IDL-first route to a typed CMeta interface and
Component binding, while keeping the current Salts Component, ComponentPlugin
(future Configurator/ServiceHost), and Plugin implementations unchanged during
development. Migrate consumers one at a time only after qualification.

The new module is a **compile-time artifact/projection backend** that consumes
the existing, canonical typed `IdlContract` and `NativeSourceIR` from
`salts-idlc`. It must not parse IDL again, fork the Service/Component IR, or
invent a second runtime.

```text
existing Salts::IDL frontend -> typed IdlContract / NativeSourceIR
                                     |
                                     v
                          component-idl codegen
                                     |
                   +-----------------+-----------------+
                   |                                   |
              static binding                     dynamic binding
              typed Interface                    typed Interface
              ComponentDesc                      ComponentDesc
              ProviderBinding                    ProviderBinding + Plugin export
                   |                                   |
        current Salts Component               current ComponentPlugin + Plugin
         (future Configurator)             (future ServiceHost + Plugin)
```

## Responsibility and dependency boundary

- **IDL** owns logical Component and Service contracts and typed Contract IR.
- **CMeta** owns native Interface/Type/Data/FunctionAbi semantics and exact
  generated typed projection functions.
- **Generated native adapter** owns exact callback glue and an explicit
  `salts_component_provider_binding`. Neither Reflection nor descriptor
  equality gives executable authority.
- **Configurator** (currently `Salts::Component`) owns instance selection,
  dependency resolution, create/activate, reverse shutdown, and rollback.
- **ServiceHost** (currently `Salts::ComponentPlugin`) owns publication,
  generation scopes, in-flight admission and drain.
- **Plugin** owns native DSO registry, actual loader/unload, leases and
  quiescence; a provider artifact must not link the host-only loader.
- **DataBind** owns contract-to-native binding, native value lifecycle, and
  failure-atomic staging, not the module loader or generation lifecycle.

Static Component generation must not require Plugin. Dynamic generation must
reuse the existing Plugin API and module lease authority.

## Incremental work (no replacement of legacy modules yet)

1. **Independent codegen/backend** — inspect real typed IR and existing
   `salts-idlc` backend interfaces; add actual module sources, CMake target,
   and tests without changing existing `ARTIFACTS` defaults.
2. **Static integration** — one IDL Service/Component and ordinary C
   implementation generate canonical CMeta declarations, exact provider
   binding and a working static Component consumer (no Plugin dependency).
3. **Dynamic integration** — the same contract generates an admitted Plugin
   provider DLL via the existing Plugin export machinery; validate a true
   dynamically loaded DSO, not a linked-in stand-in.
4. **Typed usage and RAII** — generate exact Interface projections and optional
   lexical no-fail cleanup guards; keep fallible drain/unload explicit. A live
   ServiceHost scope is noncopyable/address-stable and cannot escape its
   protecting owner.
5. **Isolated downstream migrations** — installed C11/C++17 + host CI first,
   then CHttp, TurboAgent, Flowie, and other actual consumers in separate PRs.
   Coordinate the #1153 public naming cutover only at its own ABI/release gate.

## First-slice acceptance

- The only business-authored files are an IDL contract and a C implementation
  (plus explicit build/deployment configuration).
- Static mode resolves, invokes and tears down a typed service, and rejects
  missing/ambiguous/cyclic dependencies and create/activate failures.
- Dynamic mode loads an actual provider DSO, validates canonical
  Component/Interface/Function ABI, publishes a complete generation, protects
  an existing scope through N -> N+1, observes BUSY before scope release, and
  drains/unloads safely after all provider-owned callbacks and objects end.
- Ownership is bounded and single-authority; no shadow registries,
  automatic retry, fallback adapters, or raw runtime-state persistence.
- Existing `component/`, `component-plugin/`, Plugin runtime, old IDL
  artifact selection, public SDKs, and downstream applications are unchanged
  until their separate qualification and migration PRs.
- Build/CTest use existing CMake presets, including C11/C++17 installed
  consumers, Linux/macOS/Windows and applicable sanitizer coverage.

Do not claim the codegen module or its conformance is implemented based on this
design-only checkpoint. Actual implementation and test outcomes are tracked in
#620.
