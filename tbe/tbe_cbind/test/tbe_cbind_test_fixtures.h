#ifndef TBE_CBIND_TEST_FIXTURES_H
#define TBE_CBIND_TEST_FIXTURES_H

#include <cmeta/data.h>
#include "turbo_cmeta_data.h"
#include "turbo_str.h"
#include "turbo_vstr.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct tbe_cbind_test_one {
  int value;
} tbe_cbind_test_one;

static const cmeta_type_identity tbe_cbind_test_one_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.one");
static const cmeta_type_desc tbe_cbind_test_one_type = {
    "tbe_cbind_test_one", sizeof(tbe_cbind_test_one),
    _Alignof(tbe_cbind_test_one), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_one_identity};
static const cmeta_field_desc tbe_cbind_test_one_layout_fields[] = {
    {"value", "int", offsetof(tbe_cbind_test_one, value),
     sizeof(((tbe_cbind_test_one *)0)->value),
     _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc tbe_cbind_test_one_layout = {
    "tbe_cbind_test_one", sizeof(tbe_cbind_test_one),
    _Alignof(tbe_cbind_test_one), tbe_cbind_test_one_layout_fields, 1u};
static const cmeta_data_field_desc tbe_cbind_test_one_data_fields[] = {
    {"test.tbe-cbind.one.value", "value",
     offsetof(tbe_cbind_test_one, value), &cmeta_data_int}};
static const cmeta_data_struct_shape tbe_cbind_test_one_shape = {
    &tbe_cbind_test_one_layout, tbe_cbind_test_one_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_test_one_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.one.data", "tbe_cbind_test_one", CMETA_DATA_STRUCT,
    &tbe_cbind_test_one_type, &tbe_cbind_test_one_shape, NULL};

typedef struct tbe_cbind_test_pair {
  int first;
  int second;
} tbe_cbind_test_pair;

static const cmeta_type_identity tbe_cbind_test_pair_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.pair");
static const cmeta_type_desc tbe_cbind_test_pair_type = {
    "tbe_cbind_test_pair", sizeof(tbe_cbind_test_pair),
    _Alignof(tbe_cbind_test_pair), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_pair_identity};
static const cmeta_field_desc tbe_cbind_test_pair_layout_fields[] = {
    {"first", "int", offsetof(tbe_cbind_test_pair, first), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL},
    {"second", "int", offsetof(tbe_cbind_test_pair, second), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc tbe_cbind_test_pair_layout = {
    "tbe_cbind_test_pair", sizeof(tbe_cbind_test_pair),
    _Alignof(tbe_cbind_test_pair), tbe_cbind_test_pair_layout_fields, 2u};
static const cmeta_data_field_desc tbe_cbind_test_pair_data_fields[] = {
    {"test.tbe-cbind.pair.first", "first",
     offsetof(tbe_cbind_test_pair, first), &cmeta_data_int},
    {"test.tbe-cbind.pair.second", "second",
     offsetof(tbe_cbind_test_pair, second), &cmeta_data_int}};
static const cmeta_data_struct_shape tbe_cbind_test_pair_shape = {
    &tbe_cbind_test_pair_layout, tbe_cbind_test_pair_data_fields, 2u};
static const cmeta_data_desc tbe_cbind_test_pair_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.pair.data", "tbe_cbind_test_pair", CMETA_DATA_STRUCT,
    &tbe_cbind_test_pair_type, &tbe_cbind_test_pair_shape, NULL};

typedef union tbe_cbind_test_scalar_slot {
  bool boolean;
  int8_t sint8;
  uint8_t uint8;
  int16_t sint16;
  uint16_t uint16;
  int32_t sint32;
  uint32_t uint32;
  int64_t sint64;
  uint64_t uint64;
  float real32;
  double real64;
  tstr string;
  turbo_uuid_t uuid;
} tbe_cbind_test_scalar_slot;

static const cmeta_type_identity tbe_cbind_test_scalar_slot_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.scalar-slot");
static const cmeta_type_desc tbe_cbind_test_scalar_slot_type = {
    "tbe_cbind_test_scalar_slot", sizeof(tbe_cbind_test_scalar_slot),
    _Alignof(tbe_cbind_test_scalar_slot), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_scalar_slot_identity};

typedef struct tbe_cbind_test_scalars {
  bool boolean;
  int8_t sint8;
  uint8_t uint8;
  int16_t sint16;
  uint16_t uint16;
  int32_t sint32;
  uint32_t uint32;
  int64_t sint64;
  uint64_t uint64;
  float real32;
  double real64;
  turbo_uuid_t uuid;
} tbe_cbind_test_scalars;

static const cmeta_type_identity tbe_cbind_test_scalars_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.scalars");
static const cmeta_type_desc tbe_cbind_test_scalars_type = {
    "tbe_cbind_test_scalars", sizeof(tbe_cbind_test_scalars),
    _Alignof(tbe_cbind_test_scalars), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_scalars_identity};

#define TBE_CBIND_TEST_SCALAR_LAYOUT(member_, c_type_, type_desc_)          \
  {#member_, #c_type_, offsetof(tbe_cbind_test_scalars, member_),           \
   sizeof(c_type_), _Alignof(c_type_), (type_desc_), NULL}

enum { TBE_CBIND_TEST_SCALAR_UUID_INDEX = 11u };

static cmeta_field_desc tbe_cbind_test_scalars_layout_fields[] = {
    TBE_CBIND_TEST_SCALAR_LAYOUT(boolean, bool, &cmeta_type_bool),
    TBE_CBIND_TEST_SCALAR_LAYOUT(sint8, int8_t, &turbo_int8_cmeta_type),
    TBE_CBIND_TEST_SCALAR_LAYOUT(uint8, uint8_t, &turbo_uint8_cmeta_type),
    TBE_CBIND_TEST_SCALAR_LAYOUT(sint16, int16_t, &turbo_int16_cmeta_type),
    TBE_CBIND_TEST_SCALAR_LAYOUT(uint16, uint16_t, &turbo_uint16_cmeta_type),
    TBE_CBIND_TEST_SCALAR_LAYOUT(sint32, int32_t, &turbo_int32_cmeta_type),
    TBE_CBIND_TEST_SCALAR_LAYOUT(uint32, uint32_t, &turbo_uint32_cmeta_type),
    TBE_CBIND_TEST_SCALAR_LAYOUT(sint64, int64_t, &turbo_int64_cmeta_type),
    TBE_CBIND_TEST_SCALAR_LAYOUT(uint64, uint64_t, &turbo_uint64_cmeta_type),
    TBE_CBIND_TEST_SCALAR_LAYOUT(real32, float, &cmeta_type_float),
    TBE_CBIND_TEST_SCALAR_LAYOUT(real64, double, &cmeta_type_double),
    TBE_CBIND_TEST_SCALAR_LAYOUT(uuid, turbo_uuid_t, NULL)};

#undef TBE_CBIND_TEST_SCALAR_LAYOUT

static const cmeta_struct_desc tbe_cbind_test_scalars_layout = {
    "tbe_cbind_test_scalars", sizeof(tbe_cbind_test_scalars),
    _Alignof(tbe_cbind_test_scalars), tbe_cbind_test_scalars_layout_fields,
    sizeof(tbe_cbind_test_scalars_layout_fields) /
        sizeof(tbe_cbind_test_scalars_layout_fields[0])};

#define TBE_CBIND_TEST_SCALAR_DATA(member_, data_desc_)                    \
  {"test.tbe-cbind.scalars." #member_, #member_,                          \
   offsetof(tbe_cbind_test_scalars, member_), (data_desc_)}

static cmeta_data_field_desc tbe_cbind_test_scalars_data_fields[] = {
    TBE_CBIND_TEST_SCALAR_DATA(boolean, &cmeta_data_bool),
    TBE_CBIND_TEST_SCALAR_DATA(sint8, &turbo_int8_cmeta_data),
    TBE_CBIND_TEST_SCALAR_DATA(uint8, &turbo_uint8_cmeta_data),
    TBE_CBIND_TEST_SCALAR_DATA(sint16, &turbo_int16_cmeta_data),
    TBE_CBIND_TEST_SCALAR_DATA(uint16, &turbo_uint16_cmeta_data),
    TBE_CBIND_TEST_SCALAR_DATA(sint32, &turbo_int32_cmeta_data),
    TBE_CBIND_TEST_SCALAR_DATA(uint32, &turbo_uint32_cmeta_data),
    TBE_CBIND_TEST_SCALAR_DATA(sint64, &turbo_int64_cmeta_data),
    TBE_CBIND_TEST_SCALAR_DATA(uint64, &turbo_uint64_cmeta_data),
    TBE_CBIND_TEST_SCALAR_DATA(real32, &cmeta_data_float),
    TBE_CBIND_TEST_SCALAR_DATA(real64, &cmeta_data_double),
    TBE_CBIND_TEST_SCALAR_DATA(uuid, NULL)};

#undef TBE_CBIND_TEST_SCALAR_DATA

static const cmeta_data_struct_shape tbe_cbind_test_scalars_shape = {
    &tbe_cbind_test_scalars_layout, tbe_cbind_test_scalars_data_fields,
    sizeof(tbe_cbind_test_scalars_data_fields) /
        sizeof(tbe_cbind_test_scalars_data_fields[0])};
static const cmeta_data_desc tbe_cbind_test_scalars_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.scalars.data", "tbe_cbind_test_scalars",
    CMETA_DATA_STRUCT, &tbe_cbind_test_scalars_type,
    &tbe_cbind_test_scalars_shape, NULL};

static inline const cmeta_data_desc *tbe_cbind_test_scalars_data_get(void) {
  /* Core UUID objects are dllimport symbols on MSVC, so their addresses are
   * bound at test setup instead of used as C static initializer constants. */
  tbe_cbind_test_scalars_layout_fields[TBE_CBIND_TEST_SCALAR_UUID_INDEX].type =
      &turbo_uuid_cmeta_type;
  tbe_cbind_test_scalars_data_fields[TBE_CBIND_TEST_SCALAR_UUID_INDEX].value =
      &turbo_uuid_cmeta_data;
  return &tbe_cbind_test_scalars_data;
}

typedef struct tbe_cbind_test_scalar_envelope {
  tbe_cbind_test_scalars values;
} tbe_cbind_test_scalar_envelope;

static const cmeta_type_identity tbe_cbind_test_scalar_envelope_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.scalar-envelope");
static const cmeta_type_desc tbe_cbind_test_scalar_envelope_type = {
    "tbe_cbind_test_scalar_envelope", sizeof(tbe_cbind_test_scalar_envelope),
    _Alignof(tbe_cbind_test_scalar_envelope), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_scalar_envelope_identity};
static const cmeta_field_desc tbe_cbind_test_scalar_envelope_layout_fields[] = {{
    "values", "tbe_cbind_test_scalars",
    offsetof(tbe_cbind_test_scalar_envelope, values),
    sizeof(tbe_cbind_test_scalars), _Alignof(tbe_cbind_test_scalars),
    &tbe_cbind_test_scalars_type, NULL}};
static const cmeta_struct_desc tbe_cbind_test_scalar_envelope_layout = {
    "tbe_cbind_test_scalar_envelope", sizeof(tbe_cbind_test_scalar_envelope),
    _Alignof(tbe_cbind_test_scalar_envelope),
    tbe_cbind_test_scalar_envelope_layout_fields, 1u};
static const cmeta_data_field_desc
    tbe_cbind_test_scalar_envelope_data_fields[] = {{
        "test.tbe-cbind.scalar-envelope.values", "values",
        offsetof(tbe_cbind_test_scalar_envelope, values),
        &tbe_cbind_test_scalars_data}};
static const cmeta_data_struct_shape tbe_cbind_test_scalar_envelope_shape = {
    &tbe_cbind_test_scalar_envelope_layout,
    tbe_cbind_test_scalar_envelope_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_test_scalar_envelope_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.scalar-envelope.data", "tbe_cbind_test_scalar_envelope",
    CMETA_DATA_STRUCT, &tbe_cbind_test_scalar_envelope_type,
    &tbe_cbind_test_scalar_envelope_shape, NULL};

static inline const cmeta_data_desc *
tbe_cbind_test_scalar_envelope_data_get(void) {
  (void)tbe_cbind_test_scalars_data_get();
  return &tbe_cbind_test_scalar_envelope_data;
}

typedef struct tbe_cbind_test_inner {
  int quantity;
} tbe_cbind_test_inner;

typedef struct tbe_cbind_test_nested {
  tbe_cbind_test_inner detail;
  double score;
} tbe_cbind_test_nested;

static const cmeta_type_identity tbe_cbind_test_inner_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.inner");
static const cmeta_type_desc tbe_cbind_test_inner_type = {
    "tbe_cbind_test_inner", sizeof(tbe_cbind_test_inner),
    _Alignof(tbe_cbind_test_inner), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_inner_identity};
static const cmeta_field_desc tbe_cbind_test_inner_layout_fields[] = {
    {"quantity", "int", offsetof(tbe_cbind_test_inner, quantity), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc tbe_cbind_test_inner_layout = {
    "tbe_cbind_test_inner", sizeof(tbe_cbind_test_inner),
    _Alignof(tbe_cbind_test_inner), tbe_cbind_test_inner_layout_fields, 1u};
static const cmeta_data_field_desc tbe_cbind_test_inner_data_fields[] = {
    {"test.tbe-cbind.inner.quantity", "quantity",
     offsetof(tbe_cbind_test_inner, quantity), &cmeta_data_int}};
static const cmeta_data_struct_shape tbe_cbind_test_inner_shape = {
    &tbe_cbind_test_inner_layout, tbe_cbind_test_inner_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_test_inner_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.inner.data", "tbe_cbind_test_inner", CMETA_DATA_STRUCT,
    &tbe_cbind_test_inner_type, &tbe_cbind_test_inner_shape, NULL};

static const cmeta_type_identity tbe_cbind_test_nested_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.nested");
static const cmeta_type_desc tbe_cbind_test_nested_type = {
    "tbe_cbind_test_nested", sizeof(tbe_cbind_test_nested),
    _Alignof(tbe_cbind_test_nested), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_nested_identity};
static const cmeta_field_desc tbe_cbind_test_nested_layout_fields[] = {
    {"detail", "tbe_cbind_test_inner",
     offsetof(tbe_cbind_test_nested, detail), sizeof(tbe_cbind_test_inner),
     _Alignof(tbe_cbind_test_inner), &tbe_cbind_test_inner_type, NULL},
    {"score", "double", offsetof(tbe_cbind_test_nested, score), sizeof(double),
     _Alignof(double), &cmeta_type_double, NULL}};
static const cmeta_struct_desc tbe_cbind_test_nested_layout = {
    "tbe_cbind_test_nested", sizeof(tbe_cbind_test_nested),
    _Alignof(tbe_cbind_test_nested), tbe_cbind_test_nested_layout_fields, 2u};
static const cmeta_data_field_desc tbe_cbind_test_nested_data_fields[] = {
    {"test.tbe-cbind.nested.detail", "detail",
     offsetof(tbe_cbind_test_nested, detail), &tbe_cbind_test_inner_data},
    {"test.tbe-cbind.nested.score", "score",
     offsetof(tbe_cbind_test_nested, score), &cmeta_data_double}};
static const cmeta_data_struct_shape tbe_cbind_test_nested_shape = {
    &tbe_cbind_test_nested_layout, tbe_cbind_test_nested_data_fields, 2u};
static const cmeta_data_desc tbe_cbind_test_nested_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.nested.data", "tbe_cbind_test_nested", CMETA_DATA_STRUCT,
    &tbe_cbind_test_nested_type, &tbe_cbind_test_nested_shape, NULL};

static const cmeta_data_buffer_shape tbe_cbind_test_owned_string_shape = {
    CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_buffer_shape tbe_cbind_test_borrowed_string_shape = {
    CMETA_DATA_BUFFER_BORROWED};
static const cmeta_data_desc tbe_cbind_test_owned_string_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.string.owned", "owned string", CMETA_DATA_STRING,
    &turbo_tstr_cmeta_type, &tbe_cbind_test_owned_string_shape,
    &turbo_tstr_cmeta_buffer_ops};
static const cmeta_data_desc tbe_cbind_test_borrowed_string_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.string.borrowed", "borrowed string", CMETA_DATA_STRING,
    &turbo_vstr_cmeta_type, &tbe_cbind_test_borrowed_string_shape,
    &turbo_vstr_cmeta_buffer_ops};

typedef struct tbe_cbind_test_strings {
  tstr owned;
  vstr borrowed;
} tbe_cbind_test_strings;

static const cmeta_type_identity tbe_cbind_test_strings_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.strings");
static const cmeta_type_desc tbe_cbind_test_strings_type = {
    "tbe_cbind_test_strings", sizeof(tbe_cbind_test_strings),
    _Alignof(tbe_cbind_test_strings), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_strings_identity};
static const cmeta_field_desc tbe_cbind_test_strings_layout_fields[] = {
    {"owned", "tstr", offsetof(tbe_cbind_test_strings, owned), sizeof(tstr),
     _Alignof(tstr), &turbo_tstr_cmeta_type, NULL},
    {"borrowed", "vstr", offsetof(tbe_cbind_test_strings, borrowed),
     sizeof(vstr), _Alignof(vstr), &turbo_vstr_cmeta_type, NULL}};
static const cmeta_struct_desc tbe_cbind_test_strings_layout = {
    "tbe_cbind_test_strings", sizeof(tbe_cbind_test_strings),
    _Alignof(tbe_cbind_test_strings), tbe_cbind_test_strings_layout_fields, 2u};
static const cmeta_data_field_desc tbe_cbind_test_strings_data_fields[] = {
    {"test.tbe-cbind.strings.owned", "owned",
     offsetof(tbe_cbind_test_strings, owned), &tbe_cbind_test_owned_string_data},
    {"test.tbe-cbind.strings.borrowed", "borrowed",
     offsetof(tbe_cbind_test_strings, borrowed),
     &tbe_cbind_test_borrowed_string_data}};
static const cmeta_data_struct_shape tbe_cbind_test_strings_shape = {
    &tbe_cbind_test_strings_layout, tbe_cbind_test_strings_data_fields, 2u};
static const cmeta_data_desc tbe_cbind_test_strings_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.strings.data", "tbe_cbind_test_strings",
    CMETA_DATA_STRUCT, &tbe_cbind_test_strings_type,
    &tbe_cbind_test_strings_shape, NULL};

#endif /* TBE_CBIND_TEST_FIXTURES_H */
