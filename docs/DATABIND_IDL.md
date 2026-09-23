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


## Generic BindingPlan IR

The canonical compiled provider vocabulary is transport-neutral:

```text
VALUE
METADATA
PAYLOAD
PART
RESULT
ERROR
```

A projection adapter runs at compile/control time and maps a Service/Channel
field to one of those logical classes plus an opaque selector. Examples:

```text
HTTP path/query      -> VALUE
HTTP header/cookie   -> METADATA
HTTP body            -> PAYLOAD

MQTT topic/property  -> VALUE / METADATA
MQTT payload         -> PAYLOAD

FlowMQ multipart     -> PART / PAYLOAD
service response     -> RESULT
typed service error  -> ERROR
```

The immutable BindingPlan copies the projection result. Runtime providers only
consume `binding_class + selector`; they do not inspect DataBind schema AST,
HTTP/RPC enums, CHTTP types, FlowMQ sockets, MQTT session state, or TBE typed
descriptors.

Native binding is defined by CMeta `cmeta_function_desc` and
`cmeta_data_desc` plus DataBind-owned optional-presence metadata. TBE remains
a format backend and is not the native BindingPlan authority.
