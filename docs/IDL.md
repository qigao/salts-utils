# SaltsUtils IDL

SaltsUtils 4.1 separates contract definition, data shape, binding, format, and
transport projection.

## Canonical ownership

```text
IDL      = logical contract truth
Schema   = Data-only shape truth
DataBind = logical/native binding truth
CMeta    = native C semantic truth            (Salts)
CSerde   = canonical token truth              (Salts)
CFlow    = execution truth                    (Salts)
Plugin   = module / loader / lease lifecycle  (Salts)
```

The retired model is:

```text
DataBind == IDL
```

There is no compatibility alias for that architecture.

## Contract vocabulary

```text
IDL
├── Data
│   ├── message
│   ├── enum
│   └── union
├── Interaction
│   ├── service
│   └── channel
└── Composition
    └── component
```

The public frontend is `Salts::IDL`. Parser nodes are frontend-private; the
compiler freezes one owned typed `IdlContract` before any projection runs.

`Salts::Schema` owns only Data-shape -> CMeta projection. Service, Channel and
Component do not belong to Schema.

## Compiler pipeline

```text
IDL source
  -> salts-idlc
  -> typed IdlContract
       |
       +-> DataBind binding compiler -> native binding inputs
       +-> format compiler           -> FormatPlan
       +-> transport compiler        -> TransportPlan
       +-> artifact compiler         -> Native / Plugin / WASM / OpenAPI / Mock
```

The CMake frontend is `cmake/IDL.cmake`.

The old `databindc` compiler identity is removed without alias.

## Binding compilation

Final `DataBindBindingPlan` is deliberately compiled by the DataBind runtime,
not serialized as a fake static compiler artifact. Its compilation joins:

- logical DataBind reflection;
- a compile-time projection adapter;
- generated canonical native bindings;
- current CMeta Function/Data descriptors.

The build-time binding compiler therefore owns only immutable native-binding
inputs such as presence/null maps and typed-error binding metadata. It does not
own transport state, FunctionMeta identity, Plugin lifecycle, or format
semantics.

## Format boundary

Binary is one format compiler/backend, not the IDL.

```text
IdlContract
   +
databind_binary_format_plan
   -> BinaryLayoutIR / BinaryLayoutPlan
   -> binary / socket / FlowMQ lowering
```

Binary field ordering, fixed/group/variable-data admission, wire sizes,
cursor metadata, reader names and C codegen profiles are computed by the Binary
format pass, not by the IDL frontend.

Concrete JSON/XML/YAML/CSV parsers own syntax and expose CSerde contracts.
DataBind consumes CSerde; it does not duplicate format parsers.

## Transport / artifact boundary

Transport and artifact axes remain orthogonal:

```text
TRANSPORT: HTTP | RPC | SOCKET | FLOWMQ | MQTT | WEBSOCKET
ARTIFACT:  NATIVE | PLUGIN | WASM | OPENAPI | MOCK
```

Transport runtimes keep sessions, queues, reconnect, TLS, routing and
backpressure. Plugin publication consumes the Salts-owned Plugin ABI; it does
not define another Service model.

### NATIVE Service artifact

`ARTIFACTS NATIVE` publishes the generated Service metadata and executable
adapters on the generated native target. Business implementations remain normal
native target inputs: a build may attach them to the generated NATIVE target
with ordinary CMake target composition, while Plugin keeps its dedicated
`SOURCES/LIBRARIES` convenience contract.

One NATIVE Service artifact publishes the canonical operation identity and
execution capability together:

```text
FunctionDesc / FunctionAbi
DataBindServiceNativeBinding
DataBindNativeExecution
generated Request -> Response CFlow typed projection
```

The CFlow projection reuses the same FunctionDesc/FunctionAbi and exact native
execution descriptor. It does not generate a second unary reflected function,
and typed-error/async shapes that are not admitted fail closed.

## Public targets

```text
Salts::IDL
Salts::Schema
Salts::DataBind
Salts::BindingsCpp
Salts::Lua       # optional
Salts::QuickJS   # optional
```

`Salts::PluginABI` and `Salts::Plugin` come from Salts 1.8.

There is no `Salts::DataBindSchema`, `Salts::CBind`,
`Salts::PluginCFlow`, or `Salts::PluginCFlowABI` compatibility target.
