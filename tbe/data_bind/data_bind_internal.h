#ifndef DATA_BIND_INTERNAL_H
#define DATA_BIND_INTERNAL_H

#include "data_bind.h"
#include "fmt.h"
#include "turbo_parser_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Borrows json for the duration of the call; the returned object owns its bound value tree. */
DataBindStatus data_bind_object_from_json_value(DataBind *codec, const char *type_name,
                                                json_value_t *json,
                                                DataBindObject **out_object,
                                                DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif
