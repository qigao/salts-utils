#include "data_bind_cmeta.h"
#include "data_bind_value_internal.h"

#include <cmeta/pp.h>

#include <string.h>

#define DATA_BIND_CMETA_REF_TYPES(M)                                                      \
  Schema(M,                                                                               \
         (value_ref, DataBindValueRef, "DataBindValueRef",                               \
          "turbo_parser.data_bind.ValueRef.v1"),                                         \
         (field_ref, DataBindFieldRef, "DataBindFieldRef",                               \
          "turbo_parser.data_bind.FieldRef.v1"),                                         \
         (map_entry_ref, DataBindMapEntryRef, "DataBindMapEntryRef",                     \
          "turbo_parser.data_bind.MapEntryRef.v1"))

static const cmeta_type_traits data_bind_cmeta_ref_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY};

#define DATA_BIND_CMETA_DEFINE_IDENTITY(symbol, type, display_name, stable_id) \
  static const cmeta_type_identity data_bind_cmeta_##symbol##_identity =        \
      CMETA_TYPE_ID_ATOM_INIT(stable_id);
Replay(DATA_BIND_CMETA_REF_TYPES, DATA_BIND_CMETA_DEFINE_IDENTITY)
#undef DATA_BIND_CMETA_DEFINE_IDENTITY

#define DATA_BIND_CMETA_DEFINE_DESCRIPTOR(symbol, type, display_name, stable_id) \
  static const cmeta_type_desc data_bind_cmeta_##symbol##_descriptor = {          \
      .name = display_name,                                                       \
      .size = sizeof(type),                                                       \
      .align = _Alignof(type),                                                    \
      .kind = CMETA_T_OBJECT,                                                     \
      .pointee = NULL,                                                            \
      .traits = &data_bind_cmeta_ref_traits,                                      \
      .identity = &data_bind_cmeta_##symbol##_identity};
Replay(DATA_BIND_CMETA_REF_TYPES, DATA_BIND_CMETA_DEFINE_DESCRIPTOR)
#undef DATA_BIND_CMETA_DEFINE_DESCRIPTOR

#define DATA_BIND_CMETA_DEFINE_GETTER(symbol, type, display_name, stable_id) \
  const cmeta_type_desc *data_bind_cmeta_##symbol##_type(void) {             \
    return &data_bind_cmeta_##symbol##_descriptor;                           \
  }
Replay(DATA_BIND_CMETA_REF_TYPES, DATA_BIND_CMETA_DEFINE_GETTER)
#undef DATA_BIND_CMETA_DEFINE_GETTER

DataBindStatus data_bind_cmeta_data_kind(DataBindValueKind value_kind,
                                         cmeta_data_kind *out_kind) {
  cmeta_data_kind data_kind;

  if (out_kind == NULL) return DATA_BIND_ERR_INVALID_ARG;

  switch (value_kind) {
  case DATA_BIND_VALUE_OBJECT:
    data_kind = CMETA_DATA_STRUCT;
    break;
  case DATA_BIND_VALUE_LIST:
    data_kind = CMETA_DATA_SEQUENCE;
    break;
  case DATA_BIND_VALUE_SET:
    data_kind = CMETA_DATA_SET;
    break;
  case DATA_BIND_VALUE_MAP:
    data_kind = CMETA_DATA_MAP;
    break;
  case DATA_BIND_VALUE_INT:
  case DATA_BIND_VALUE_INT64:
    data_kind = CMETA_DATA_SINT;
    break;
  case DATA_BIND_VALUE_UINT64:
    data_kind = CMETA_DATA_UINT;
    break;
  case DATA_BIND_VALUE_DOUBLE:
    data_kind = CMETA_DATA_FLOAT;
    break;
  case DATA_BIND_VALUE_BOOL:
    data_kind = CMETA_DATA_BOOL;
    break;
  case DATA_BIND_VALUE_STRING:
    data_kind = CMETA_DATA_STRING;
    break;
  case DATA_BIND_VALUE_BYTES:
    data_kind = CMETA_DATA_BYTES;
    break;
  case DATA_BIND_VALUE_UUID:
  case DATA_BIND_VALUE_DATETIME:
  case DATA_BIND_VALUE_DATE:
  case DATA_BIND_VALUE_TIME:
  case DATA_BIND_VALUE_DURATION:
  case DATA_BIND_VALUE_DECIMAL:
  case DATA_BIND_VALUE_BIGINT:
  case DATA_BIND_VALUE_MONEY:
    data_kind = CMETA_DATA_CUSTOM;
    break;
  case DATA_BIND_VALUE_NULL:
    return DATA_BIND_ERR_TYPE_MISMATCH;
  default:
    return DATA_BIND_ERR_INVALID_ARG;
  }

  *out_kind = data_kind;
  return DATA_BIND_OK;
}

static size_t data_bind_cmeta_values_size(const void *object) {
  return data_bind_value_count((const DataBindValue *)object);
}

static size_t data_bind_cmeta_fields_size(const void *object) {
  return data_bind_value_field_count((const DataBindValue *)object);
}

static uint64_t data_bind_cmeta_container_version(const void *object) {
  const DataBindValue *owner = (const DataBindValue *)object;
  if (owner == NULL) return UINT64_C(0);
  switch (owner->kind) {
  case DATA_BIND_VALUE_OBJECT:
    return vec_generation(&owner->data.object.fields);
  case DATA_BIND_VALUE_LIST:
    return vec_generation(&owner->data.sequence.values);
  case DATA_BIND_VALUE_SET:
    return owner->data.set.generation;
  case DATA_BIND_VALUE_MAP:
    return owner->data.map.generation;
  default:
    return UINT64_C(0);
  }
}

static cmeta_gen_status data_bind_cmeta_values_next(const void *object,
                                                     cmeta_range_cursor *cursor,
                                                     void *out_value) {
  const DataBindValue *owner = (const DataBindValue *)object;
  DataBindValueRef ref;
  size_t count;

  if (owner == NULL || cursor == NULL || out_value == NULL) return CMETA_GEN_ERROR;
  count = data_bind_value_count(owner);
  if (cursor->index >= count) return CMETA_GEN_DONE;
  ref.value = data_bind_value_at(owner, cursor->index);
  if (ref.value == NULL) return CMETA_GEN_ERROR;
  memcpy(out_value, &ref, sizeof(ref));
  ++cursor->index;
  return cursor->index == count ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static cmeta_gen_status data_bind_cmeta_fields_next(const void *object,
                                                     cmeta_range_cursor *cursor,
                                                     void *out_value) {
  const DataBindValue *owner = (const DataBindValue *)object;
  DataBindFieldRef ref;
  size_t count;

  if (owner == NULL || cursor == NULL || out_value == NULL) return CMETA_GEN_ERROR;
  count = data_bind_value_field_count(owner);
  if (cursor->index >= count) return CMETA_GEN_DONE;
  ref.name = data_bind_value_field_name(owner, cursor->index);
  ref.value = data_bind_value_field_at(owner, cursor->index);
  if (ref.name == NULL || ref.value == NULL) return CMETA_GEN_ERROR;
  memcpy(out_value, &ref, sizeof(ref));
  ++cursor->index;
  return cursor->index == count ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static cmeta_gen_status data_bind_cmeta_map_entries_next(const void *object,
                                                          cmeta_range_cursor *cursor,
                                                          void *out_value) {
  const DataBindValue *owner = (const DataBindValue *)object;
  DataBindMapEntry entry;
  DataBindMapEntryRef ref;
  size_t count;

  if (owner == NULL || cursor == NULL || out_value == NULL) return CMETA_GEN_ERROR;
  count = data_bind_value_count(owner);
  if (cursor->index >= count) return CMETA_GEN_DONE;
  entry = data_bind_value_map_entry_at(owner, cursor->index);
  if (entry.key == NULL || entry.value == NULL) return CMETA_GEN_ERROR;
  ref.key = entry.key;
  ref.value = entry.value;
  memcpy(out_value, &ref, sizeof(ref));
  ++cursor->index;
  return cursor->index == count ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

DataBindStatus data_bind_cmeta_range_init(const DataBindValue *owner,
                                          DataBindCMetaRangeKind kind,
                                          cmeta_range *out_range) {
  cmeta_range range = {0};
  DataBindValueKind value_kind;

  if (out_range == NULL) return DATA_BIND_ERR_INVALID_ARG;
  memset(out_range, 0, sizeof(*out_range));
  if (owner == NULL) return DATA_BIND_ERR_INVALID_ARG;

  value_kind = data_bind_value_kind(owner);
  range.object = owner;
  range.flags = CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE;
  switch (kind) {
  case DATA_BIND_CMETA_RANGE_VALUES:
    if (value_kind != DATA_BIND_VALUE_LIST && value_kind != DATA_BIND_VALUE_SET)
      return DATA_BIND_ERR_INVALID_ARG;
    if (value_kind == DATA_BIND_VALUE_SET) range.flags |= CMETA_RANGE_UNIQUE;
    range.element_type = data_bind_cmeta_value_ref_type();
    range.size = data_bind_cmeta_values_size;
    range.next = data_bind_cmeta_values_next;
    break;
  case DATA_BIND_CMETA_RANGE_FIELDS:
    if (value_kind != DATA_BIND_VALUE_OBJECT) return DATA_BIND_ERR_INVALID_ARG;
    range.element_type = data_bind_cmeta_field_ref_type();
    range.size = data_bind_cmeta_fields_size;
    range.next = data_bind_cmeta_fields_next;
    break;
  case DATA_BIND_CMETA_RANGE_MAP_ENTRIES:
    if (value_kind != DATA_BIND_VALUE_MAP) return DATA_BIND_ERR_INVALID_ARG;
    range.element_type = data_bind_cmeta_map_entry_ref_type();
    range.size = data_bind_cmeta_values_size;
    range.next = data_bind_cmeta_map_entries_next;
    break;
  default:
    return DATA_BIND_ERR_INVALID_ARG;
  }

  range.current_version = data_bind_cmeta_container_version;
  range.version = cmeta_range_capture_version(range.current_version, range.object);
  if (range.version == UINT64_C(0)) return DATA_BIND_ERR_RUNTIME;

  *out_range = range;
  return DATA_BIND_OK;
}
