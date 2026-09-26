#include "cmeta_graph_generated.h"
#include "schema_cmeta.h"
#include "tinytest.h"
#include <salts_cmeta_data.h>
#include <string.h>

/* This consumer links the separately C-compiled, real CLI-generated fixture.
 * No Node codec, private lowering, or generated implementation include is used. */
static DataBind *acceptance_codec(void) {
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
  return codec;
}

static void same_value_type(const cmeta_data_desc *runtime, const cmeta_data_desc *native) {
  check_not_null(runtime);
  check_not_null(native);
  if (runtime && native) {
    check_not_null(runtime->storage_type);
    check_not_null(native->storage_type);
    if (!runtime->storage_type || !native->storage_type) return;
    cmeta_type_desc copy = *native->storage_type;
    cmeta_type_identity identity = *copy.identity;
    copy.identity = &identity;
    check(cmeta_data_desc_valid(runtime));
    check(cmeta_data_desc_valid(native));
    check_equal(runtime->kind, native->kind);
    check_equal(runtime->stable_id, native->stable_id);
    check(cmeta_type_equal(runtime->storage_type, &copy));
  }
}

static void unresolved_field(DataBind *codec, const char *record, size_t index,
                             const char *path) {
  const cmeta_data_desc *data = &cmeta_data_int32;
  DataBindError first = DATA_BIND_ERROR_INIT, second = DATA_BIND_ERROR_INIT;
  check_equal(data_bind_schema_field_cmeta_data(codec, record, index, &data, &first),
              DATA_BIND_ERR_SCHEMA);
  check(data == &cmeta_data_int32);
  check_equal(first.code, DATA_BIND_ERR_SCHEMA);
  check_equal(first.path, path);
  check_not_null(strstr(first.message, "CMeta"));
  check_equal(data_bind_schema_field_cmeta_data(codec, record, index, &data, &second),
              DATA_BIND_ERR_SCHEMA);
  check(data == &cmeta_data_int32);
  check_equal(second.code, first.code);
  check_equal(second.path, first.path);
  check_equal(second.message, first.message);
}

typedef struct MapVisitState {
  size_t count;
  const char *keys[2];
  int32_t values[2];
} MapVisitState;

static cmeta_status collect_map_entry(void *context, const void *key,
                                      const void *value) {
  MapVisitState *state = (MapVisitState *)context;
  if (!state || !key || !value || state->count >= 2u)
    return CMETA_CALLBACK_ERROR;
  state->keys[state->count] = *(const tstr *)key;
  state->values[state->count] = *(const int32_t *)value;
  ++state->count;
  return CMETA_OK;
}

suite("real generated and runtime CMeta acceptance") {
  /* Mutations: alias/width drift in either production consumer, or pointer-only
   * identity that rejects an intact descriptor from another translation unit. */
  it("agrees on all 29 numeric alias spellings and exact shapes") {
    static const struct { const char *name; const char *alias; cmeta_data_kind kind; unsigned bits; } cases[] = {
      {"s8a", "int8_t", CMETA_DATA_SINT, 8}, {"s8b", "int8", CMETA_DATA_SINT, 8}, {"s8c", "i8", CMETA_DATA_SINT, 8},
      {"s16a", "int16_t", CMETA_DATA_SINT, 16}, {"s16b", "int16", CMETA_DATA_SINT, 16}, {"s16c", "i16", CMETA_DATA_SINT, 16},
      {"s32a", "int32_t", CMETA_DATA_SINT, 32}, {"s32b", "int32", CMETA_DATA_SINT, 32}, {"s32c", "i32", CMETA_DATA_SINT, 32},
      {"s64a", "int64_t", CMETA_DATA_SINT, 64}, {"s64b", "int64", CMETA_DATA_SINT, 64}, {"s64c", "i64", CMETA_DATA_SINT, 64},
      {"u8a", "uint8_t", CMETA_DATA_UINT, 8}, {"u8b", "uint8", CMETA_DATA_UINT, 8}, {"u8c", "u8", CMETA_DATA_UINT, 8},
      {"octet", "byte", CMETA_DATA_UINT, 8},
      {"u16a", "uint16_t", CMETA_DATA_UINT, 16}, {"u16b", "uint16", CMETA_DATA_UINT, 16}, {"u16c", "u16", CMETA_DATA_UINT, 16},
      {"u32a", "uint32_t", CMETA_DATA_UINT, 32}, {"u32b", "uint32", CMETA_DATA_UINT, 32}, {"u32c", "u32", CMETA_DATA_UINT, 32},
      {"u64a", "uint64_t", CMETA_DATA_UINT, 64}, {"u64b", "uint64", CMETA_DATA_UINT, 64}, {"u64c", "u64", CMETA_DATA_UINT, 64},
      {"f32a", "float", CMETA_DATA_FLOAT, 32}, {"f32b", "f32", CMETA_DATA_FLOAT, 32},
      {"f64a", "double", CMETA_DATA_FLOAT, 64}, {"f64b", "f64", CMETA_DATA_FLOAT, 64}
    };
    DataBind *codec = acceptance_codec();
    const cmeta_data_desc *native = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t i;
    check_not_null(codec);
    if (!codec) return;
    check_equal(Scalars_cmeta_data(&native, &error), DATA_BIND_OK);
    check_not_null(native);
    if (native) {
      const cmeta_data_struct_shape *shape = native->shape;
      check_equal(shape->field_count, sizeof(cases) / sizeof(cases[0]));
      check_equal(data_bind_schema_field_count(codec, "Scalars"), shape->field_count);
      for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
        const cmeta_data_desc *runtime = NULL;
        const cmeta_data_field_desc *generated = cmeta_data_struct_find_field(shape, cases[i].name);
        check_not_null(generated);
        if (!generated) continue;
        check(data_bind_schema_field_at(codec, "Scalars", i, &field));
        check_equal(field.name, cases[i].name);
        check_equal(field.type, cases[i].alias);
        check_equal(field.has_cmeta_kind, 1);
        check_equal(field.cmeta_kind, cases[i].kind);
        check_equal(data_bind_schema_field_cmeta_data(codec, "Scalars", i, &runtime, &error), DATA_BIND_OK);
        same_value_type(runtime, generated->value);
        if (runtime) {
          unsigned bits = runtime->kind == CMETA_DATA_FLOAT
              ? ((const cmeta_data_float_shape *)runtime->shape)->bits
              : ((const cmeta_data_integer_shape *)runtime->shape)->bits;
          check_equal(bits, cases[i].bits);
          check_equal(field.field_size_bytes, cases[i].bits / 8u);
        }
      }
    }
    data_bind_free(codec);
  }

  it("keeps UUID domain classification separate from its shared native text adapter") {
    DataBind *codec = acceptance_codec();
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *native = NULL, *runtime = NULL;
    check_not_null(codec);
    if (!codec) return;
    check_equal(UuidStorage_cmeta_data(&native, &error), DATA_BIND_OK);
    check(data_bind_schema_field_at(codec, "UuidStorage", 0, &field));
    check_equal(field.cmeta_kind, CMETA_DATA_CUSTOM);
    check_equal(data_bind_schema_field_cmeta_data(codec, "UuidStorage", 0, &runtime, &error), DATA_BIND_OK);
    check(salts_uuid_cmeta_data_valid(runtime));
    if (native) same_value_type(runtime, ((const cmeta_data_struct_shape *)native->shape)->fields[0].value);
    data_bind_free(codec);
  }

  /* Mutation: semantic reflection invents native storage for a provider-less
   * named declaration, or enum/flags metadata is lost in generated records. */
  it("preserves nested structure and enum flags metadata without inferring a runtime provider") {
    static const struct { const char *record; size_t index; cmeta_data_kind kind; const char *path; } cases[] = {
      {"Sample", 0, CMETA_DATA_STRUCT, "Sample.point"},
      {"Sample", 1, CMETA_DATA_ENUM, "Sample.state"},
      {"FlagStorage", 0, CMETA_DATA_ENUM, "FlagStorage.value"}
    };
    DataBind *codec = acceptance_codec();
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *sample = NULL, *flags = NULL;
    size_t i;
    check_not_null(codec);
    if (!codec) return;
    check_equal(Sample_cmeta_data(&sample, &error), DATA_BIND_OK);
    check_equal(FlagStorage_cmeta_data(&flags, &error), DATA_BIND_OK);
    check_not_null(sample);
    check_not_null(flags);
    if (sample && flags) {
      const cmeta_data_struct_shape *shape = sample->shape;
      const cmeta_data_struct_shape *flag_shape = flags->shape;
      const cmeta_data_desc *state_data;
      const cmeta_data_desc *permission_data;
      const cmeta_data_enum_bits_ops *state_ops;
      const cmeta_data_enum_bits_ops *permission_ops;
      const cmeta_enum_domain *state;
      const cmeta_enum_domain *permission;
      const TbeTypedDescriptor *sample_descriptor = Sample_typed_descriptor();
      const TbeTypedDescriptor *flag_descriptor = FlagStorage_typed_descriptor();
      State_t state_value = 0;
      Permission_t permission_value = 0;
      uint64_t bits = 0u;
      DataBindSchemaEnumItem item = DATA_BIND_SCHEMA_ENUM_ITEM_INIT;
      cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("tbe.native.Graph.Point_t");
      check_not_null(sample_descriptor);
      check_not_null(flag_descriptor);
      check_not_null(shape);
      check_not_null(flag_shape);
      if (!sample_descriptor || !flag_descriptor || !shape || !flag_shape) {
        data_bind_free(codec);
        return;
      }
      check(sample_descriptor->native_data == sample);
      check(flag_descriptor->native_data == flags);
      check_equal(tbe_typed_descriptor_validate(sample_descriptor, &error), DATA_BIND_OK);
      check_equal(tbe_typed_descriptor_validate(flag_descriptor, &error), DATA_BIND_OK);
      check_equal(shape->field_count, 3u);
      check_equal(flag_shape->field_count, 1u);
      check_not_null(shape->fields);
      check_not_null(flag_shape->fields);
      if (shape->field_count != 3u || flag_shape->field_count != 1u ||
          !shape->fields || !flag_shape->fields) {
        data_bind_free(codec);
        return;
      }
      state_data = shape->fields[1].value;
      permission_data = flag_shape->fields[0].value;
      check_not_null(shape->fields[0].value);
      check_not_null(state_data);
      check_not_null(permission_data);
      if (!shape->fields[0].value || !state_data || !permission_data) {
        data_bind_free(codec);
        return;
      }
      check_not_null(shape->fields[0].value->storage_type);
      if (!shape->fields[0].value->storage_type) {
        data_bind_free(codec);
        return;
      }
      check(cmeta_type_identity_equal(shape->fields[0].value->storage_type->identity, &identity));
      check_null(state_data->shape);
      check_null(state_data->enum_ops);
      check_null(permission_data->shape);
      check_null(permission_data->enum_ops);
      state_ops = cmeta_data_enum_bits_ops_of(state_data);
      permission_ops = cmeta_data_enum_bits_ops_of(permission_data);
      check_not_null(state_ops);
      check_not_null(permission_ops);
      if (!state_ops || !permission_ops) {
        data_bind_free(codec);
        return;
      }
      state = state_ops->domain;
      permission = permission_ops->domain;
      check_not_null(state);
      check_not_null(permission);
      if (!state || !permission) {
        data_bind_free(codec);
        return;
      }
      check_equal(state->signedness, CMETA_ENUM_SIGNED);
      check_equal(state->bits, 16u);
      check_equal(state->kind, CMETA_ENUM_ORDINARY);
      check_equal(state->declared_mask, 0u);
      check_equal(permission->signedness, CMETA_ENUM_UNSIGNED);
      check_equal(permission->bits, 8u);
      check_equal(permission->kind, CMETA_ENUM_FLAGS);
      check_equal(permission->declared_mask, 3u);
      check_equal(state->count, 2u);
      check_equal(permission->count, 2u);
      check_not_null(state->items);
      check_not_null(permission->items);
      if (state->count != 2u || permission->count != 2u ||
          !state->items || !permission->items) {
        data_bind_free(codec);
        return;
      }
      check_equal(state->items[1].bits, 7u);
      check_equal(permission->items[1].bits, 2u);
      check_equal(cmeta_data_enum_assign_bits(state_data, &state_value, 7u), CMETA_OK);
      check_equal(cmeta_data_enum_read_bits(state_data, &state_value, &bits), CMETA_OK);
      check_equal(bits, 7u);
      check_equal(state_value, State_Ready);
      check_equal(cmeta_data_enum_assign_bits(permission_data, &permission_value,
                                              UINT64_C(1) | UINT64_C(2)), CMETA_OK);
      check_equal(cmeta_data_enum_read_bits(permission_data, &permission_value, &bits), CMETA_OK);
      check_equal(bits, 3u);
      check_equal(permission_value, Permission_Read | Permission_Write);
      check(data_bind_schema_enum_item_at(codec, "Permission", 1, &item));
      check_equal(item.name, permission->items[1].symbol);
      check_equal(item.value, "2");
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
      check(data_bind_schema_field_at(codec, cases[i].record, cases[i].index, &field));
      check_equal(field.cmeta_kind, cases[i].kind);
      check_null(field.cmeta_data);
      unresolved_field(codec, cases[i].record, cases[i].index, cases[i].path);
    }
    data_bind_free(codec);
  }

  it("rejects buffer and deferred container native queries with repeatable atomic diagnostics") {
    typedef DataBindStatus (*Getter)(const cmeta_data_desc **, DataBindError *);
    static const struct { const char *record; size_t index; cmeta_data_kind kind; const char *path; const char *id; Getter get; } cases[] = {
      {"Unsupported", 1, CMETA_DATA_STRING, "Unsupported.bad", NULL, Unsupported_cmeta_data},
      {"BytesStorage", 0, CMETA_DATA_BYTES, "BytesStorage.value", NULL, BytesStorage_cmeta_data}
    };
    DataBind *codec = acceptance_codec();
    size_t i;
    check_not_null(codec);
    if (!codec) return;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
      DataBindError error = DATA_BIND_ERROR_INIT, again = DATA_BIND_ERROR_INIT;
      const cmeta_data_desc *out = &cmeta_data_int32;
      check(data_bind_schema_field_at(codec, cases[i].record, cases[i].index, &field));
      check_equal(field.cmeta_kind, cases[i].kind);
      if (cases[i].id) {
        check_not_null(field.cmeta_data);
        if (field.cmeta_data) {
          check(cmeta_data_desc_valid(field.cmeta_data));
          check_equal(field.cmeta_data->stable_id, cases[i].id);
          check_null(field.cmeta_data->storage_type);
          check_null(field.cmeta_data->shape);
        }
      } else check_null(field.cmeta_data);
      unresolved_field(codec, cases[i].record, cases[i].index, cases[i].path);
      check_equal(cases[i].get(&out, &error), DATA_BIND_ERR_SCHEMA);
      check(out == &cmeta_data_int32);
      check_equal(error.path, cases[i].record);
      check_equal(cases[i].get(&out, &again), DATA_BIND_ERR_SCHEMA);
      check(out == &cmeta_data_int32);
      check_equal(again.code, error.code);
      check_equal(again.path, error.path);
      check_equal(again.message, error.message);
    }
    data_bind_free(codec);
  }

  it("publishes generated list and set through canonical CSTL CMeta providers") {
    static const struct {
      const char *record;
      cmeta_data_kind kind;
      DataBindStatus (*get)(const cmeta_data_desc **, DataBindError *);
    } cases[] = {
      {"ListStorage", CMETA_DATA_SEQUENCE, ListStorage_cmeta_data},
      {"SetStorage", CMETA_DATA_SET, SetStorage_cmeta_data},
      {"StringListStorage", CMETA_DATA_SEQUENCE, StringListStorage_cmeta_data}
    };
    DataBind *codec = acceptance_codec();
    size_t i;

    check_not_null(codec);
    if (!codec) return;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      const cmeta_data_desc *out = NULL;
      const cmeta_data_desc *element;
      DataBindError error = DATA_BIND_ERROR_INIT;
      DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;

      check(data_bind_schema_field_at(codec, cases[i].record, 0u, &field));
      check_equal(field.cmeta_kind, cases[i].kind);
      check_equal(cases[i].get(&out, &error), DATA_BIND_OK);
      check_not_null(out);
      if (out == NULL) continue;
      check_true(cmeta_data_desc_valid(out));
      check_equal(out->kind, cases[i].kind);
      check_not_null(out->storage_type);
      element = cmeta_data_collection_element_data(out);
      check_not_null(element);
      if (element != NULL) {
        check_not_null(element->storage_type);
        check_true(cmeta_type_desc_valid(element->storage_type));
      }
    }

    data_bind_free(codec);
  }

  it("publishes explicit ordered map key/value providers without erased inference") {
    DataBind *codec = acceptance_codec();
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *record_data = NULL;
    const cmeta_data_desc *map_data;
    const cmeta_data_map_ops *ops;
    const cmeta_data_struct_shape *shape;
    MapStorage_t object;
    MapStorage_value_entry_t first = {0}, second = {0}, duplicate = {0};
    MapVisitState visited = {0};
    cmeta_data_map_borrow_cursor cursor;
    cmeta_collector collector;
    stl_status duplicate_status;
    const void *key = NULL, *value = NULL;
    size_t size = 0u;

    check_not_null(codec);
    if (!codec) return;
    check(data_bind_schema_field_at(codec, "MapStorage", 0u, &schema_field));
    check_equal(schema_field.cmeta_kind, CMETA_DATA_MAP);
    check_not_null(schema_field.cmeta_data);
    if (schema_field.cmeta_data) {
      check_null(schema_field.cmeta_data->storage_type);
      check_null(schema_field.cmeta_data->map_ops);
    }
    unresolved_field(codec, "MapStorage", 0u, "MapStorage.value");

    check_equal(MapStorage_cmeta_data(&record_data, &error), DATA_BIND_OK);
    check_not_null(record_data);
    if (!record_data) {
      data_bind_free(codec);
      return;
    }
    shape = (const cmeta_data_struct_shape *)record_data->shape;
    check_not_null(shape);
    if (!shape || shape->field_count != 1u || !shape->fields) {
      data_bind_free(codec);
      return;
    }
    map_data = shape->fields[0].value;
    check_not_null(map_data);
    if (!map_data) {
      data_bind_free(codec);
      return;
    }
    check(cmeta_data_desc_valid(map_data));
    check_equal(map_data->kind, CMETA_DATA_MAP);
    check_null(map_data->collection_ops);
    check_not_null(strstr(map_data->stable_id, "databind.native.Graph.MapStorage_t.value.map"));
    ops = cmeta_data_map_ops_of(map_data);
    check_not_null(ops);
    if (!ops) {
      data_bind_free(codec);
      return;
    }
    check_equal(ops->flags, CMETA_DATA_MAP_UNIQUE_KEYS | CMETA_DATA_MAP_ORDERED);

    MapStorage_init(&object);
    first.key = tstr_dup("first");
    first.value = 11;
    second.key = tstr_dup("second");
    second.value = 22;
    check_not_null(first.key);
    check_not_null(second.key);
    if (!first.key || !second.key) {
      tstr_free(first.key);
      tstr_free(second.key);
      MapStorage_clear(&object);
      data_bind_free(codec);
      return;
    }
    check_equal(MapStorage_value_vec_t_push(&object.value, first), STL_OK);
    check_equal(MapStorage_value_vec_t_push(&object.value, second), STL_OK);
    check(cmeta_data_desc_equal(ops->key(&object.value), &salts_tstr_cmeta_data));
    same_value_type(ops->value(&object.value), &cmeta_data_int32);
    check_equal(cmeta_data_map_foreach(map_data, &object.value, collect_map_entry,
                                      &visited, 2u), CMETA_OK);
    check_equal(visited.count, 2u);
    check_equal(visited.keys[0], "first");
    check_equal(visited.values[0], 11);
    check_equal(visited.keys[1], "second");
    check_equal(visited.values[1], 22);
    visited = (MapVisitState){0};
    check_equal(cmeta_data_map_foreach(map_data, &object.value, collect_map_entry,
                                      &visited, 1u), CMETA_CAPACITY_EXCEEDED);
    check_equal(visited.count, 0u);

    check_equal(cmeta_data_map_borrow_begin(map_data, &object.value, &cursor), CMETA_OK);
    check_equal(cmeta_data_map_borrow_size(&cursor, &size), CMETA_OK);
    check_equal(size, 2u);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value), CMETA_GEN_VALUE);
    check_equal(*(const tstr *)key, "first");
    check_equal(*(const int32_t *)value, 11);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                CMETA_GEN_VALUE_AND_DONE);
    check_equal(*(const tstr *)key, "second");
    check_equal(*(const int32_t *)value, 22);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value), CMETA_GEN_DONE);
    check_equal(cmeta_data_map_collector(map_data, &object.value, 2u, &collector),
                CMETA_TRAIT_MISSING);

    duplicate.key = tstr_dup("first");
    duplicate.value = 33;
    check_not_null(duplicate.key);
    if (duplicate.key) {
      duplicate_status = MapStorage_value_vec_t_push(&object.value, duplicate);
      check_equal(duplicate_status, STL_OK);
      if (duplicate_status == STL_OK) {
        visited = (MapVisitState){0};
        check_equal(cmeta_data_map_foreach(map_data, &object.value,
                                          collect_map_entry, &visited, 3u),
                    CMETA_CALLBACK_ERROR);
        check_equal(visited.count, 0u);
        check_equal(cmeta_data_map_borrow_begin(map_data, &object.value, &cursor),
                    CMETA_OK);
        check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                    CMETA_GEN_ERROR);
      } else {
        tstr_free(duplicate.key);
      }
    }

    MapStorage_clear(&object);
    data_bind_free(codec);
  }

  it("keeps optional values gated and distinguishes Bool8 native storage") {
    DataBind *codec = acceptance_codec();
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *base = NULL, *out;
    check_not_null(codec);
    if (!codec) return;
    check(data_bind_schema_field_at(codec, "OptionalStorage", 0, &field));
    check_equal(field.is_optional, 1);
    check_equal(data_bind_schema_field_cmeta_data(codec, "OptionalStorage", 0, &base, &error), DATA_BIND_OK);
    check_not_null(base);
    if (base) {
      check_equal(base->storage_type->identity->form, CMETA_TYPE_ATOM);
      same_value_type(base, &cmeta_data_int32);
    }
    out = NULL;
    check_equal(OptionalStorage_cmeta_data(&out, &error), DATA_BIND_OK);
    check_not_null(out);
    if (out) {
      const cmeta_data_struct_shape *optional_shape =
          (const cmeta_data_struct_shape *)out->shape;
      check_equal(out->kind, CMETA_DATA_STRUCT);
      check_not_null(optional_shape);
      if (optional_shape && optional_shape->field_count == 1u)
        same_value_type(optional_shape->fields[0].value, &cmeta_data_int32);
    }
    check_equal(data_bind_schema_field_cmeta_data(codec, "BoolStorage", 0, &base, &error), DATA_BIND_OK);
    same_value_type(base, &cmeta_data_bool);
    out = base;
    check_equal(BoolStorage_cmeta_data(&out, &error), DATA_BIND_OK);
    check_not_null(out);
    if (out) {
      const cmeta_data_struct_shape *shape =
          (const cmeta_data_struct_shape *)out->shape;
      check_not_null(shape);
      if (shape && shape->field_count == 1u) {
        const cmeta_data_desc *native_bool = shape->fields[0].value;
        check_not_null(native_bool);
        if (!native_bool) {
          data_bind_free(codec);
          return;
        }
        check_equal(native_bool->kind, CMETA_DATA_BOOL);
        check(cmeta_type_equal(native_bool->storage_type,
                               &salts_bool8_cmeta_type));
        check(!cmeta_type_equal(native_bool->storage_type,
                                cmeta_data_bool.storage_type));
      }
    }
    unresolved_field(codec, "WideEnumStorage", 0, "WideEnumStorage.value");
    data_bind_free(codec);
  }

  it("keeps union custom and unknown native mappings gated with schema field context") {
    static const char schema[] =
        "composite Point { int32 x; } union Choice { Point point; }"
        "message Gate { Choice choice; datetime timestamp; date day; time clock;"
        "duration elapsed; decimal amount; bigint large; money price; MadeUp unknown; }";
    static const char *const names[] = {"choice", "timestamp", "day", "clock", "elapsed", "amount", "large", "price", "unknown"};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t i;
    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1u, &codec, &error), DATA_BIND_OK);
    if (!codec) return;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
      DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
      char path[64];
      check(data_bind_schema_field_at(codec, "Gate", i, &field));
      check_equal(field.name, names[i]);
      check_equal(field.has_cmeta_kind, i != 8u);
      if (i != 8u) check_equal(field.cmeta_kind, i == 0u ? CMETA_DATA_VARIANT : CMETA_DATA_CUSTOM);
      else check_equal(field.kind, "unknown");
      check_null(field.cmeta_data);
      snprintf(path, sizeof(path), "Gate.%s", names[i]);
      unresolved_field(codec, "Gate", i, path);
    }
    data_bind_free(codec);
  }

  /* Mutation: schema aliases/defaults/fingerprint become structural identity,
   * or equal CMeta value types bypass schema compatibility and validation. */
  it("keeps wire names defaults validation and fingerprints independent of CMeta identity") {
    static const char a[] = "schema Overlay; message Value { [name(wire_a), alias(old_a)] int32 count default 9; }";
    static const char b[] = "schema Overlay; message Value { [name(wire_b), alias(old_b)] int32 count default 10; }";
    static const char alias_json[] = "{\"old_a\":7}";
    DataBind *left = NULL, *right = NULL;
    DataBindObject *object = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_desc *one = NULL, *two = NULL;
    char *json = NULL;
    size_t length = 0;
    check_equal(data_bind_create_from_text(a, sizeof(a) - 1u, &left, &error), DATA_BIND_OK);
    check_equal(data_bind_create_from_text(b, sizeof(b) - 1u, &right, &error), DATA_BIND_OK);
    if (!left || !right) { data_bind_free(left); data_bind_free(right); return; }
    check_equal(data_bind_schema_field_cmeta_data(left, "Value", 0, &one, &error), DATA_BIND_OK);
    check_equal(data_bind_schema_field_cmeta_data(right, "Value", 0, &two, &error), DATA_BIND_OK);
    same_value_type(one, two);
    check(data_bind_schema_field_at(left, "Value", 0, &field));
    check_equal(field.name, "count");
    check_equal(field.default_value, "9");
    check_equal(data_bind_object_from_json(left, "Value", "{}", 2u, &object, &error), DATA_BIND_OK);
    if (object) {
      check_equal(data_bind_object_serialize_json(left, object, &json, &length, &error), DATA_BIND_OK);
      check_equal(json, "{\"wire_a\":9}");
      data_bind_serialized_free(json); json = NULL;
      check_equal(data_bind_object_serialize_json(right, object, &json, &length, &error), DATA_BIND_ERR_SCHEMA);
      check_null(json);
      check_equal(length, 0u);
      check_equal(error.path, "Value");
      check_not_null(strstr(error.message, "fingerprint"));
      data_bind_object_free(object); object = NULL;
    }
    check_equal(data_bind_object_from_json(left, "Value", alias_json, sizeof(alias_json) - 1u, &object, &error), DATA_BIND_OK);
    if (object) {
      check_equal(data_bind_value_as_int(data_bind_value_get(data_bind_object_value(object), "count")), 7);
      data_bind_object_free(object);
    }
    {
      const char invalid[] = "{\"wire_a\":2147483648}";
      check(data_bind_validate_json(left, "Value", invalid, sizeof(invalid) - 1u, &error) != DATA_BIND_OK);
    }
    data_bind_free(left); data_bind_free(right);
  }
}
