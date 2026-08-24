#ifndef TBE_CBIND_MULTITU_FIXTURE_H
#define TBE_CBIND_MULTITU_FIXTURE_H

#include <cmeta/data.h>
#include "turbo_cmeta_data.h"
#include "turbo_str.h"

#include <stddef.h>

typedef struct tbe_cbind_multitu_text {
  int value;
} tbe_cbind_multitu_text;

typedef struct tbe_cbind_multitu_pair {
  tbe_cbind_multitu_text left;
  tbe_cbind_multitu_text right;
} tbe_cbind_multitu_pair;

static const cmeta_type_identity tbe_cbind_multitu_text_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.multitu-text");
static const cmeta_type_desc tbe_cbind_multitu_text_type = {
    "tbe_cbind_multitu_text", sizeof(tbe_cbind_multitu_text),
    _Alignof(tbe_cbind_multitu_text), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_multitu_text_identity};
static const cmeta_field_desc tbe_cbind_multitu_text_layout_fields[] = {
    {"value", "int", offsetof(tbe_cbind_multitu_text, value), sizeof(int),
     _Alignof(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc tbe_cbind_multitu_text_layout = {
    "tbe_cbind_multitu_text", sizeof(tbe_cbind_multitu_text),
    _Alignof(tbe_cbind_multitu_text), tbe_cbind_multitu_text_layout_fields,
    1u};
static const cmeta_data_field_desc tbe_cbind_multitu_text_data_fields[] = {
    {"test.tbe-cbind.multitu-text.value", "value",
     offsetof(tbe_cbind_multitu_text, value), &cmeta_data_int}};
static const cmeta_data_struct_shape tbe_cbind_multitu_text_shape = {
    &tbe_cbind_multitu_text_layout, tbe_cbind_multitu_text_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_multitu_text_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.multitu-text.data", "tbe_cbind_multitu_text",
    CMETA_DATA_STRUCT, &tbe_cbind_multitu_text_type,
    &tbe_cbind_multitu_text_shape, NULL};

static const cmeta_type_identity tbe_cbind_multitu_pair_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.multitu-pair");
static const cmeta_type_desc tbe_cbind_multitu_pair_type = {
    "tbe_cbind_multitu_pair", sizeof(tbe_cbind_multitu_pair),
    _Alignof(tbe_cbind_multitu_pair), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_multitu_pair_identity};
static const cmeta_field_desc tbe_cbind_multitu_pair_layout_fields[] = {
    {"left", "tbe_cbind_multitu_text",
     offsetof(tbe_cbind_multitu_pair, left), sizeof(tbe_cbind_multitu_text),
     _Alignof(tbe_cbind_multitu_text), &tbe_cbind_multitu_text_type, NULL},
    {"right", "tbe_cbind_multitu_text",
     offsetof(tbe_cbind_multitu_pair, right), sizeof(tbe_cbind_multitu_text),
     _Alignof(tbe_cbind_multitu_text), &tbe_cbind_multitu_text_type, NULL}};
static const cmeta_struct_desc tbe_cbind_multitu_pair_layout = {
    "tbe_cbind_multitu_pair", sizeof(tbe_cbind_multitu_pair),
    _Alignof(tbe_cbind_multitu_pair), tbe_cbind_multitu_pair_layout_fields,
    2u};
static const cmeta_data_field_desc tbe_cbind_multitu_pair_data_fields[] = {
    {"test.tbe-cbind.multitu-pair.left", "left",
     offsetof(tbe_cbind_multitu_pair, left), &tbe_cbind_multitu_text_data},
    {"test.tbe-cbind.multitu-pair.right", "right",
     offsetof(tbe_cbind_multitu_pair, right), &tbe_cbind_multitu_text_data}};
static const cmeta_data_struct_shape tbe_cbind_multitu_pair_shape = {
    &tbe_cbind_multitu_pair_layout, tbe_cbind_multitu_pair_data_fields, 2u};
static const cmeta_data_desc tbe_cbind_multitu_pair_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.multitu-pair.data", "tbe_cbind_multitu_pair",
    CMETA_DATA_STRUCT, &tbe_cbind_multitu_pair_type,
    &tbe_cbind_multitu_pair_shape, NULL};

const cmeta_data_desc *tbe_cbind_multitu_external_text_data(void);

typedef struct tbe_cbind_multitu_string_text {
  tstr value;
} tbe_cbind_multitu_string_text;

typedef struct tbe_cbind_multitu_string_pair {
  tbe_cbind_multitu_string_text left;
  tbe_cbind_multitu_string_text right;
} tbe_cbind_multitu_string_pair;

static const cmeta_data_buffer_shape
    tbe_cbind_multitu_owned_string_shape = {CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_desc tbe_cbind_multitu_owned_string_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.multitu-owned-string.data", "owned string",
    CMETA_DATA_STRING, &turbo_tstr_cmeta_type,
    &tbe_cbind_multitu_owned_string_shape, &turbo_tstr_cmeta_buffer_ops};

static const cmeta_type_identity tbe_cbind_multitu_string_text_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.multitu-string-text");
static const cmeta_type_desc tbe_cbind_multitu_string_text_type = {
    "tbe_cbind_multitu_string_text", sizeof(tbe_cbind_multitu_string_text),
    _Alignof(tbe_cbind_multitu_string_text), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_multitu_string_text_identity};
static const cmeta_field_desc
    tbe_cbind_multitu_string_text_layout_fields[] = {{
        "value", "tstr", offsetof(tbe_cbind_multitu_string_text, value),
        sizeof(tstr), _Alignof(tstr), &turbo_tstr_cmeta_type, NULL}};
static const cmeta_struct_desc tbe_cbind_multitu_string_text_layout = {
    "tbe_cbind_multitu_string_text", sizeof(tbe_cbind_multitu_string_text),
    _Alignof(tbe_cbind_multitu_string_text),
    tbe_cbind_multitu_string_text_layout_fields, 1u};
static const cmeta_data_field_desc
    tbe_cbind_multitu_string_text_data_fields[] = {{
        "test.tbe-cbind.multitu-string-text.value", "value",
        offsetof(tbe_cbind_multitu_string_text, value),
        &tbe_cbind_multitu_owned_string_data}};
static const cmeta_data_struct_shape tbe_cbind_multitu_string_text_shape = {
    &tbe_cbind_multitu_string_text_layout,
    tbe_cbind_multitu_string_text_data_fields, 1u};
static const cmeta_data_desc tbe_cbind_multitu_string_text_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.multitu-string-text.data",
    "tbe_cbind_multitu_string_text", CMETA_DATA_STRUCT,
    &tbe_cbind_multitu_string_text_type,
    &tbe_cbind_multitu_string_text_shape, NULL};

static const cmeta_type_identity tbe_cbind_multitu_string_pair_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.multitu-string-pair");
static const cmeta_type_desc tbe_cbind_multitu_string_pair_type = {
    "tbe_cbind_multitu_string_pair", sizeof(tbe_cbind_multitu_string_pair),
    _Alignof(tbe_cbind_multitu_string_pair), CMETA_T_OBJECT, NULL, NULL,
    &tbe_cbind_multitu_string_pair_identity};
static const cmeta_field_desc
    tbe_cbind_multitu_string_pair_layout_fields[] = {
        {"left", "tbe_cbind_multitu_string_text",
         offsetof(tbe_cbind_multitu_string_pair, left),
         sizeof(tbe_cbind_multitu_string_text),
         _Alignof(tbe_cbind_multitu_string_text),
         &tbe_cbind_multitu_string_text_type, NULL},
        {"right", "tbe_cbind_multitu_string_text",
         offsetof(tbe_cbind_multitu_string_pair, right),
         sizeof(tbe_cbind_multitu_string_text),
         _Alignof(tbe_cbind_multitu_string_text),
         &tbe_cbind_multitu_string_text_type, NULL}};
static const cmeta_struct_desc tbe_cbind_multitu_string_pair_layout = {
    "tbe_cbind_multitu_string_pair", sizeof(tbe_cbind_multitu_string_pair),
    _Alignof(tbe_cbind_multitu_string_pair),
    tbe_cbind_multitu_string_pair_layout_fields, 2u};
static const cmeta_data_field_desc
    tbe_cbind_multitu_string_pair_data_fields[] = {
        {"test.tbe-cbind.multitu-string-pair.left", "left",
         offsetof(tbe_cbind_multitu_string_pair, left),
         &tbe_cbind_multitu_string_text_data},
        {"test.tbe-cbind.multitu-string-pair.right", "right",
         offsetof(tbe_cbind_multitu_string_pair, right),
         &tbe_cbind_multitu_string_text_data}};
static const cmeta_data_struct_shape tbe_cbind_multitu_string_pair_shape = {
    &tbe_cbind_multitu_string_pair_layout,
    tbe_cbind_multitu_string_pair_data_fields, 2u};
static const cmeta_data_desc tbe_cbind_multitu_string_pair_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.tbe-cbind.multitu-string-pair.data",
    "tbe_cbind_multitu_string_pair", CMETA_DATA_STRUCT,
    &tbe_cbind_multitu_string_pair_type,
    &tbe_cbind_multitu_string_pair_shape, NULL};

const cmeta_data_desc *tbe_cbind_multitu_external_string_text_data(void);

#endif /* TBE_CBIND_MULTITU_FIXTURE_H */
