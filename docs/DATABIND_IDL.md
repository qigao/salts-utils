# DataBind IDL

DataBind is the canonical transport-neutral typed IDL and binding compiler in SaltsUtils.

## Ownership

- **DataBind** owns data, service, channel and component contracts, schema validation, canonical compiler IR and binding plans.
- **CMeta** owns native C type, function and interface semantics.
- **CFlow** owns typed graph normalization, optimization and execution.
- **TurboFlow** owns product assembly, provider lifecycle, durability and settlement.
- **TBE** is a DataBind binary format/backend, not the umbrella schema/compiler architecture.

## Canonical pipeline

```text
DataBind IDL
    |
    v
databindc
    |
    +-- canonical reflection / binding IR
    +-- generated native bindings
    +-- selected projection backends
    |
    v
CMeta native semantics
    |
    v
CFlow execution consumers
    |
    v
TurboFlow product/runtime consumers
```

The semantic vocabulary is:

```text
Data
  message / enum / union

Interaction
  service / channel

Composition
  component
```

Artifact projections such as NATIVE, PLUGIN, WASM, OPENAPI and MOCK are compiler/build selections rather than new IDL languages.

Transport runtimes such as CHTTP, CRPC, Flowie, FlowMQ and CNet retain connection/session/protocol ownership. DataBind compiles contracts and bindings; it does not become a network framework.

The canonical architecture decision is tracked by salts-utils issue #141.
