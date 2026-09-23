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


## Native egress through CSerde

DataBind owns the canonical native CMeta serialization boundary in both
directions:

```text
cserde_reader
    ↓
data_bind_native_decode()
    ↓
native CMeta storage

native CMeta storage
    ↓
data_bind_native_encode()
    ↓
cserde_writer
```

Transport and format integrations must consume these primitives rather than
implement another generic CMeta serializer. In particular, CHTTP, CRPC,
FlowMQ, MQTT/Flowie and future projection backends own framing/session/output
state but do not recursively reinterpret `cmeta_data_desc`.

`data_bind_native_encode()` validates the same canonical native graph as the
reader path before emitting the first token. It borrows and never mutates the
source value. The caller owns the CSerde writer, and DataBind intentionally
does not call `cserde_writer_finish()`.

Writer output is not inherently transactional. Atomic publication belongs to
the compiled BindingPlan provider boundary:

```text
begin_output
  -> write_output
       -> data_bind_native_encode(...)
  -> commit_output
or
  -> abort_output
```

This keeps format-neutral native semantics in DataBind while leaving HTTP/RPC/
messaging framing and transaction ownership in their respective providers.
