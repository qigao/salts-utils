# Salts Plugin

`Salts::Plugin` is the SaltsUtils-owned dynamic-plugin publication and lifecycle
runtime built on CMeta semantics.

It intentionally has **one current ABI only**. There are no parallel legacy/current ABI generations,
ABI negotiation, readable-prefix compatibility, or fallback to an older
manifest. Plugins built against an obsolete ABI must be rebuilt.

## Architecture

```text
                     Salts::CMeta
       Type / Interface / FunctionMeta / FunctionAbi
                           |
                           v
                    Salts::Plugin
          manifest / loader / registry / lease
              lifecycle / quiescent unload
                    /                 \
                   v                   v
          Function export       Interface export
          Service operation     Provider capability
                   \                   /
                    +--------+---------+
                             |
                             v
                 optional Salts::PluginCFlow
```

Plugin discovery/loading/lifecycle stay out of CMeta and CFlow.

## One query symbol, one ABI

Every dynamic plugin exports:

```c
SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return NULL;
    return &manifest;
}
```

The host calls the query exactly once with the one supported
`SALTS_PLUGIN_ABI_VERSION`.

It does not retry an older number.

The returned manifest must satisfy:

```text
manifest.abi_version == SALTS_PLUGIN_ABI_VERSION
manifest.struct_size == SALTS_PLUGIN_MANIFEST_SIZE

each export.struct_size == SALTS_PLUGIN_EXPORT_SIZE
```

`struct_size` is an exact-layout guard, not a compatibility prefix.

## Semantic identity

Pointer addresses are representation facts:

```text
descriptor pointer != semantic identity
adapter pointer    != semantic identity
vtable pointer     != semantic identity
DSO address        != plugin identity
```

Plugin contract admission uses stable domain identity:

```text
plugin_id
export_id
contract_id + contract_version
capabilities
```

CMeta owns native type/function semantics.

## Function export

Operation-oriented capabilities use the Function representation:

```text
cmeta_function_desc
+
cmeta_function_abi_desc
+
{ context, typed exact invoke }
```

`cmeta_function_desc` answers what the native function means.

`cmeta_function_abi_desc` answers how its declaration crosses the C call
boundary.

The exact adapter is generated from the same declaration/code-generation source
as the reflected function:

```c
typedef bool (SALTS_PLUGIN_CALL *salts_plugin_function_invoke_fn)(
    void *context,
    void *return_storage,
    const void *const *params,
    size_t param_count);
```

It is **not** libffi and does not reconstruct arbitrary C calls from runtime
metadata.

Plugin admission rejects:

- invalid FunctionMeta;
- invalid FunctionAbi;
- `CMETA_ABI_UNSPECIFIED` carriers;
- a FunctionAbi that points at a different FunctionMeta;
- a missing exact adapter.

The standard DataBind Service path is therefore:

```text
DataBind Service operation
        ↓
generated native function
        ↓
FunctionMeta + FunctionAbi
        ↓
generated exact adapter
        ↓
Plugin Function export
```

Services are not converted into generated vtables merely because they are
packaged in a DLL/SO.

## Interface export

Long-lived stateful/polymorphic providers retain the CMeta Interface shape:

```text
cmeta_interface_desc
+
{ self, vtable }
```

Typical examples include Executor, Scheduler, Publisher, Clock, Device and
StorageProvider.

Plugin does not invent a second interface/vtable system.

## No generic Callable publication

`cmeta_callable` remains an execution representation used by CFlow and other
admitted execution layers.

It is no longer a Plugin publication shape.

Plugin exports the original semantic capability:

```text
Service operation -> Function
Provider object    -> Interface
```

An eligible Function may later be projected into `cmeta_callable` by CFlow,
but that happens downstream of Plugin publication.

## Lifecycle

Plugin manifests are either:

```text
passive:
  self = NULL
  start/request_stop/is_quiescent/destroy = NULL

managed:
  self != NULL
  all four callbacks are present
```

The registry state machine is:

```text
LOADED
  ↓ start
STARTING
  ↓
STARTED
  ↓ request_stop
STOPPING
  ↓ plugin quiescent + host leases == 0
QUIESCENT
  ↓ unload
STALE ref
```

The loader/registry remains bounded, generation-safe and explicit. There is no
force-unload fallback or hidden polling thread.

## Lease rule

Everything borrowed from the plugin DSO is valid only while a live
`salts_plugin_lease` keeps that DSO loaded.

That includes:

- manifest/export rows and tagged capability payloads;
- FunctionMeta / FunctionAbi;
- TypeDesc / DataDesc;
- custom type-trait callbacks;
- exact Function adapter code;
- Interface self/vtable.

For a generated Service operation the safe borrow spans the whole native
operation lifetime:

```text
acquire lease
    ↓
bind/decode input
    ↓
invoke exact adapter
    ↓
encode/write output
    ↓
destroy/rollback native staging/result
    ↓
release lease
```

Do not release the lease immediately after the native function returns if
egress or cleanup can still call plugin-owned CMeta traits.

A lease is a DSO/resource borrow. A generated long-lived Service handle may keep
one lease across many calls; the bounded lease table is not intended to be a
request-concurrency limit.

## Registry

The registry loads:

```text
UTF-8 path
    ↓
platform open
    ↓
resolve salts_plugin_query
    ↓
query(SALTS_PLUGIN_ABI_VERSION)
    ↓
exact current-ABI validation
    ↓
duplicate plugin_id check
    ↓
publish slot + generation
```

POSIX/Windows native handles stay private.

Admission failures are transactional: the newly opened DSO is closed and the
registry remains unchanged.

## DataBind and CFlow boundaries

`Salts::Plugin` depends on CMeta but not DataBind or CFlow.

DataBind generates Plugin publication glue through `PROJECTIONS PLUGIN`.

CFlow integration remains a separate optional target:

```text
Salts::PluginCFlow
    PUBLIC Salts::Plugin Salts::CFlow
```

The same DataBind component may also project to WASM without changing its
logical contract.
