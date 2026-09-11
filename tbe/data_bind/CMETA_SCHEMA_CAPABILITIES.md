# DataBind / TBE schema to CMeta capability matrix

Status: design baseline for #45. This document is normative for the DataBind convergence work; implementation must not add a second DataBind-private type universe where CMeta already provides the structural/data semantic.

## Ownership rule

Schema is the serializable-data subset of CMeta plus wire/schema semantics.

CMeta owns structural data identity and shape. DataBind/TBE owns schema names, external names and aliases, required/optional/wire presence, defaults and validation constraints, format annotations, compatibility rules, and schema fingerprinting.

Traits, callable/`typed_any`, interface/implements, Range, Collector, effect/property metadata and pointer identity are not schema field types.

## Current lowering matrix

| Schema / DataBind family | Canonical CMeta data semantic | Storage/type source | Status | Notes |
| --- | --- | --- | --- | --- |
| `bool` | `CMETA_DATA_BOOL` | CMeta bool descriptor | supported | No DataBind-private boolean kind is needed beyond compatibility APIs. |
| signed integers | `CMETA_DATA_SINT` | `salts_cmeta_fixed_width.h` exact-width descriptors | supported substrate | Schema width must lower to the exact 8/16/32/64-bit descriptor; never use platform `long` for schema `int64`. |
| unsigned integers | `CMETA_DATA_UINT` | `salts_cmeta_fixed_width.h` exact-width descriptors | supported substrate | Width is part of the native storage contract. |
| floating point | `CMETA_DATA_FLOAT` | CMeta float/double descriptors | supported substrate | Schema float width must remain explicit. |
| `string` | `CMETA_DATA_STRING` | storage-specific CMeta buffer adapter | supported substrate | Ownership/borrowed lifetime belongs to the storage adapter, not schema. |
| `bytes` | `CMETA_DATA_BYTES` | storage-specific CMeta buffer adapter | supported substrate | Same ownership rule as string. |
| `uuid` | string-like custom canonical data descriptor | `salts_uuid_cmeta_data` | supported substrate | Use Salts canonical UUID descriptor; do not manufacture a DataBind UUID descriptor. |
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
