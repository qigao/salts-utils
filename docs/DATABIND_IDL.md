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

DataBind generation is selected along **orthogonal axes** rather than one
format×transport×artifact backend namespace:

```text
FORMAT
  DataBindFormat / FormatPlan
  JSON | YAML | XML | CSV | BINARY | ...

TRANSPORT
  HTTP | RPC | SOCKET | FLOWMQ | MQTT | WEBSOCKET

ARTIFACT
  NATIVE | PLUGIN | WASM | OPENAPI | MOCK
```

The public compiler keeps one entry point. Artifact generation is selected with
`databindc --artifacts ...`; transport plan generation is selected with
`databindc --transports ...`. The CMake equivalent remains one
`databind_target()` call with independent `ARTIFACTS` and `TRANSPORTS`
arguments. Format choice stays in canonical `DataBindFormat` /
`FormatPlan` configuration; the compiler does not invent a second format enum
or a `HTTP_JSON`/ `FLOWMQ_BINARY` Cartesian backend identity.

Transport runtimes such as CHTTP, CRPC, Flowie, FlowMQ and CNet retain
connection/session/protocol ownership. DataBind compiles contracts and immutable
plans; it does not become a network framework. Artifact choices such as PLUGIN
or WASM do not become transport identities.

The canonical architecture decision is tracked by salts-utils issue #141 and
the composable plan implementation by #188.


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
