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
  const cmeta_data_desc *data = &salts_int32_cmeta_data;
  DataBindError first = DATA_BIND_ERROR_INIT, second = DATA_BIND_ERROR_INIT;
  check_equal(data_bind_schema_field_cmeta_data(codec, record, index, &data, &first),
              DATA_BIND_ERR_SCHEMA);
  check(data == &salts_int32_cmeta_data);
  check_equal(first.code, DATA_BIND_ERR_SCHEMA);
  check_equal(first.path, path);
  check_not_null(strstr(first.message, "CMeta"));
  check_equal(data_bind_schema_field_cmeta_data(codec, record, index, &data, &second),
              DATA_BIND_ERR_SCHEMA);
  check(data == &salts_int32_cmeta_data);
  check_equal(second.code, first.code);
  check_equal(second.path, first.path);
  check_equal(second.message, first.message);
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
    if (sample && flags) {
      const cmeta_data_struct_shape *shape = sample->shape;
      const cmeta_data_enum_shape *state = shape->fields[1].value->shape;
      const cmeta_data_enum_shape *permission = ((const cmeta_data_struct_shape *)flags->shape)->fields[0].value->shape;
      DataBindSchemaEnumItem item = DATA_BIND_SCHEMA_ENUM_ITEM_INIT;
      cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("tbe.native.Graph.Point_t");
      check(cmeta_type_identity_equal(shape->fields[0].value->storage_type->identity, &identity));
      check_equal(state->meta->items[1].value, 7);
      check_equal(permission->meta->count, 2u);
      check_equal(permission->meta->items[1].value, 2);
      check(data_bind_schema_enum_item_at(codec, "Permission", 1, &item));
      check_equal(item.name, permission->meta->items[1].symbol);
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

  it("rejects buffer and container native queries with repeatable atomic diagnostics") {
    typedef DataBindStatus (*Getter)(const cmeta_data_desc **, DataBindError *);
    static const struct { const char *record; size_t index; cmeta_data_kind kind; const char *path; const char *id; Getter get; } cases[] = {
      {"Unsupported", 1, CMETA_DATA_STRING, "Unsupported.bad", NULL, Unsupported_cmeta_data},
      {"BytesStorage", 0, CMETA_DATA_BYTES, "BytesStorage.value", NULL, BytesStorage_cmeta_data},
      {"ListStorage", 0, CMETA_DATA_SEQUENCE, "ListStorage.value", "cmeta.data.sequence", ListStorage_cmeta_data},
      {"SetStorage", 0, CMETA_DATA_SET, "SetStorage.value", "cmeta.data.set", SetStorage_cmeta_data},
      {"MapStorage", 0, CMETA_DATA_MAP, "MapStorage.value", "cmeta.data.map", MapStorage_cmeta_data}
    };
    DataBind *codec = acceptance_codec();
    size_t i;
    check_not_null(codec);
    if (!codec) return;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
      DataBindError error = DATA_BIND_ERROR_INIT, again = DATA_BIND_ERROR_INIT;
      const cmeta_data_desc *out = &salts_int32_cmeta_data;
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
      check(out == &salts_int32_cmeta_data);
      check_equal(error.path, cases[i].record);
      check_equal(cases[i].get(&out, &again), DATA_BIND_ERR_SCHEMA);
      check(out == &salts_int32_cmeta_data);
      check_equal(again.code, error.code);
      check_equal(again.path, error.path);
      check_equal(again.message, error.message);
    }
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
      same_value_type(base, &salts_int32_cmeta_data);
    }
    out = base;
    check_equal(OptionalStorage_cmeta_data(&out, &error), DATA_BIND_ERR_SCHEMA);
    check(out == base);
    check_equal(error.path, "OptionalStorage");
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
