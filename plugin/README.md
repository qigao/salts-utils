# Salts Plugin

`Salts::Plugin` is the CMeta-based plugin contract layer owned by SaltsUtils.
It defines the portable manifest/export ABI, validates semantic contracts, and
provides the bounded POSIX/Windows dynamic loader registry plus explicit
lease-based lifecycle/quiescent unload. Optional CFlow integration is exported
separately as `Salts::PluginCFlow`.

## Boundary

```text
Salts::CMeta
  Type Identity / Interface / Callable
             |
             v
       Salts::Plugin
  manifest / export ABI
  semantic admission
  bounded loader / registry

optional adapter:
Salts::PluginCFlow
  lease-owned Publisher / Executor / Scheduler bindings
```

Plugin discovery, version policy and unload semantics stay out of CMeta/CFlow.

## Entry point

Every dynamic plugin exports exactly one well-known query symbol:

```c
SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return NULL;
    return &manifest;
}
```

The fixed symbol name is `SALTS_PLUGIN_QUERY_SYMBOL`, currently
`"salts_plugin_query"`. `SALTS_PLUGIN_QUERY_EXPORT` combines platform
visibility/export with C linkage, so the symbol remains unmangled when a plugin
implementation is compiled as C++.

The query returns borrowed immutable metadata. The host must keep the DSO loaded
while it reads the manifest, export rows, CMeta descriptors/interface values, or
callables.

### Salts/CMeta ABI prerequisite

Plugin ABI V1 does not replace the CMeta ABI contract. In particular,
`cmeta_callable` contains the finite CMeta signature universe, and changing the
configured callable type/relation lists changes that ABI. Host and plugin must
therefore be compiled against the same compatible installed Salts/CMeta SDK
profile and callable universe. A Plugin ABI version match is not permission to
mix unrelated CMeta builds or per-plugin type-universe overrides.

## Semantic identity

Addresses are representation facts, not plugin identities:

```text
descriptor pointer != semantic type/interface identity
vtable pointer      != interface identity
DSO load address    != plugin identity
```

Each export therefore carries:

```text
export_id                 unique inside one plugin
contract_id               stable cross-DSO semantic identity
contract_version          exact V1 contract version
capabilities              finite positive capabilities
CMeta representation      Interface or Callable
```

A host and plugin can compile the same `CMETA_INTERFACE(...)` declaration into
different translation units/DSOs and receive different descriptor addresses.
`salts_plugin_interface_desc_equal()` compares the CMeta metadata that exists
today (interface/method names, return spellings and arity) by content. CMeta's
current interface descriptor does not encode parameter type spellings, so this
comparison is intentionally only a structural consistency check.

The authoritative cross-DSO contract is `contract_id + contract_version`.
**Every ABI-significant change, including a parameter type change, must advance
`contract_version`.** `salts_plugin_export_contract_equal()` requires that
declared identity first and then applies the available CMeta structural check;
it never treats descriptor/vtable/function addresses as identity.

## Interface export

A domain header owns the interface exactly once:

```c
#define IMAGE_CODEC_METHODS(X, I) \
    X(I, R1, bool, probe, const void *, bytes)

CMETA_INTERFACE(ImageCodec, IMAGE_CODEC_METHODS);
```

A plugin implementation uses the normal CMeta interface protocol:

```c
CMETA_IMPLEMENTS(ImageCodec, png_codec, IMAGE_CODEC_CAN_DECODE,
    .probe = png_probe);
```

The plugin export row carries the semantic contract ID plus a borrowed CMeta
interface descriptor and mutable interface handle. The row/descriptor metadata
remain immutable, while the handle is intentionally mutable because ordinary
CMeta interface methods (including an owning `D0` destructor) may mutate state
or invalidate the handle. After Plugin admission, the typed host still uses
`ImageCodec_valid()` before normal typed dispatch. Plugin does not invent a
second vtable or method metadata system.

## Callable export

Algorithms can be exported directly as existing `cmeta_callable` values:

```c
typed_any(value, int, normalize_score, (int value)) {
    return value < 0 ? 0 : value;
}
```

The export points at that callable. Callable compatibility uses the bound CMeta
signature, effects and properties. Dispatch mode, function/adapter target
addresses and capture bytes are implementation representation, not the semantic
contract. This permits, for example, an adapter-backed implementation to satisfy
the same declared callable contract as a canonical raw implementation.

## ABI admission

V1 is exact and fail-fast:

- `SALTS_PLUGIN_ABI_VERSION == 1`;
- manifest and export rows carry `struct_size` and `abi_version`;
- V1 size constants mark the last readable V1 field rather than aliasing a
  future `sizeof(struct)`, so later tail extensions cannot silently redefine
  the V1 prefix;
- IDs are non-empty bounded strings;
- export count is bounded by `SALTS_PLUGIN_MAX_EXPORTS`;
- interface method metadata is bounded and validated;
- duplicate export IDs are rejected;
- callable contracts must bind and validate through CMeta;
- unsupported ABI is distinct from malformed metadata;
- there is no compatibility shim or silent fallback.

The generic admission helpers `salts_plugin_export_require_interface()` and
`salts_plugin_export_require_callable()` return
`SALTS_PLUGIN_INCOMPATIBLE_CONTRACT` for contract ID/version, capability, or
available CMeta-structure mismatches. They do not execute plugin lifecycle
callbacks.

The loader/registry will add duplicate plugin-ID and resource ownership checks
without changing these semantic rows.

## Lifecycle fields

The manifest reserves explicit `start`, `request_stop`, `is_quiescent` and
`destroy` callbacks. Query and lifecycle callbacks share the explicit
`SALTS_PLUGIN_CALL` calling convention across the DSO boundary. ABI validation
never invokes them. Their ordering,
generation-safe handles, in-flight accounting and quiescent unload are owned by
the lifecycle work, not by this contract validator.


## Bounded loader and registry

The loader/registry layer keeps platform library handles private and publishes
only generation-bearing `salts_plugin_ref` values.

```text
borrowed UTF-8 path
      ↓
platform open
      ↓
resolve exact salts_plugin_query
      ↓
query(SALTS_PLUGIN_ABI_VERSION)
      ↓
manifest validation
      ↓
duplicate plugin_id check
      ↓
publish bounded slot + generation
```

`salts_plugin_registry_init()` allocates the fixed slot table once. Load never
grows or replaces it, and lookup is intentionally bounded O(capacity) rather
than hiding a second dynamically growing registry. Registry control-plane calls
are externally serialized in V1; the module creates no worker thread and does
no background polling.

Paths are borrowed only for one load call, must be non-empty strict UTF-8, and
are never retained. POSIX uses `dlopen(..., RTLD_NOW | RTLD_LOCAL)`; Windows
converts UTF-8 to UTF-16 and uses `LoadLibraryW`. `dlopen` handles,
`HMODULE`, `FARPROC`, loader flags, and native error objects never enter the
public ABI.

Admission is transactional: failures before publication close the newly opened
library and leave registry count/refs unchanged. Missing files, missing query
symbols, query rejection, unsupported ABI, duplicate plugin IDs, capacity
exhaustion, and invalid arguments stay distinguishable.

## Lifecycle and quiescent unload

Plugin manifests use one of two exact lifecycle forms:

```text
passive:
  self = NULL
  start/request_stop/is_quiescent/destroy = NULL

managed:
  self != NULL
  all four callbacks are present
```

Partial lifecycle groups are rejected during manifest admission.

The host-visible state machine is:

```text
LOADED
  ↓ start
STARTING
  ↓
STARTED
  ↓ request_stop (closes new lease admission first)
STOPPING
  ↓ plugin quiescent + host leases == 0
QUIESCENT
  ↓ unload
STALE ref
```

A failed `start` is required to be failure-atomic. The registry records the
first failure and moves directly to `QUIESCENT`, so destroy/unload remains
possible. A failed `request_stop` never reopens admission: the state remains
`STOPPING`, the first failure remains observable in
`salts_plugin_lifecycle_info.failure`, and the host may continue polling until
the plugin becomes quiescent.

### Lease rule

Plugin-owned manifest/export/interface/callable pointers may be dereferenced
only while holding a live `salts_plugin_lease` returned by
`salts_plugin_registry_acquire()`. The lease must remain live across every
callback into plugin code and is returned with
`salts_plugin_registry_release()`.

`request_stop()` makes future acquire attempts fail before it invokes the
plugin callback. Existing leases may drain normally. `unload()` returns
`SALTS_PLUGIN_BUSY` while any lease or lifecycle callback is in flight.

Each plugin slot has a bounded lease table. Both plugin refs and leases carry
generations. Successful unload increments the slot generation; reusing the same
slot therefore cannot make an old ref or lease target a newly loaded DSO.

### Destroy and unload ordering

A managed plugin is unloadable only if it was never started (`LOADED`) or has
reached `QUIESCENT`. The registry invokes `destroy` at most once, outside the
registry mutex, while the DSO is still loaded; only after that does it call
`dlclose`/`FreeLibrary`. If the native close fails, the slot remains present
for an explicit retry and the already-called destroy callback is not repeated.

There is no force unload, retry loop, hidden worker, or background quiescence
polling.


## PluginCFlow adapter

`Salts::PluginCFlow` is intentionally separate from the base Plugin target:

```text
Salts::Plugin
    -> Salts::CMeta

Salts::PluginCFlow
    -> Salts::Plugin
    -> Salts::CFlow
```

The adapter recognizes three stable semantic contracts at version 1:
`salts.cflow.publisher`, `salts.cflow.executor`, and
`salts.cflow.scheduler`.

A binding acquires one ordinary Plugin lease, verifies the export contract
against the corresponding CMeta-generated CFlow interface descriptor, verifies
that the export capability bits exactly match the provider vtable capabilities,
and then exposes the plugin-owned typed interface pointer.

The provider remains borrowed. The host must not invoke its owning `D0
destroy()`, copy/move it into another owner, or retain it after binding release.
The plugin lifecycle remains responsible for provider shutdown and destruction.
This avoids double ownership while still making the Plugin lease the DSO-safety
boundary.

A live binding prevents unload. `request_stop()` may close new Plugin
admission, but already acquired bindings remain valid so accepted CFlow work can
drain according to the provider's own contract. After bindings are released,
the plugin's `is_quiescent()` decides when unload is safe. PluginCFlow creates
no proxy runtime, Subscription owner, Executor, Event type, thread, or Graph
DSL.
