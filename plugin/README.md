# Salts Plugin

`Salts::Plugin` is the CMeta-based plugin contract layer owned by SaltsUtils.
It defines the portable manifest/export ABI and validates semantic contracts. It
does **not** load shared libraries yet; POSIX/Windows loading and the bounded
registry are tracked separately.

## Boundary

```text
Salts::CMeta
  Type Identity / Interface / Callable
             |
             v
       Salts::Plugin
  manifest / export ABI
  semantic admission
             |
             v
   future loader / registry

optional later:
Salts::PluginCFlow
  Publisher / Executor / Scheduler / Event integration
```

Plugin discovery, version policy and unload semantics stay out of CMeta/CFlow.

## Entry point

Every dynamic plugin exports exactly one well-known query symbol:

```c
SALTS_PLUGIN_ENTRY
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return NULL;
    return &manifest;
}
```

The fixed symbol name is `SALTS_PLUGIN_QUERY_SYMBOL`, currently
`"salts_plugin_query"`.

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

The plugin export row carries the semantic contract ID plus borrowed CMeta
interface descriptor/value. After Plugin admission, the typed host still uses
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
