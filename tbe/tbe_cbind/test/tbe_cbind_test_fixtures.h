#ifndef TBE_CBIND_TEST_FIXTURES_H
#define TBE_CBIND_TEST_FIXTURES_H

#include <cmeta/data.h>
#include "turbo_cmeta_data.h"
#include "turbo_str.h"
#include "turbo_vstr.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

typedef int16_t tbe_cbind_test_state;

enum {
  TBE_CBIND_TEST_STATE_IDLE = 1,
  TBE_CBIND_TEST_STATE_READY = 2,
  TBE_CBIND_TEST_STATE_PAUSED = 7
};

static bool tbe_cbind_test_state_is_zero(const void *object) {
  tbe_cbind_test_state value;
  if (object == NULL) return false;
  memcpy(&value, object, sizeof(value));
  return value == 0;
}

static cmeta_status tbe_cbind_test_state_read(const void *object,
                                              int64_t *out) {
  tbe_cbind_test_state value;
  if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  memcpy(&value, object, sizeof(value));
  *out = (int64_t)value;
  return CMETA_OK;
}

static cmeta_status tbe_cbind_test_state_assign(void *object, int64_t value) {
  tbe_cbind_test_state native;
  if (object == NULL || value < INT16_MIN || value > INT16_MAX)
    return CMETA_INVALID_ARGUMENT;
  native = (tbe_cbind_test_state)value;
  memcpy(object, &native, sizeof(native));
  return CMETA_OK;
}

static void tbe_cbind_test_state_restore_zero(void *object) {
  const tbe_cbind_test_state zero = 0;
  if (object != NULL) memcpy(object, &zero, sizeof(zero));
}

static const cmeta_enum_item_desc tbe_cbind_test_state_items[] = {
    {TBE_CBIND_TEST_STATE_IDLE, "State_Idle", "Idle"},
    {TBE_CBIND_TEST_STATE_READY, "State_Ready", "Ready"},
    {TBE_CBIND_TEST_STATE_PAUSED, "State_Paused", "Paused"}};
static const cmeta_enum_desc tbe_cbind_test_state_meta = {
    "State", tbe_cbind_test_state_items,
    sizeof(tbe_cbind_test_state_items) /
        sizeof(tbe_cbind_test_state_items[0])};
static const cmeta_data_enum_shape tbe_cbind_test_state_shape = {
    &tbe_cbind_test_state_meta};
static const cmeta_data_enum_ops tbe_cbind_test_state_ops = {
    sizeof(cmeta_data_enum_ops), CMETA_DATA_ENUM_OPS_ABI_VERSION,
    &turbo_int16_cmeta_type, tbe_cbind_test_state_is_zero,
    tbe_cbind_test_state_read, tbe_cbind_test_state_assign,
    tbe_cbind_test_state_restore_zero};
static const cmeta_data_desc tbe_cbind_test_state_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.State.data",
    .display_name = "State",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &turbo_int16_cmeta_type,
    .shape = &tbe_cbind_test_state_shape,
    .enum_ops = &tbe_cbind_test_state_ops};

typedef struct tbe_cbind_test_enum_detail {
  int32_t prefix;
  tbe_cbind_test_state state;
} tbe_cbind_test_enum_detail;

static const cmeta_type_identity tbe_cbind_test_enum_detail_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.enum-detail");
static const cmeta_type_desc tbe_cbind_test_enum_detail_type = {
    "tbe_cbind_test_enum_detail", sizeof(tbe_cbind_test_enum_detail),
    _Alignof(tbe_cbind_test_enum_detail), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_enum_detail_identity};
static const cmeta_field_desc tbe_cbind_test_enum_detail_layout_fields[] = {
    {"prefix", "int32_t", offsetof(tbe_cbind_test_enum_detail, prefix),
     sizeof(int32_t), _Alignof(int32_t), &turbo_int32_cmeta_type, NULL},
    {"state", "tbe_cbind_test_state",
     offsetof(tbe_cbind_test_enum_detail, state), sizeof(tbe_cbind_test_state),
     _Alignof(tbe_cbind_test_state), &turbo_int16_cmeta_type, NULL}};
static const cmeta_struct_desc tbe_cbind_test_enum_detail_layout = {
    "tbe_cbind_test_enum_detail", sizeof(tbe_cbind_test_enum_detail),
    _Alignof(tbe_cbind_test_enum_detail),
    tbe_cbind_test_enum_detail_layout_fields, 2u};
static const cmeta_data_field_desc
    tbe_cbind_test_enum_detail_data_fields[] = {
        {"test.tbe-cbind.enum-detail.prefix", "prefix",
         offsetof(tbe_cbind_test_enum_detail, prefix),
         &turbo_int32_cmeta_data},
        {"test.tbe-cbind.enum-detail.state", "state",
         offsetof(tbe_cbind_test_enum_detail, state),
         &tbe_cbind_test_state_data}};
static const cmeta_data_struct_shape tbe_cbind_test_enum_detail_shape = {
    &tbe_cbind_test_enum_detail_layout,
    tbe_cbind_test_enum_detail_data_fields, 2u};
static const cmeta_data_desc tbe_cbind_test_enum_detail_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.enum-detail.data",
    .display_name = "tbe_cbind_test_enum_detail",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &tbe_cbind_test_enum_detail_type,
    .shape = &tbe_cbind_test_enum_detail_shape};

typedef struct tbe_cbind_test_enum_envelope {
  tbe_cbind_test_enum_detail detail;
  int32_t suffix;
} tbe_cbind_test_enum_envelope;

static const cmeta_type_identity tbe_cbind_test_enum_envelope_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.enum-envelope");
static const cmeta_type_desc tbe_cbind_test_enum_envelope_type = {
    "tbe_cbind_test_enum_envelope", sizeof(tbe_cbind_test_enum_envelope),
    _Alignof(tbe_cbind_test_enum_envelope), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_enum_envelope_identity};
static const cmeta_field_desc tbe_cbind_test_enum_envelope_layout_fields[] = {
    {"detail", "tbe_cbind_test_enum_detail",
     offsetof(tbe_cbind_test_enum_envelope, detail),
     sizeof(tbe_cbind_test_enum_detail), _Alignof(tbe_cbind_test_enum_detail),
     &tbe_cbind_test_enum_detail_type, NULL},
    {"suffix", "int32_t", offsetof(tbe_cbind_test_enum_envelope, suffix),
     sizeof(int32_t), _Alignof(int32_t), &turbo_int32_cmeta_type, NULL}};
static const cmeta_struct_desc tbe_cbind_test_enum_envelope_layout = {
    "tbe_cbind_test_enum_envelope", sizeof(tbe_cbind_test_enum_envelope),
    _Alignof(tbe_cbind_test_enum_envelope),
    tbe_cbind_test_enum_envelope_layout_fields, 2u};
static const cmeta_data_field_desc
    tbe_cbind_test_enum_envelope_data_fields[] = {
        {"test.tbe-cbind.enum-envelope.detail", "detail",
         offsetof(tbe_cbind_test_enum_envelope, detail),
         &tbe_cbind_test_enum_detail_data},
        {"test.tbe-cbind.enum-envelope.suffix", "suffix",
         offsetof(tbe_cbind_test_enum_envelope, suffix),
         &turbo_int32_cmeta_data}};
static const cmeta_data_struct_shape tbe_cbind_test_enum_envelope_shape = {
    &tbe_cbind_test_enum_envelope_layout,
    tbe_cbind_test_enum_envelope_data_fields, 2u};
static const cmeta_data_desc tbe_cbind_test_enum_envelope_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.enum-envelope.data",
    .display_name = "tbe_cbind_test_enum_envelope",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &tbe_cbind_test_enum_envelope_type,
    .shape = &tbe_cbind_test_enum_envelope_shape};

static const char tbe_cbind_test_state_record_schema[] =
    "enum State <i16> { Idle = 1; Ready; Paused = 7; } "
    "message EnumDetail { int32 prefix; State state; }";
static const char tbe_cbind_test_state_envelope_schema[] =
    "enum State <int16> { Idle = 1; Ready; Paused = 7; } "
    "composite EnumDetail { int32 prefix; State state; } "
    "message EnumEnvelope { EnumDetail detail; int32 suffix; }";

typedef int32_t tbe_cbind_test_mode;

enum {
  TBE_CBIND_TEST_MODE_UNKNOWN = 0,
  TBE_CBIND_TEST_MODE_BUSY = 4,
  TBE_CBIND_TEST_MODE_DONE = 5
};

static bool tbe_cbind_test_mode_is_zero(const void *object) {
  tbe_cbind_test_mode value;
  if (object == NULL) return false;
  memcpy(&value, object, sizeof(value));
  return value == 0;
}

static cmeta_status tbe_cbind_test_mode_read(const void *object,
                                             int64_t *out) {
  tbe_cbind_test_mode value;
  if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  memcpy(&value, object, sizeof(value));
  *out = (int64_t)value;
  return CMETA_OK;
}

static cmeta_status tbe_cbind_test_mode_assign(void *object, int64_t value) {
  tbe_cbind_test_mode native;
  if (object == NULL || value < INT32_MIN || value > INT32_MAX)
    return CMETA_INVALID_ARGUMENT;
  native = (tbe_cbind_test_mode)value;
  memcpy(object, &native, sizeof(native));
  return CMETA_OK;
}

static void tbe_cbind_test_mode_restore_zero(void *object) {
  const tbe_cbind_test_mode zero = 0;
  if (object != NULL) memcpy(object, &zero, sizeof(zero));
}

static const cmeta_enum_item_desc tbe_cbind_test_mode_items[] = {
    {TBE_CBIND_TEST_MODE_UNKNOWN, "Mode_Unknown", "Unknown"},
    {TBE_CBIND_TEST_MODE_BUSY, "Mode_Busy", "Busy"},
    {TBE_CBIND_TEST_MODE_DONE, "Mode_Done", "Done"}};
static const cmeta_enum_desc tbe_cbind_test_mode_meta = {
    "Mode", tbe_cbind_test_mode_items,
    sizeof(tbe_cbind_test_mode_items) /
        sizeof(tbe_cbind_test_mode_items[0])};
static const cmeta_data_enum_shape tbe_cbind_test_mode_shape = {
    &tbe_cbind_test_mode_meta};
static const cmeta_data_enum_ops tbe_cbind_test_mode_ops = {
    sizeof(cmeta_data_enum_ops), CMETA_DATA_ENUM_OPS_ABI_VERSION,
    &turbo_int32_cmeta_type, tbe_cbind_test_mode_is_zero,
    tbe_cbind_test_mode_read, tbe_cbind_test_mode_assign,
    tbe_cbind_test_mode_restore_zero};
static const cmeta_data_desc tbe_cbind_test_mode_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.Mode.data",
    .display_name = "Mode",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &turbo_int32_cmeta_type,
    .shape = &tbe_cbind_test_mode_shape,
    .enum_ops = &tbe_cbind_test_mode_ops};

typedef struct tbe_cbind_test_mode_record {
  tbe_cbind_test_mode mode;
} tbe_cbind_test_mode_record;

static const cmeta_type_identity tbe_cbind_test_mode_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.mode-record");
static const cmeta_type_desc tbe_cbind_test_mode_record_type = {
    "tbe_cbind_test_mode_record", sizeof(tbe_cbind_test_mode_record),
    _Alignof(tbe_cbind_test_mode_record), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_test_mode_record_identity};
static const cmeta_field_desc tbe_cbind_test_mode_record_layout_fields[] = {{
    "mode", "tbe_cbind_test_mode",
    offsetof(tbe_cbind_test_mode_record, mode), sizeof(tbe_cbind_test_mode),
    _Alignof(tbe_cbind_test_mode), &turbo_int32_cmeta_type, NULL}};
static const cmeta_struct_desc tbe_cbind_test_mode_record_layout = {
    "tbe_cbind_test_mode_record", sizeof(tbe_cbind_test_mode_record),
    _Alignof(tbe_cbind_test_mode_record),
    tbe_cbind_test_mode_record_layout_fields, 1u};
static const cmeta_data_field_desc
    tbe_cbind_test_mode_record_data_fields[] = {{
        "test.tbe-cbind.mode-record.mode", "mode",
        offsetof(tbe_cbind_test_mode_record, mode),
        &tbe_cbind_test_mode_data}};
static const cmeta_data_struct_shape tbe_cbind_test_mode_record_shape = {
    &tbe_cbind_test_mode_record_layout,
    tbe_cbind_test_mode_record_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_test_mode_record_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tbe-cbind.mode-record.data",
    .display_name = "tbe_cbind_test_mode_record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &tbe_cbind_test_mode_record_type,
    .shape = &tbe_cbind_test_mode_record_shape};

static const char tbe_cbind_test_mode_schema[] =
    "enum Mode { Unknown; Busy = 4; Done; } "
    "message ModeRecord { Mode mode; }";

#endif /* TBE_CBIND_TEST_FIXTURES_H */
