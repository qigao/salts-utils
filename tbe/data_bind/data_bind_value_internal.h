#ifndef DATA_BIND_VALUE_INTERNAL_H
#define DATA_BIND_VALUE_INTERNAL_H

#include "data_bind.h"

#include <cstl.h>

typedef struct db_field_slot {
  char *name;
  DataBindValue *value;
} db_field_slot_t;

typedef struct db_owned_value_slot {
  DataBindValue *value;
} db_owned_value_slot_t;

typedef struct db_object_storage {
  vec_t fields;
} db_object_storage_t;

typedef struct db_sequence_storage {
  vec_t values;
} db_sequence_storage_t;

typedef struct data_bind_value_map_entry {
  char *key;
  DataBindValue *value;
} data_bind_value_map_entry_t;

typedef struct data_bind_value_map_array {
  data_bind_value_map_entry_t *items;
  size_t count;
  size_t capacity;
} data_bind_value_map_array_t;

typedef struct db_dynamic_graph db_dynamic_graph_t;

struct DataBindValue {
  size_t references;
  const cmeta_type_identity *type_identity;
  db_dynamic_graph_t *owned_graph;
  DataBindValueKind kind;
  union {
    int32_t int_val;
    int64_t int64_val;
    uint64_t uint64_val;
    double double_val;
    int bool_val;
    struct {
      char *ptr;
      size_t len;
    } string_val;
    struct {
      uint8_t *ptr;
      size_t len;
    } bytes_val;
    salts_uuid_t uuid_val;
    datetime_t datetime_val;
    DataBindDate date_val;
    DataBindTime time_val;
    int64_t duration_ms;
    DataBindDecimal decimal_val;
    struct {
      char *ptr;
    } bigint_val;
    DataBindMoney money_val;
    db_object_storage_t object;
    db_sequence_storage_t sequence;
    data_bind_value_map_array_t map_val;
  } data;
};

typedef enum db_internal_storage_kind {
  DB_INTERNAL_STORAGE_SCALAR = 0,
  DB_INTERNAL_STORAGE_VEC,
  DB_INTERNAL_STORAGE_ORDERED_SET,
  DB_INTERNAL_STORAGE_ORDERED_MAP
} db_internal_storage_kind_t;

static inline db_internal_storage_kind_t
data_bind_internal_storage_kind(const DataBindValue *value) {
  const vec_t *vec;
  size_t slot_size;
  if (value == NULL) return DB_INTERNAL_STORAGE_SCALAR;
  if (value->kind == DATA_BIND_VALUE_OBJECT) {
    vec = &value->data.object.fields;
    slot_size = sizeof(db_field_slot_t);
  } else if (value->kind == DATA_BIND_VALUE_LIST ||
             value->kind == DATA_BIND_VALUE_SET) {
    vec = &value->data.sequence.values;
    slot_size = sizeof(db_owned_value_slot_t);
  } else {
    return DB_INTERNAL_STORAGE_SCALAR;
  }
  if (!vec->initialized || vec->cmeta.descriptor != &stl_vec_container_desc ||
      vec->element_type == NULL || vec->element_type->size != slot_size ||
      cmeta_type_require_traits(vec->element_type,
                                CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                                    CMETA_TRAIT_DESTROY) != CMETA_OK)
    return DB_INTERNAL_STORAGE_SCALAR;
  return DB_INTERNAL_STORAGE_VEC;
}

#endif
