#ifndef DATA_BIND_INTERNAL_H
#define DATA_BIND_INTERNAL_H

#include "data_bind.h"
#include "fmt.h"
#include <json_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum db_internal_storage_kind {
  DB_INTERNAL_STORAGE_SCALAR = 0,
  DB_INTERNAL_STORAGE_VEC,
  DB_INTERNAL_STORAGE_ORDERED_SET,
  DB_INTERNAL_STORAGE_ORDERED_MAP
} db_internal_storage_kind_t;

/* Borrows json for the duration of the call; the returned object owns its bound value tree. */
DataBindStatus data_bind_object_from_json_value(DataBind *codec, const char *type_name,
                                                json_value_t *json,
                                                DataBindObject **out_object,
                                                DataBindError *error);

/* White-box contract for in-tree storage migration tests; not installed. */
db_internal_storage_kind_t
data_bind_internal_storage_kind(const DataBindValue *value);

#ifdef __cplusplus
}
#endif

#endif
