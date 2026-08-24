#ifndef TBE_CBIND_TEST_FIXTURES_H
#define TBE_CBIND_TEST_FIXTURES_H

#include <cmeta/data.h>
#include "turbo_cmeta_data.h"
#include "turbo_str.h"
#include "turbo_vstr.h"

#include <stddef.h>

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
