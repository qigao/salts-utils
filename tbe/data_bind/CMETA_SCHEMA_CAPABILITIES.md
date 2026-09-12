# DataBind / TBE schema to CMeta capability matrix

Status: incremental implementation for #45. This document is normative for the DataBind convergence work; implementation must not add a second DataBind-private type universe where CMeta already provides the structural/data semantic. Production schema/reflection graph migration remains incomplete.

## Ownership rule

Schema is the serializable-data subset of CMeta plus wire/schema semantics.

CMeta owns structural data identity and shape. DataBind/TBE owns schema names, external names and aliases, required/optional/wire presence, defaults and validation constraints, format annotations, compatibility rules, and schema fingerprinting.

Traits, callable/`typed_any`, interface/implements, Range, Collector, effect/property metadata and pointer identity are not schema field types.

## Current lowering matrix

`descriptor helper` means `schema_cmeta_builtin_data` resolves the listed names to an immutable, provider-owned descriptor; callers borrow it and must not free it. This status does **not** mean the production schema/reflection graph, generated binding, or dynamic storage path has migrated. `kind only` means classification is available but a concrete storage descriptor has not been selected: successful `schema_cmeta_data_kind` is not evidence of descriptor lowering.

| Schema / DataBind family | Canonical CMeta data semantic | Storage/type source | Status | Notes |
| --- | --- | --- | --- | --- |
| `bool` | `CMETA_DATA_BOOL` | `cmeta_data_bool` / `cmeta_type_bool` | descriptor helper | Reuses canonical CMeta boolean storage; no DataBind-private descriptor. |
| signed integers | `CMETA_DATA_SINT` | `salts_cmeta_fixed_width.h` exact-width descriptors | descriptor helper | Schema width lowers to the exact 8/16/32/64-bit descriptor; never use platform `long` for schema `int64`. |
| unsigned integers | `CMETA_DATA_UINT` | `salts_cmeta_fixed_width.h` exact-width descriptors | descriptor helper | Width is part of the native storage contract. |
| `float` / `f32`, `double` / `f64` | `CMETA_DATA_FLOAT` | `cmeta_data_float` / `cmeta_data_double` | descriptor helper | Aliases reuse the corresponding canonical CMeta storage type and 32/64-bit float shape. |
| `string` | `CMETA_DATA_STRING` | explicit CMeta type/shape/ops | internal buffer builder; production integration pending | Builtin descriptor lookup returns NULL. Ownership/borrowed lifetime must be selected explicitly by the storage adapter, not inferred from schema kind. |
| `bytes` | `CMETA_DATA_BYTES` | explicit CMeta type/shape/ops | internal buffer builder; production integration pending | Builtin descriptor lookup returns NULL; no implicit owning or borrowed storage selection. |
| `uuid` | custom canonical data descriptor | `salts_uuid_cmeta_data` | descriptor helper | Use Salts canonical UUID descriptor; do not manufacture a DataBind UUID descriptor. |
| message / record | `CMETA_DATA_STRUCT` | schema-lowered `cmeta_data_struct_shape` + native/dynamic storage descriptor | target | Structural fields come from CMeta; aliases/wire names remain schema overlay metadata. |
| enum / flags | `CMETA_DATA_ENUM` | CMeta enum metadata + enum storage adapter | target | Flags require explicit schema/wire semantics; structural enum identity belongs to CMeta. |
| union / oneof | `CMETA_DATA_VARIANT` | CMeta variant descriptor/adapter | gated | Enable only after the schema discriminator/wire representation is frozen. CMeta data-level Variant support exists even though higher-level CMeta DSL syntax may remain intentionally limited. |
| sequence (`list`, repeated, array-like semantic sequence) | `CMETA_DATA_SEQUENCE` | CMeta container descriptor + CSTL/container provider | target | Schema expresses sequence semantics, not concrete `Vec`/`List`/`Deque` storage. |
| `set` | `CMETA_DATA_SET` | CMeta container descriptor + CSTL/container provider | target | Concrete ordered/hash storage is a native profile concern. |
| `map<K,V>` | `CMETA_DATA_MAP` | CMeta container descriptor + CSTL/container provider | target | Key/value identities come from CMeta; schema owns admissible wire key representation. |
| optional / presence | CMeta generic/presence descriptor plus schema presence overlay | CMeta `Option` where storage uses it, or explicit native presence descriptor | target | Schema optionality is a wire/validation rule and must not be inferred solely from one storage layout. |
| Pair | CMeta generic Pair identity | CMeta generic descriptor | syntax gap | Add schema syntax only if a stable wire representation is useful; do not create a DataBind Pair kind. |
| Tuple | CMeta generic Tuple identity | CMeta generic descriptor | syntax gap | Fixed heterogeneous tuple needs canonical positional wire semantics first. |
| Result<T,E> | CMeta generic Result identity | CMeta generic descriptor | gated | Requires an explicit canonical wire representation before schema exposure. |
| datetime/date/time/duration | custom schema scalar over CMeta data/storage descriptor | Salts temporal storage descriptors when available | gap | Today these exist as DataBind value kinds. Convergence must define canonical CMeta descriptors instead of preserving permanent DataBind-only identities. |
| decimal/money/bigint | custom schema scalar over CMeta data/storage descriptor | canonical numeric/domain descriptor required | gap | Do not map these to platform integers/floats by approximation. |
| null | no standalone native storage type | n/a | not a standalone schema type | Null is a value/presence token; a concrete target type must define how it is represented. |

The scalar helper regression coverage is in `tbe/schema/test/test_schema_cmeta.c`: exact-width integer aliases and UUID, canonical bool storage, float/f32 and double/f64 storage and bit widths, descriptor/kind agreement, invalid names, and the string/bytes storage-selection boundary. These helper tests do not close the production migration gates below.

## Internal buffer lowering

`schema_cmeta_buffer_data` in `tbe/schema/src/schema_cmeta_buffer.h` is an internal, non-installed builder for #45. The caller supplies STRING/BYTES semantics and a complete storage type, buffer shape and adapter. CMeta validates semantic type identity, exact layout, callbacks and ownership agreement before the caller-owned descriptor is published. Invalid inputs and custom ownership return zero without changing the output. No new type/ownership enum, allocator, buffer callback or fallback is introduced.

The descriptor borrows all input metadata; names, type, shape and ops must remain immutable and outlive its use. Construction allocates no buffer and invokes no provider callback. Runtime storage remains governed by its provider:

| Explicit profile | Storage provider | Runtime lifetime |
| --- | --- | --- |
| STRING or BYTES, owned | `salts_tstr_cmeta_buffer_ops` | NULL zero state; assignment copies exact bytes, including NUL; restore frees and resets. |
| STRING or BYTES, borrowed | `salts_vstr_cmeta_buffer_ops` | `{NULL, 0}` zero state; assignment borrows without extending source lifetime; restore clears without freeing source bytes. |

CBind accepts borrowed input only under its stable-view contract; a transient view is rejected, never converted to owned storage. Buffer byte quotas and semantic-zero rollback remain CBind/CMeta responsibilities. A read view expires when its provider storage changes or is released; borrowed source bytes must remain alive throughout use.

`tbe/schema/test/test_schema_cmeta_buffer.c` exercises both semantics with both providers through real CBind decode, metadata-copy identity, invalid mappings, embedded NUL, empty values, byte limits, occupied destinations and owned-field rollback after borrowed-field failure. It runs within `test_schema_cmeta`; only that test target links CBind. The schema library's dependency/export closure is unchanged.

This is a descriptor-building and CBind-consumption foundation, not production schema graph integration, generated/runtime mapping convergence, typed encoding or dynamic-storage replacement. Name-only `schema_cmeta_builtin_data("string"/"bytes")` still returns NULL. Public API review and the remaining #45 gates are not bypassed by this private helper.

## DataBindValueKind compatibility mapping

During migration, the legacy dynamic API still exposes `DataBindValueKind`. Its structural meaning maps to CMeta as follows:

```text
OBJECT   -> CMETA_DATA_STRUCT
LIST     -> CMETA_DATA_SEQUENCE
SET      -> CMETA_DATA_SET
MAP      -> CMETA_DATA_MAP
INT      -> CMETA_DATA_SINT
INT64    -> CMETA_DATA_SINT
UINT64   -> CMETA_DATA_UINT
DOUBLE   -> CMETA_DATA_FLOAT
BOOL     -> CMETA_DATA_BOOL
STRING   -> CMETA_DATA_STRING
BYTES    -> CMETA_DATA_BYTES
UUID     -> CMETA_DATA_CUSTOM (canonical Salts UUID descriptor at descriptor level)
DATETIME -> CMETA_DATA_CUSTOM
DATE     -> CMETA_DATA_CUSTOM
TIME     -> CMETA_DATA_CUSTOM
DURATION -> CMETA_DATA_CUSTOM
DECIMAL  -> CMETA_DATA_CUSTOM
BIGINT   -> CMETA_DATA_CUSTOM
MONEY    -> CMETA_DATA_CUSTOM
```

`DATA_BIND_VALUE_NULL` has no independent native CMeta storage kind and therefore does not publish a CMeta kind through the compatibility mapping.

The `CMETA_DATA_CUSTOM` entries above are migration classifications, not permission to invent DataBind-private permanent descriptors. Each supported domain scalar must ultimately point at one canonical CMeta data descriptor with a stable semantic identity.

## Container rule

Schema chooses semantic shape, not concrete CSTL storage:

```text
sequence<User> -> Vec<User> | List<User> | Deque<User> ...
set<User>      -> Set<User> | HashSet<User> ...
map<K,V>       -> Map<K,V> | HashMap<K,V> | BTree<K,V> ...
```

CBind and the dynamic runtime must consume the CMeta container/Range/Collector contracts and must not inspect CSTL implementation layouts.

## Reflection rule

The final DataBind schema reflection surface may expose schema-specific metadata, but structural answers must be sourced from CMeta:

- field structural type and semantic identity -> CMeta;
- struct/enum/container shape -> CMeta;
- generic constructor/argument identities -> CMeta;
- external/wire field name and aliases -> schema overlay;
- optional/required/default/validation/format -> schema overlay;
- compatibility/fingerprint -> schema layer.

No new DataBind-private type-kind enum may be introduced for a structural concept already expressible by CMeta.

## Gates for #45

1. Legacy `DataBindValueKind` has one checked compatibility mapping to `cmeta_data_kind`.
2. Runtime schema scalar widths map to canonical exact-width CMeta descriptors.
3. Message/enum structural reflection can be represented as CMeta descriptors without losing schema overlay metadata.
4. Container schema nodes lower to semantic CMeta container shapes without hard-coding a CSTL implementation.
5. Unsupported/gated constructs fail explicitly; no fallback creates a private runtime type.
6. Generated and runtime schema paths consume the same lowering rules.
