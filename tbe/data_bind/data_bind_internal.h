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

/* Test-only seam: advances container generation without changing its contents. */
DATA_BIND_API DataBindStatus
data_bind_internal_test_touch_generation(DataBindValue *value);

#ifdef __cplusplus
}
#endif

#endif
