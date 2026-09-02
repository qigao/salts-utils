#ifndef TBE_TYPED_H
#define TBE_TYPED_H

#include "data_bind.h"
#include "turbo_str.h"
#include <rocida/stl.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define TBE_TYPED_ALIGNOF(TYPE) alignof(TYPE)
#else
#define TBE_TYPED_ALIGNOF(TYPE) _Alignof(TYPE)
#endif

/* TBE generated records keep the raw vector as their complete field storage.
 * This module-owned facade preserves that public layout while delegating every
 * allocation and mutation to TurboSTL's bounded raw vector implementation. */
#define TBE_TYPED_VEC_DEFINE(NAME, TYPE)                                                   \
  typedef struct NAME {                                                                   \
    vec_t raw;                                                                            \
  } NAME;                                                                                 \
  static inline stl_status NAME##_init(NAME *vec, size_t limit) {                         \
    return vec ? vec_init_bytes(&vec->raw, sizeof(TYPE), TBE_TYPED_ALIGNOF(TYPE), limit)  \
               : STL_INVALID_ARGUMENT;                                                    \
  }                                                                                       \
  static inline stl_status NAME##_from(NAME *vec, const TYPE *elements,                   \
                                       size_t count, size_t limit) {                       \
    return vec ? vec_from_array_bytes(&vec->raw, elements, count, sizeof(TYPE),           \
                                      TBE_TYPED_ALIGNOF(TYPE), limit)                     \
               : STL_INVALID_ARGUMENT;                                                    \
  }                                                                                       \
  static inline void NAME##_destroy(NAME *vec) {                                          \
    if (vec) vec_destroy(&vec->raw);                                                       \
  }                                                                                       \
  static inline stl_status NAME##_clear(NAME *vec) {                                      \
    return vec ? vec_clear(&vec->raw) : STL_INVALID_ARGUMENT;                             \
  }                                                                                       \
  static inline stl_status NAME##_reserve(NAME *vec, size_t capacity) {                   \
    return vec ? vec_reserve(&vec->raw, capacity) : STL_INVALID_ARGUMENT;                 \
  }                                                                                       \
  static inline stl_status NAME##_resize(NAME *vec, size_t size) {                        \
    return vec ? vec_resize(&vec->raw, size) : STL_INVALID_ARGUMENT;                      \
  }                                                                                       \
  static inline stl_status NAME##_push(NAME *vec, TYPE value) {                           \
    return vec ? vec_push(&vec->raw, &value) : STL_INVALID_ARGUMENT;                      \
  }                                                                                       \
  static inline stl_status NAME##_pop(NAME *vec, TYPE *out_value) {                       \
    return vec ? vec_pop(&vec->raw, out_value) : STL_INVALID_ARGUMENT;                    \
  }                                                                                       \
  static inline TYPE *NAME##_at(NAME *vec, size_t index) {                                \
    return vec ? (TYPE *)vec_at(&vec->raw, index) : NULL;                                 \
  }                                                                                       \
  static inline const TYPE *NAME##_at_const(const NAME *vec, size_t index) {              \
    return vec ? (const TYPE *)vec_at_const(&vec->raw, index) : NULL;                     \
  }                                                                                       \
  static inline TYPE *NAME##_data(NAME *vec) {                                            \
    return vec ? (TYPE *)vec_data(&vec->raw) : NULL;                                      \
  }                                                                                       \
  static inline const TYPE *NAME##_data_const(const NAME *vec) {                          \
    return vec ? (const TYPE *)vec_data_const(&vec->raw) : NULL;                         \
  }                                                                                       \
  static inline size_t NAME##_size(const NAME *vec) {                                     \
    return vec ? vec_size(&vec->raw) : 0u;                                                \
  }                                                                                       \
  static inline size_t NAME##_capacity(const NAME *vec) {                                 \
    return vec ? vec_capacity(&vec->raw) : 0u;                                            \
  }                                                                                       \
  static inline bool NAME##_empty(const NAME *vec) {                                      \
    return !vec || vec_empty(&vec->raw);                                                   \
  }

#ifdef __cplusplus
extern "C" {
#endif

typedef struct json_value_s json_value_t;

/** Runtime field kinds used by generated owning C records. */
typedef enum TbeTypedKind {
  TBE_TYPED_BOOL = 0,
  TBE_TYPED_I8,
  TBE_TYPED_U8,
  TBE_TYPED_I16,
  TBE_TYPED_U16,
  TBE_TYPED_I32,
  TBE_TYPED_U32,
  TBE_TYPED_I64,
  TBE_TYPED_U64,
  TBE_TYPED_F32,
  TBE_TYPED_F64,
  TBE_TYPED_ENUM,
  TBE_TYPED_STRING,
  TBE_TYPED_BYTES,
  TBE_TYPED_FIXED_BYTES,
  TBE_TYPED_OBJECT,
  TBE_TYPED_FIXED_ARRAY,
  TBE_TYPED_LIST,
  TBE_TYPED_SET,
  TBE_TYPED_MAP,
  TBE_TYPED_UUID
} TbeTypedKind;

enum {
  TBE_TYPED_FIELD_OPTIONAL = 1u << 0,
  TBE_TYPED_FIELD_WIRE_OFFSET = 1u << 1,
  TBE_TYPED_FIELD_VAR_DATA = 1u << 2,
  TBE_TYPED_FIELD_GROUP = 1u << 3
};

typedef struct TbeTypedType TbeTypedType;

typedef struct TbeTypedField {
  const char *name;
  TbeTypedKind kind;
  TbeTypedKind wire_kind;
  size_t offset;
  TbeTypedKind element_kind;
  TbeTypedKind element_wire_kind;
  size_t element_size;
  size_t fixed_count;
  const TbeTypedType *object_type;
  size_t map_entry_size;
  size_t map_key_offset;
  size_t map_value_offset;
  TbeTypedKind map_value_kind;
  TbeTypedKind map_value_wire_kind;
  const TbeTypedType *map_value_type;
  size_t wire_offset;
  size_t wire_size;
  unsigned optional_bit;
  unsigned flags;
} TbeTypedField;

struct TbeTypedType {
  const char *name;
  size_t size;
  const TbeTypedField *fields;
  size_t field_count;
  size_t fixed_block_size;
  size_t presence_offset;
  size_t presence_size;
  int wire_big_endian;
};

enum { TBE_TYPED_DESCRIPTOR_ABI_VERSION = 1 };

/** Versioned boundary for descriptors compiled separately from DataBind. */
typedef struct TbeTypedDescriptor {
  size_t struct_size;
  uint32_t abi_version;
  const TbeTypedType *type;
} TbeTypedDescriptor;

#define TBE_TYPED_DESCRIPTOR_INIT(TYPE)                                                     \
  { sizeof(TbeTypedDescriptor), TBE_TYPED_DESCRIPTOR_ABI_VERSION, (TYPE) }

/**
 * Header-only descriptors for binding an existing C struct.
 *
 * These macros create the same TbeTypedField/TbeTypedType metadata emitted by
 * tbe_compiler, without generating a header or source file. Schema names are
 * canonical names; `[name(...)]` and `[alias(...)]` remain schema concerns.
 *
 * A requirement argument is either TBE_TYPED_REQUIRED or
 * TBE_TYPED_OPTIONAL(bit). Optional fields require a descriptor created with
 * TBE_TYPED_DEFINE_STRUCT_WITH_PRESENCE or TBE_TYPED_DEFINE_STRUCT_EX.
 */
#define TBE_TYPED_REQUIRED 0u, 0u
#define TBE_TYPED_OPTIONAL(BIT) (unsigned)(BIT), TBE_TYPED_FIELD_OPTIONAL

#define TBE_TYPED_FIELD_EX(                                                              \
    C_TYPE, MEMBER, SCHEMA_NAME, KIND, WIRE_KIND, ELEMENT_KIND, ELEMENT_WIRE_KIND,       \
    ELEMENT_SIZE, FIXED_COUNT, OBJECT_TYPE, MAP_ENTRY_SIZE, MAP_KEY_OFFSET,               \
    MAP_VALUE_OFFSET, MAP_VALUE_KIND, MAP_VALUE_WIRE_KIND, MAP_VALUE_TYPE, WIRE_OFFSET,   \
    WIRE_SIZE, OPTIONAL_BIT, FLAGS)                                                       \
  {                                                                                      \
    (SCHEMA_NAME), (KIND), (WIRE_KIND), offsetof(C_TYPE, MEMBER), (ELEMENT_KIND),         \
        (ELEMENT_WIRE_KIND), (ELEMENT_SIZE), (FIXED_COUNT), (OBJECT_TYPE),                \
        (MAP_ENTRY_SIZE), (MAP_KEY_OFFSET), (MAP_VALUE_OFFSET), (MAP_VALUE_KIND),         \
        (MAP_VALUE_WIRE_KIND), (MAP_VALUE_TYPE), (WIRE_OFFSET), (WIRE_SIZE),              \
        (OPTIONAL_BIT), (FLAGS)                                                           \
  }

#define TBE_TYPED_PRIVATE_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, KIND, OPTIONAL_BIT, FLAGS)   \
  TBE_TYPED_FIELD_EX(C_TYPE, MEMBER, SCHEMA_NAME, KIND, KIND, TBE_TYPED_BOOL,             \
                     TBE_TYPED_BOOL, 0u, 0u, NULL, 0u, 0u, 0u, TBE_TYPED_BOOL,            \
                     TBE_TYPED_BOOL, NULL, 0u, 0u, OPTIONAL_BIT, FLAGS)

/** Bind a scalar, enum, UUID, tstr string, or tbe_bytes_t member. */
#define TBE_TYPED_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, KIND, REQUIREMENT)                    \
  TBE_TYPED_PRIVATE_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, KIND, REQUIREMENT)

#define TBE_TYPED_PRIVATE_OBJECT_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, OBJECT_TYPE,           \
                                       OPTIONAL_BIT, FLAGS)                                \
  TBE_TYPED_FIELD_EX(C_TYPE, MEMBER, SCHEMA_NAME, TBE_TYPED_OBJECT, TBE_TYPED_OBJECT,      \
                     TBE_TYPED_BOOL, TBE_TYPED_BOOL, 0u, 0u, OBJECT_TYPE, 0u, 0u, 0u,     \
                     TBE_TYPED_BOOL, TBE_TYPED_BOOL, NULL, 0u, 0u, OPTIONAL_BIT, FLAGS)

/** Bind an inline nested C struct using another descriptor. */
#define TBE_TYPED_OBJECT_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, OBJECT_TYPE, REQUIREMENT)       \
  TBE_TYPED_PRIVATE_OBJECT_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, OBJECT_TYPE, REQUIREMENT)

#define TBE_TYPED_PRIVATE_COLLECTION_FIELD(                                                \
    C_TYPE, MEMBER, SCHEMA_NAME, KIND, ELEMENT_KIND, ELEMENT_C_TYPE, OBJECT_TYPE,          \
    FIXED_COUNT, OPTIONAL_BIT, FLAGS)                                                       \
  TBE_TYPED_FIELD_EX(C_TYPE, MEMBER, SCHEMA_NAME, KIND, KIND, ELEMENT_KIND, ELEMENT_KIND,  \
                     sizeof(ELEMENT_C_TYPE), FIXED_COUNT, OBJECT_TYPE, 0u, 0u, 0u,         \
                     TBE_TYPED_BOOL, TBE_TYPED_BOOL, NULL, 0u, 0u, OPTIONAL_BIT, FLAGS)

/** Bind a TBE_TYPED_VEC_DEFINE-compatible list member. */
#define TBE_TYPED_LIST_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, ELEMENT_KIND, ELEMENT_C_TYPE,     \
                             OBJECT_TYPE, REQUIREMENT)                                      \
  TBE_TYPED_PRIVATE_COLLECTION_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, TBE_TYPED_LIST,          \
                                     ELEMENT_KIND, ELEMENT_C_TYPE, OBJECT_TYPE, 0u,         \
                                     REQUIREMENT)

/** Bind a TBE_TYPED_VEC_DEFINE-compatible set member. */
#define TBE_TYPED_SET_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, ELEMENT_KIND, ELEMENT_C_TYPE,      \
                            OBJECT_TYPE, REQUIREMENT)                                       \
  TBE_TYPED_PRIVATE_COLLECTION_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, TBE_TYPED_SET,           \
                                     ELEMENT_KIND, ELEMENT_C_TYPE, OBJECT_TYPE, 0u,         \
                                     REQUIREMENT)

/** Bind a fixed C array; its element count is derived from the member. */
#define TBE_TYPED_FIXED_ARRAY_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, ELEMENT_KIND,              \
                                    ELEMENT_C_TYPE, OBJECT_TYPE, REQUIREMENT)                \
  TBE_TYPED_PRIVATE_COLLECTION_FIELD(                                                       \
      C_TYPE, MEMBER, SCHEMA_NAME, TBE_TYPED_FIXED_ARRAY, ELEMENT_KIND, ELEMENT_C_TYPE,     \
      OBJECT_TYPE, sizeof(((C_TYPE *)0)->MEMBER) / sizeof(((C_TYPE *)0)->MEMBER[0]),        \
      REQUIREMENT)

/** Bind a fixed uint8_t byte array; its byte count is derived from the member. */
#define TBE_TYPED_FIXED_BYTES_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, REQUIREMENT)                \
  TBE_TYPED_FIELD_EX(C_TYPE, MEMBER, SCHEMA_NAME, TBE_TYPED_FIXED_BYTES,                    \
                     TBE_TYPED_FIXED_BYTES, TBE_TYPED_U8, TBE_TYPED_U8, sizeof(uint8_t),    \
                     sizeof(((C_TYPE *)0)->MEMBER), NULL, 0u, 0u, 0u, TBE_TYPED_BOOL,      \
                     TBE_TYPED_BOOL, NULL, 0u, 0u, REQUIREMENT)

#define TBE_TYPED_PRIVATE_MAP_FIELD(                                                        \
    C_TYPE, MEMBER, SCHEMA_NAME, ENTRY_TYPE, KEY_MEMBER, VALUE_MEMBER, VALUE_KIND,          \
    VALUE_TYPE, OPTIONAL_BIT, FLAGS)                                                        \
  TBE_TYPED_FIELD_EX(                                                                       \
      C_TYPE, MEMBER, SCHEMA_NAME, TBE_TYPED_MAP, TBE_TYPED_MAP, TBE_TYPED_BOOL,            \
      TBE_TYPED_BOOL, sizeof(ENTRY_TYPE), 0u, NULL, sizeof(ENTRY_TYPE),                     \
      offsetof(ENTRY_TYPE, KEY_MEMBER), offsetof(ENTRY_TYPE, VALUE_MEMBER), VALUE_KIND,     \
      VALUE_KIND, VALUE_TYPE, 0u, 0u, OPTIONAL_BIT, FLAGS)

/**
 * Bind a TBE_TYPED_VEC_DEFINE-compatible map member.
 * ENTRY_TYPE::KEY_MEMBER must be tstr; VALUE_TYPE is NULL except for objects.
 */
#define TBE_TYPED_MAP_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, ENTRY_TYPE, KEY_MEMBER,             \
                            VALUE_MEMBER, VALUE_KIND, VALUE_TYPE, REQUIREMENT)                \
  TBE_TYPED_PRIVATE_MAP_FIELD(C_TYPE, MEMBER, SCHEMA_NAME, ENTRY_TYPE, KEY_MEMBER,          \
                              VALUE_MEMBER, VALUE_KIND, VALUE_TYPE, REQUIREMENT)

#define TBE_TYPED_PRIVATE_DEFINE_STRUCT(                                                    \
    BINDING, C_TYPE, SCHEMA_NAME, FIXED_BLOCK_SIZE, PRESENCE_OFFSET, PRESENCE_SIZE,         \
    WIRE_BIG_ENDIAN, ...)                                                                   \
  static const TbeTypedField BINDING##_fields[] = {__VA_ARGS__};                           \
  static const TbeTypedType BINDING = {                                                     \
      (SCHEMA_NAME),                                                                        \
      sizeof(C_TYPE),                                                                       \
      BINDING##_fields,                                                                     \
      sizeof(BINDING##_fields) / sizeof(BINDING##_fields[0]),                              \
      (FIXED_BLOCK_SIZE),                                                                   \
      (PRESENCE_OFFSET),                                                                    \
      (PRESENCE_SIZE),                                                                      \
      (WIRE_BIG_ENDIAN)};                                                                   \
  static const TbeTypedDescriptor BINDING##_descriptor = TBE_TYPED_DESCRIPTOR_INIT(&(BINDING))

/** Define a text-format binding for a C struct with no optional fields. */
#define TBE_TYPED_DEFINE_STRUCT(BINDING, C_TYPE, SCHEMA_NAME, ...)                          \
  TBE_TYPED_PRIVATE_DEFINE_STRUCT(BINDING, C_TYPE, SCHEMA_NAME, 0u, 0u, 0u, 0,            \
                                  __VA_ARGS__)

/** Define a text-format binding whose presence member is a byte bitmap. */
#define TBE_TYPED_DEFINE_STRUCT_WITH_PRESENCE(BINDING, C_TYPE, SCHEMA_NAME,                 \
                                              PRESENCE_MEMBER, ...)                         \
  TBE_TYPED_PRIVATE_DEFINE_STRUCT(                                                          \
      BINDING, C_TYPE, SCHEMA_NAME, 0u, offsetof(C_TYPE, PRESENCE_MEMBER),                 \
      sizeof(((C_TYPE *)0)->PRESENCE_MEMBER), 0, __VA_ARGS__)

/**
 * Define a descriptor with an explicit binary layout.
 * Field initializers must provide matching wire offsets/flags through
 * TBE_TYPED_FIELD_EX. Invalid or incomplete layouts fail descriptor/schema
 * validation; no layout is inferred from the host C struct.
 */
#define TBE_TYPED_DEFINE_STRUCT_EX(BINDING, C_TYPE, SCHEMA_NAME, FIXED_BLOCK_SIZE,           \
                                   PRESENCE_OFFSET, PRESENCE_SIZE, WIRE_BIG_ENDIAN, ...)     \
  TBE_TYPED_PRIVATE_DEFINE_STRUCT(BINDING, C_TYPE, SCHEMA_NAME, FIXED_BLOCK_SIZE,           \
                                  PRESENCE_OFFSET, PRESENCE_SIZE, WIRE_BIG_ENDIAN,           \
                                  __VA_ARGS__)

/** Convenience calls using the schema/type name stored in a macro descriptor. */
#define TBE_TYPED_BIND_INIT(BINDING, OBJECT, ERROR)                                         \
  tbe_typed_init(&(BINDING), (OBJECT), (ERROR))
#define TBE_TYPED_BIND_CLEAR(BINDING, OBJECT) tbe_typed_clear(&(BINDING), (OBJECT))
#define TBE_TYPED_BIND_PARSE(CODEC, BINDING, FORMAT, DATA, LEN, ROW, OBJECT, ERROR)          \
  tbe_typed_parse((CODEC), (BINDING).name, &(BINDING), (FORMAT), (DATA), (LEN), (ROW),      \
                  (OBJECT), (ERROR))
#define TBE_TYPED_BIND_PARSE_EX(CODEC, BINDING, FORMAT, DATA, LEN, ROW, OBJECT, ERROR)       \
  tbe_typed_parse_ex((CODEC), (BINDING).name, &(BINDING), (FORMAT), (DATA), (LEN), (ROW),   \
                     (OBJECT), (ERROR))
#define TBE_TYPED_BIND_SERIALIZE(CODEC, BINDING, OBJECT, FORMAT, OUT, OUT_LEN, ERROR)        \
  tbe_typed_serialize((CODEC), (BINDING).name, &(BINDING), (OBJECT), (FORMAT), (OUT),       \
                      (OUT_LEN), (ERROR))
#define TBE_TYPED_BIND_SERIALIZE_EX(CODEC, BINDING, OBJECT, FORMAT, OUT, OUT_LEN, ERROR)     \
  tbe_typed_serialize_ex((CODEC), (BINDING).name, &(BINDING), (OBJECT), (FORMAT), (OUT),    \
                         (OUT_LEN), (ERROR))

TBE_TYPED_VEC_DEFINE(tbe_bytes_t, uint8_t)

/**
 * Initialize an owning generated object from its descriptor.
 * @param type Generated record descriptor.
 * @param object Writable storage of at least `type->size` bytes.
 * @param error Optional structured error initialized with `DATA_BIND_ERROR_INIT`.
 * @return `DATA_BIND_OK`, or an allocation/argument error. On success, call
 *         `tbe_typed_clear` before discarding the object.
 */
DATA_BIND_API DataBindStatus tbe_typed_init(const TbeTypedType *type, void *object,
                                            DataBindError *error);

/** Release all owned strings, byte vectors, containers, and nested objects. */
DATA_BIND_API void tbe_typed_clear(const TbeTypedType *type, void *object);

/**
 * Validate host-memory bounds, collection metadata, optional presence bits,
 * and nested descriptors without requiring a schema codec.
 */
DATA_BIND_API DataBindStatus tbe_typed_validate_descriptor(const TbeTypedType *type,
                                                           DataBindError *error);

/** Validate the descriptor ABI before reading its TbeTypedType layout. */
DATA_BIND_API DataBindStatus tbe_typed_descriptor_validate(
    const TbeTypedDescriptor *descriptor, DataBindError *error);

/**
 * Populate an initialized object from a schema-bound dynamic value.
 * @return `DATA_BIND_OK`, or a type/range/allocation error.
 */
DATA_BIND_API DataBindStatus tbe_typed_from_value(const TbeTypedType *type,
                                                  const DataBindValue *value, void *object,
                                                  DataBindError *error);

/**
 * Verify that a generated native descriptor matches a type in @p codec.
 *
 * This checks the record kind, field order, field names, optional flags, byte
 * order, defined wire offsets, scalar kinds, and host-memory bounds. It does
 * not retain @p codec.
 */
DATA_BIND_API DataBindStatus tbe_typed_validate_schema(DataBind *codec, const char *type_name,
                                                       const TbeTypedType *type,
                                                       DataBindError *error);

/**
 * Decode binary wire data directly into an initialized owning object.
 *
 * The descriptor must define a fixed binary block plus supported group or
 * string/bytes tail fields. It is the binary layout contract. The previous
 * object remains unchanged on failure. Call tbe_typed_validate_schema when the
 * descriptor and codec were not generated from the same trusted schema.
 */
DATA_BIND_API DataBindStatus tbe_typed_parse_binary(const TbeTypedType *type, const void *data,
                                                    size_t len, void *object, DataBindError *error);

/**
 * Convert an owning object to a newly allocated TurboParser JSON value.
 * Release the result with tbe_typed_json_free().
 */
DATA_BIND_API json_value_t *tbe_typed_to_json(const TbeTypedType *type, const void *object,
                                              DataBindError *error);

/** Release a JSON value returned by tbe_typed_to_json() and set it to NULL. */
DATA_BIND_API void tbe_typed_json_free(json_value_t **value);

/**
 * Parse `bin`, `json`, `yaml`, `csv`, or `xml` into an initialized object.
 * CSV uses the zero-based @p row index. The previous object remains unchanged
 * when parsing fails.
 */
DATA_BIND_API DataBindStatus tbe_typed_parse(DataBind *codec, const char *type_name,
                                             const TbeTypedType *type, const char *format,
                                             const void *data, size_t len, size_t row, void *object,
                                             DataBindError *error);

/** Enum-based typed parse; preferred over the string compatibility wrapper. */
DATA_BIND_API DataBindStatus tbe_typed_parse_ex(DataBind *codec, const char *type_name,
                                                const TbeTypedType *type, DataBindFormat format,
                                                const void *data, size_t len, size_t row,
                                                void *object, DataBindError *error);

/** Versioned descriptor parse for generated/shared-library boundaries. */
DATA_BIND_API DataBindStatus tbe_typed_descriptor_parse(
    DataBind *codec, const char *type_name, const TbeTypedDescriptor *descriptor,
    DataBindFormat format, const void *data, size_t len, size_t row, void *object,
    DataBindError *error);

/**
 * Serialize to `json`, `yaml`, `csv`, or `xml` using schema field mappings.
 * The primary `[name(...)]` annotation is used for output; `[alias(...)]`
 * annotations are input-only.
 * @param codec Required for every text format.
 * @param out Receives an allocated buffer released by `tbe_typed_serialized_free`.
 */
DATA_BIND_API DataBindStatus tbe_typed_serialize(DataBind *codec, const char *type_name,
                                                 const TbeTypedType *type, const void *object,
                                                 const char *format, char **out, size_t *out_len,
                                                 DataBindError *error);

/** Enum-based typed serialization; binary uses the binary-specific APIs. */
DATA_BIND_API DataBindStatus tbe_typed_serialize_ex(DataBind *codec, const char *type_name,
                                                    const TbeTypedType *type, const void *object,
                                                    DataBindFormat format, char **out,
                                                    size_t *out_len, DataBindError *error);

/** Versioned descriptor serialization for generated/shared-library boundaries. */
DATA_BIND_API DataBindStatus tbe_typed_descriptor_serialize(
    DataBind *codec, const char *type_name, const TbeTypedDescriptor *descriptor,
    const void *object, DataBindFormat format, char **out, size_t *out_len,
    DataBindError *error);

/** Serialize an owning object into its schema binary wire representation. */
DATA_BIND_API DataBindStatus tbe_typed_serialize_binary(const TbeTypedType *type,
                                                        const void *object, uint8_t **out,
                                                        size_t *out_len, DataBindError *error);

/**
 * Serialize binary wire bytes into caller-owned storage without allocating the
 * output buffer. out_len receives the required size even when capacity is too
 * small, in which case DATA_BIND_ERR_BUFFER_TOO_SMALL is returned. Passing
 * output=NULL and capacity=0 performs a size query without writing bytes.
 */
DATA_BIND_API DataBindStatus tbe_typed_serialize_binary_into(const TbeTypedType *type,
                                                             const void *object, uint8_t *output,
                                                             size_t capacity, size_t *out_len,
                                                             DataBindError *error);

/** Release any buffer returned by a generated `*_to_*` function. */
DATA_BIND_API void tbe_typed_serialized_free(void *data);

#ifdef __cplusplus
}
#endif

#endif
