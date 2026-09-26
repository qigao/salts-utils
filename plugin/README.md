# Salts Plugin

`Salts::Plugin` is the SaltsUtils-owned dynamic-plugin publication and lifecycle
runtime built on CMeta semantics.

It intentionally has **one current ABI only**. There are no parallel legacy/current ABI generations,
ABI negotiation, readable-prefix compatibility, or fallback to an older
manifest. Plugins built against an obsolete ABI must be rebuilt.

## Current semantic baseline

The single current Plugin ABI requires **Salts.Native 1.7.7** within
the same declared package contract. This baseline includes the canonical CMeta Function/FunctionAbi and Interface reflection/equality used by Plugin admission, plus the canonical reflected Function-to-CFlow projection consumed by the current semantic stack.

Plugin does not carry private compatibility copies of those semantics.

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

## Publication-only target

Generated plugin DSOs link the publication contract only:

```text
Salts::PluginABI
    -> <salts/plugin.h>
    -> Salts::CMeta

Salts::Plugin
    -> Salts::PluginABI
    -> loader / registry / lifecycle
```

`Salts::PluginABI` is an installed INTERFACE target. It deliberately carries
no dynamic-loader or registry implementation. DataBind-generated Service
plugins therefore publish FunctionMeta/FunctionAbi and
`salts_plugin_query()` without linking host runtime machinery.

Host applications continue to link `Salts::Plugin`.

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

CMeta owns native type/function/interface semantics, including canonical
Interface validation and semantic equality. Plugin does not maintain a second
Interface descriptor validator or comparator.

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
    void *const *params,
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

## DataBind public projection frontend

DataBind artifact generation uses the same `databindc` frontend as ordinary
native source generation.

Example:

```text
databindc image.schema \
  --lang c \
  --output out/image_native.h \
  --artifacts plugin \
  --component Image.ImageProcessor \
  --artifact-name image_processor \
  --artifact-version 1.0.0
```

The source-language and artifact dimensions remain orthogonal:

```text
--lang c
    -> native/source rendering

--artifacts plugin
    -> artifact backend selection
```

For the command above, PLUGIN outputs are derived deterministically next to the
ordinary `--output` header:

```text
out/image_native.h
out/image_processor.plugin.h
out/image_processor.plugin.c
out/image_processor.plugin_client.h
out/image_processor.plugin_client.c
```

`--component` takes the canonical qualified Component identity and becomes
the Plugin artifact identity through `Component.qualified_name`.

There is no `--plugin-output`, no Plugin-specific parser frontend, and no
schema-wide fallback when Component selection is missing.

## CMake typed generation frontend

Installed SaltsUtils exposes the same typed generation frontend through
`databind_target()`:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)

databind_target(
    TARGET image
    IDL "${CMAKE_CURRENT_SOURCE_DIR}/image.schema"
    COMPONENT Image.ImageProcessor
    VERSION 1.0.0
    ARTIFACTS PLUGIN
    SOURCES image.c)
```

The helper creates:

```text
image
    logical umbrella target

image_plugin
    generated shared-library Plugin provider

image_plugin_client
    generated host-side typed client library
```

The generated source/header path lives under the target build directory and the
helper invokes the same public `databindc --artifacts plugin` CLI. It does
not contain a second parser or Plugin generator.

`ARTIFACT_NAME` may override the artifact basename without changing the CMake
logical target name.

For native host builds the helper prefers the `databindc` installed beside
the same SaltsUtils package and launches it with the package-local SaltsUtils
runtime plus the current host `SALTS_ROOT`.

During cross compilation, target-platform tools and target-platform Salts
libraries are never executed implicitly. Provide both the host compiler and its
matching host Salts SDK explicitly:

```cmake
-DSaltsUtils_DATABINDC_EXECUTABLE=/path/to/host/databindc
-DSaltsUtils_DATABINDC_HOST_SALTS_ROOT=/path/to/host/salts-sdk
```

The generated target therefore never relies on an Android/target `SALTS_ROOT`
to launch a host compiler.

## Generated typed Plugin client

The PLUGIN projection generates a separate host client in addition to the
provider DSO. Opening the client acquires one Plugin lease, verifies the
Component/plugin identity, Service/export identities, and CMeta
FunctionDesc/FunctionAbi equality, then caches the admitted exports.

Repeated typed calls reuse that lease and cache. They return Plugin bridge
status separately from the native business integer status. Closing the client
releases the lease; unload remains busy while the client is open.

Provider and client code are separate targets, so host loader/registry runtime
does not leak back into the provider DSO.

## DataBind and CFlow boundaries

`Salts::Plugin` depends on CMeta but not DataBind or CFlow.

DataBind generates Plugin publication glue through `ARTIFACTS PLUGIN`.

CFlow integration remains a separate optional target:

```text
Salts::PluginCFlow
    PUBLIC Salts::Plugin Salts::CFlow
```

The same DataBind component may also project to WASM without changing its
logical contract.
