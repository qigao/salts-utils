#ifndef DATA_BIND_VALUE_INTERNAL_H
#define DATA_BIND_VALUE_INTERNAL_H

#include "data_bind.h"

#include <cstl.h>
#include <string.h>

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

typedef struct db_value_ref_key {
  const DataBindValue *value;
} db_value_ref_key_t;

typedef struct db_set_storage {
  vec_t ordered_values;
  hash_set_t membership;
  uint64_t generation;
} db_set_storage_t;

typedef struct db_map_entry_slot {
  DataBindValue *key_value;
  char *public_key_text;
  DataBindValue *value;
} db_map_entry_slot_t;

typedef struct db_map_index_value {
  size_t ordered_index;
} db_map_index_value_t;

typedef struct db_map_storage {
  vec_t ordered_entries;
  hash_map_t index;
  uint64_t generation;
} db_map_storage_t;

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
    db_set_storage_t set;
    db_map_storage_t map;
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
  } else if (value->kind == DATA_BIND_VALUE_LIST) {
    vec = &value->data.sequence.values;
    slot_size = sizeof(db_owned_value_slot_t);
  } else if (value->kind == DATA_BIND_VALUE_SET) {
    const hash_set_t *membership = &value->data.set.membership;
    vec = &value->data.set.ordered_values;
    slot_size = sizeof(db_owned_value_slot_t);
    if (membership->cmeta.descriptor == NULL ||
        membership->cmeta.descriptor->name == NULL ||
        strcmp(membership->cmeta.descriptor->name, stl_hash_set_container_desc.name) != 0 ||
        membership->element_type == NULL ||
        membership->element_type->size != sizeof(db_value_ref_key_t) ||
        cmeta_type_require_traits(membership->element_type,
                                  CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH |
                                      CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                                      CMETA_TRAIT_DESTROY) != CMETA_OK ||
        !membership->table.initialized)
      return DB_INTERNAL_STORAGE_SCALAR;
  } else if (value->kind == DATA_BIND_VALUE_MAP) {
    const hash_map_t *index = &value->data.map.index;
    vec = &value->data.map.ordered_entries;
    slot_size = sizeof(db_map_entry_slot_t);
    if (index->cmeta.descriptor == NULL ||
        index->cmeta.descriptor->name == NULL ||
        strcmp(index->cmeta.descriptor->name, stl_hash_map_container_desc.name) != 0 ||
        index->key_type == NULL ||
        index->key_type->size != sizeof(db_value_ref_key_t) ||
        cmeta_type_require_traits(index->key_type,
                                  CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH |
                                      CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                                      CMETA_TRAIT_DESTROY) != CMETA_OK ||
        index->value_type == NULL ||
        index->value_type->size != sizeof(db_map_index_value_t) ||
        cmeta_type_require_traits(index->value_type,
                                  CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                                      CMETA_TRAIT_DESTROY) != CMETA_OK ||
        !index->initialized)
      return DB_INTERNAL_STORAGE_SCALAR;
  } else {
    return DB_INTERNAL_STORAGE_SCALAR;
  }
  if (!vec->initialized || vec->cmeta.descriptor == NULL ||
      vec->cmeta.descriptor->name == NULL ||
      strcmp(vec->cmeta.descriptor->name, stl_vec_container_desc.name) != 0 ||
      vec->element_type == NULL || vec->element_type->size != slot_size ||
      cmeta_type_require_traits(vec->element_type,
                                CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                                    CMETA_TRAIT_DESTROY) != CMETA_OK)
    return DB_INTERNAL_STORAGE_SCALAR;
  if (value->kind == DATA_BIND_VALUE_SET) return DB_INTERNAL_STORAGE_ORDERED_SET;
  if (value->kind == DATA_BIND_VALUE_MAP) return DB_INTERNAL_STORAGE_ORDERED_MAP;
  return DB_INTERNAL_STORAGE_VEC;
}

/* Private range-version query keeps CMeta adapters independent of value layout. */
#ifdef __cplusplus
extern "C" {
#endif
DATA_BIND_API uint64_t data_bind_value_generation(const DataBindValue *value);
#ifdef __cplusplus
}
#endif

#endif
