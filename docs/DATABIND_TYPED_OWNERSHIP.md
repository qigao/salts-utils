# DataBind historical `tbe_typed` ownership inventory

Tracking issue: #236  
Stacked on: PR #235

This document classifies every public identifier exposed by
`databind/runtime/tbe_typed.h` before any further rename or migration.

The rule is architectural, not lexical:

- **CMeta / DataBind native** owns native type identity, storage, lifecycle,
  ownership and canonical CSerde <-> native conversion.
- **DataBind Contract** owns logical presence, nullability, defaults and
  constraints.
- **Binary** owns only Binary representation/layout/codec semantics.
- **Generated native storage** owns generated C/CSTL convenience storage where
  still required.
- **Format-neutral DataBind** owns generic format dispatch only until it is
  replaced by FormatPlan / BindingPlan / native boundaries.
- **Obsolete mixed** means the identifier combines more than one authority and
  must be removed after its consumers move. It must not be mechanically renamed.

Canonical format/API/config spelling is `binary`. The concise schema annotation
namespace is `@bin.*`.

## Core public types

| Identifier | Current role | Canonical owner | Target / action |
| --- | --- | --- | --- |
| `TbeTypedKind` | Shared native + wire kind universe | CMeta + Binary private lowering | Remove. Native identity comes from CMeta; Binary may derive a backend-private scalar/layout kind. Do not create `DataBindBinaryKind`. |
| `TbeTypedField` | Native offsets/storage + schema state + Binary wire layout | Split across CMeta, Contract HIR, BinaryLayoutIR | Remove mixed descriptor after compiler/runtime cutover. |
| `TbeTypedType` | Native size/fields + state bitmaps + Binary fixed-block/byte-order facts | Split across CMeta, Contract HIR, BinaryLayoutIR | Remove mixed descriptor after compiler/runtime cutover. |
| `TbeTypedDescriptor` | Joins mixed overlay to `cmeta_data_desc` | Native admission + Binary plan | Remove. Native join uses CMeta/DataBind native admission; Binary consumes a separate validated layout. |

## Public kind constants

These identifiers are all part of the duplicate generic type universe and are
therefore **not** future Binary public type identifiers.

| Identifier | Classification | Target / action |
| --- | --- | --- |
| `TBE_TYPED_BOOL` | Obsolete mixed type identity | CMeta bool identity; derive Binary representation privately. |
| `TBE_TYPED_I8` | Obsolete mixed type identity | CMeta signed integer identity. |
| `TBE_TYPED_U8` | Obsolete mixed type identity | CMeta unsigned integer identity. |
| `TBE_TYPED_I16` | Obsolete mixed type identity | CMeta signed integer identity. |
| `TBE_TYPED_U16` | Obsolete mixed type identity | CMeta unsigned integer identity. |
| `TBE_TYPED_I32` | Obsolete mixed type identity | CMeta signed integer identity. |
| `TBE_TYPED_U32` | Obsolete mixed type identity | CMeta unsigned integer identity. |
| `TBE_TYPED_I64` | Obsolete mixed type identity | CMeta signed integer identity. |
| `TBE_TYPED_U64` | Obsolete mixed type identity | CMeta unsigned integer identity. |
| `TBE_TYPED_F32` | Obsolete mixed type identity | CMeta float identity. |
| `TBE_TYPED_F64` | Obsolete mixed type identity | CMeta float identity. |
| `TBE_TYPED_ENUM` | Obsolete mixed type identity | CMeta enum identity/domain. |
| `TBE_TYPED_STRING` | Obsolete mixed type identity | CMeta buffer/string provider semantics. |
| `TBE_TYPED_BYTES` | Obsolete mixed type identity | CMeta buffer provider semantics. |
| `TBE_TYPED_FIXED_BYTES` | Obsolete mixed type identity | CMeta fixed-array/buffer shape + Binary lowering. |
| `TBE_TYPED_OBJECT` | Obsolete mixed type identity | CMeta Struct identity. |
| `TBE_TYPED_FIXED_ARRAY` | Obsolete mixed type identity | CMeta fixed container/array shape. |
| `TBE_TYPED_LIST` | Obsolete mixed type identity | CMeta/CSTL container provider. |
| `TBE_TYPED_SET` | Obsolete mixed type identity | CMeta/CSTL container provider. |
| `TBE_TYPED_MAP` | Obsolete mixed type identity | CMeta/CSTL container provider. |
| `TBE_TYPED_UUID` | Obsolete mixed type identity | Canonical CMeta UUID/native identity. |

## Field/state/layout flags

| Identifier | Current role | Canonical owner | Target / action |
| --- | --- | --- | --- |
| `TBE_TYPED_FIELD_OPTIONAL` | Host/schema presence | DataBind Contract + chosen native representation | Remove from mixed public descriptor. Presence remains logical Contract semantics; native bitmap is only one lowering. |
| `TBE_TYPED_FIELD_NULLABLE` | Host/schema null state | DataBind Contract + chosen native representation | Remove from mixed public descriptor. |
| `TBE_TYPED_FIELD_WIRE_OFFSET` | Explicit Binary fixed field location | Binary | Move to BinaryLayoutIR/internal Binary plan. |
| `TBE_TYPED_FIELD_VAR_DATA` | Binary variable tail encoding | Binary | Move to BinaryLayoutIR/internal Binary plan. |
| `TBE_TYPED_FIELD_GROUP` | Binary repeated/group encoding | Binary | Move to BinaryLayoutIR/internal Binary plan. |

## Descriptor ABI helpers

| Identifier | Classification | Target / action |
| --- | --- | --- |
| `TBE_TYPED_DESCRIPTOR_ABI_VERSION` | Transitional mixed-descriptor ABI | Remove when `TbeTypedDescriptor` is removed; do not create a Binary alias. |
| `TBE_TYPED_DESCRIPTOR_INIT` | Transitional mixed-descriptor constructor | Remove; native and Binary descriptors/plans are constructed independently. |

## Generated native storage helpers

These are not Binary semantics even when generated code currently reaches them
through `tbe_typed.h`.

| Identifier | Canonical owner | Target / action |
| --- | --- | --- |
| `TBE_TYPED_ALIGNOF` | Generated native storage | Replace with normal C/CMeta/CSTL helper or eliminate from generated API. |
| `TBE_TYPED_VEC_DEFINE` | Generated native/CSTL storage | Move to DataBind-generated native helper or generate canonical CSTL provider use directly. |
| `tbe_bytes_t` | Generated native/CSTL storage | Replace with non-TBE generated/native storage type or canonical CMeta buffer provider. |

## Requirement/state macros

| Identifier | Canonical owner | Target / action |
| --- | --- | --- |
| `TBE_TYPED_REQUIRED` | DataBind Contract/native lowering convenience | Remove after compiler emits resolved presence facts directly. |
| `TBE_TYPED_OPTIONAL` | DataBind Contract/native lowering convenience | Remove after compiler emits resolved presence facts directly. |
| `TBE_TYPED_NULLABLE_FLAGS` | DataBind Contract/native lowering convenience | Remove after compiler emits resolved nullability facts directly. |
| `TBE_TYPED_OPTIONAL_NULLABLE_FLAGS` | DataBind Contract/native lowering convenience | Remove after compiler emits resolved state facts directly. |

## Field declaration macros

All of these construct the mixed `TbeTypedField` model and are therefore
**obsolete mixed**. Their native facts must come from CMeta/generated native
reflection while Binary facts are emitted to BinaryLayoutIR.

| Identifier | Action |
| --- | --- |
| `TBE_TYPED_FIELD_STATE_EX` | Remove after split generator emits independent native + Binary metadata. |
| `TBE_TYPED_FIELD_EX` | Remove after split generator emits independent native + Binary metadata. |
| `TBE_TYPED_PRIVATE_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_FIELD_STATE` | Remove with mixed macro layer. |
| `TBE_TYPED_PRIVATE_OBJECT_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_OBJECT_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_PRIVATE_COLLECTION_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_LIST_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_SET_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_FIXED_ARRAY_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_FIXED_BYTES_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_PRIVATE_MAP_FIELD` | Remove with mixed macro layer. |
| `TBE_TYPED_MAP_FIELD` | Remove with mixed macro layer. |

## Struct/type declaration macros

| Identifier | Classification | Target / action |
| --- | --- | --- |
| `TBE_TYPED_PRIVATE_DEFINE_STRUCT_STATE` | Obsolete mixed native/state/Binary builder | Remove. |
| `TBE_TYPED_PRIVATE_DEFINE_STRUCT` | Obsolete mixed native/state/Binary builder | Remove. |
| `TBE_TYPED_DEFINE_STRUCT` | Generated native convenience over mixed descriptor | Replace with generated CMeta/native metadata; no Binary ownership. |
| `TBE_TYPED_DEFINE_STRUCT_WITH_PRESENCE` | Generated native + Contract state convenience | Replace with Contract lowering + generated native representation. |
| `TBE_TYPED_DEFINE_STRUCT_WITH_STATE` | Generated native + Contract state convenience | Replace with Contract lowering + generated native representation. |
| `TBE_TYPED_DEFINE_STRUCT_EX` | Explicit Binary layout mixed with native metadata | Split: native CMeta + BinaryLayoutIR. |
| `TBE_TYPED_DEFINE_STRUCT_STATE_EX` | Explicit Binary/state layout mixed with native metadata | Split: Contract state + native CMeta + BinaryLayoutIR. |

## Bind convenience macros

These are format-neutral convenience wrappers around the historical mixed
runtime. They are not Binary APIs.

| Identifier | Canonical owner | Target / action |
| --- | --- | --- |
| `TBE_TYPED_BIND_INIT` | DataBind native lifecycle | Generated code should call canonical native lifecycle/plan entry. Remove wrapper. |
| `TBE_TYPED_BIND_CLEAR` | DataBind native lifecycle | Generated code should call canonical native lifecycle/plan entry. Remove wrapper. |
| `TBE_TYPED_BIND_PARSE` | Format-neutral DataBind | Replace with FormatPlan/provider + native/BindingPlan path. |
| `TBE_TYPED_BIND_PARSE_EX` | Format-neutral DataBind | Replace with enum-based canonical FormatPlan/provider path. |
| `TBE_TYPED_BIND_SERIALIZE` | Format-neutral DataBind | Replace with FormatPlan + native writer/BindingPlan path. |
| `TBE_TYPED_BIND_SERIALIZE_EX` | Format-neutral DataBind | Replace with enum-based canonical FormatPlan + native writer path. |

## Public functions

| Identifier | Current responsibility | Canonical owner | Target / action |
| --- | --- | --- | --- |
| `tbe_typed_init` | Initialize generated owning native value | DataBind native/CMeta lifecycle | Converge on `data_bind_native_init`; close provider gaps instead of preserving duplicate lifecycle. |
| `tbe_typed_clear` | Clear generated owning native value | DataBind native/CMeta lifecycle | Converge on `data_bind_native_clear`. |
| `tbe_typed_validate_descriptor` | Validate mixed host/layout descriptor | Split | Native graph validation moves to CMeta/native; Binary validation moves to BinaryLayoutIR. Remove mixed validator. |
| `tbe_typed_descriptor_validate` | Validate mixed descriptor + CMeta join | Native admission + Binary plan | Split and remove. |
| `tbe_typed_descriptor_init` | Descriptor-routed native init | DataBind native/CMeta lifecycle | Converge on `data_bind_native_init`. |
| `tbe_typed_descriptor_clear` | Descriptor-routed native clear | DataBind native/CMeta lifecycle | Converge on `data_bind_native_clear`. |
| `tbe_typed_from_value` | Dynamic DataBindValue -> native conversion | Format-neutral DataBind/native binding | Transitional. Replace with canonical BindingPlan/native staging path; remove when no generated consumer requires the dynamic-tree bridge. |
| `tbe_typed_validate_schema` | Schema + typed/native/wire compatibility check | Compiler/admission | Split into Contract<->CMeta native admission and Binary representability/layout admission. |
| `tbe_typed_parse_binary` | Direct Binary decode into owning native object | Binary + native binding | Replace with validated Binary plan/generated decoder using canonical native storage. |
| `tbe_typed_to_json` | Native -> JSON DOM | JSON format | Remove from historical typed module; route through JSON/CSerde writer or generated JSON adapter. |
| `tbe_typed_json_free` | Release JSON DOM produced above | JSON format | Remove with `tbe_typed_to_json`. |
| `tbe_typed_parse` | String-selected multi-format parse | Format-neutral DataBind | Remove. No string compatibility/fallback route after cutover. |
| `tbe_typed_parse_ex` | Enum-selected multi-format parse | Format-neutral DataBind | Converge on explicit FormatPlan/provider + native decode. |
| `tbe_typed_descriptor_parse` | Mixed-descriptor multi-format parse | Format-neutral DataBind/native binding | Converge on FormatPlan/provider + CMeta native binding; remove mixed descriptor. |
| `tbe_typed_serialize` | String-selected multi-format serialize | Format-neutral DataBind | Remove. No string compatibility/fallback route after cutover. |
| `tbe_typed_serialize_ex` | Enum-selected multi-format serialize | Format-neutral DataBind | Converge on explicit FormatPlan/provider + native encode. |
| `tbe_typed_descriptor_serialize` | Mixed-descriptor multi-format serialize | Format-neutral DataBind/native binding | Converge on FormatPlan + CMeta native encode; remove mixed descriptor. |
| `tbe_typed_descriptor_serialize_binary` | Descriptor-routed Binary encode with allocation | Binary | Replace with Binary plan/generated exact encoder. |
| `tbe_typed_descriptor_serialize_binary_into` | Descriptor-routed Binary encode into caller storage | Binary | Replace with Binary plan/generated exact encoder-into path. |
| `tbe_typed_serialize_binary` | Binary encode with allocation | Binary | Replace with Binary plan/generated exact encoder. |
| `tbe_typed_serialize_binary_into` | Binary encode into caller storage | Binary | Replace with Binary plan/generated exact encoder-into path. |
| `tbe_typed_serialized_free` | Frees historical allocated serialization buffers | Format-neutral output ownership | Remove with historical allocated-return APIs; replacement ownership is defined by selected format/generated artifact. |

## Header-only implementation identifiers

These are visible macros but are implementation details, not semantic extension
points. They remain listed so the completeness test can detect accidental public
surface growth.

| Identifier | Classification | Action |
| --- | --- | --- |
| `TBE_TYPED_H` | Header guard | Disappears with header removal. |
| `TBE_TYPED_PRIVATE_COLLECTION_FIELD` | Private macro in public header | Remove with mixed macro layer. |
| `TBE_TYPED_PRIVATE_DEFINE_STRUCT` | Private macro in public header | Remove with mixed macro layer. |
| `TBE_TYPED_PRIVATE_DEFINE_STRUCT_STATE` | Private macro in public header | Remove with mixed macro layer. |
| `TBE_TYPED_PRIVATE_FIELD` | Private macro in public header | Remove with mixed macro layer. |
| `TBE_TYPED_PRIVATE_MAP_FIELD` | Private macro in public header | Remove with mixed macro layer. |
| `TBE_TYPED_PRIVATE_OBJECT_FIELD` | Private macro in public header | Remove with mixed macro layer. |

## Current consumer classes

Repository search at the #236 start point shows the public surface is consumed by:

- generated C source/header templates under `databind/compiler/templates/`;
- the Lua bridge in `tools/lua/salts_lua.h`;
- compiler conformance tests;
- typed runtime / nullable / descriptor-boundary tests;
- native-storage requirement tests;
- documentation and generated SDK consumer fixtures.

This means migration should start at compiler-generated metadata and canonical
native providers before deleting the public header. Tests that only prove the
historical mixed descriptor should move with the owner they actually qualify,
not be preserved as compatibility tests.

## Migration order derived from the inventory

1. **Native first:** prove generated records have complete CMeta/provider graphs
   for init/clear/container/string/bytes/enum behavior through
   `data_bind_native_*`.
2. **Format-neutral routing:** cut generated JSON/YAML/XML/CSV paths from
   `tbe_typed_*` to FormatPlan/CSerde/native paths.
3. **Binary layout:** introduce BinaryLayoutIR independent from native struct
   offsets and lower `@bin.*` into it.
4. **Generated Binary specialization:** generate exact Binary encode/decode where
   the schema is build-time known; keep one generic Binary plan executor only
   for runtime-known schemas.
5. **Consumers:** move Lua/tooling/tests to canonical reflection/plans.
6. **Removal:** delete `tbe_typed.h/.c`, `TbeTyped*`, `TBE_TYPED_*` and
   `tbe_bytes_t` without aliases or dual mode.

## Invariant

No future change may add a new `TbeTyped*`, `TBE_TYPED_*`,
`tbe_typed_*`, or `tbe_bytes_t` identifier without updating this ownership
inventory. The accompanying CMake test enforces that rule while #236 is active.
