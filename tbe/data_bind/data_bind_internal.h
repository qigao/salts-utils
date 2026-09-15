#ifndef DATA_BIND_INTERNAL_H
#define DATA_BIND_INTERNAL_H

#include "data_bind.h"
#include "data_bind_value_internal.h"
#include "fmt.h"
#include <json_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrows json for the duration of the call; the returned object owns its bound value tree. */
DataBindStatus data_bind_object_from_json_value(DataBind *codec, const char *type_name,
                                                json_value_t *json,
                                                DataBindObject **out_object,
                                                DataBindError *error);

/* Borrows the schema and JSON object; applies the schema's input name/aliases. */
json_value_t *data_bind_internal_json_field_value(
    DataBind *codec, const char *type_name, size_t field_index,
    const json_value_t *object);
const char *data_bind_internal_json_field_output_name(
    DataBind *codec, const char *type_name, size_t field_index);

/* Test-only seam: advances container generation without changing its contents. */
DATA_BIND_API DataBindStatus
data_bind_internal_test_touch_generation(DataBindValue *value);

#ifdef __cplusplus
}
#endif

#endif
