#include "nested_native_generics_native.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int nested_native_generics_cpp_inspect(void);
int nested_native_generics_cpp_generic_owners(void);
int nested_native_generics_cpp_recursive_identity(void);

/* C++ 只声明存储布局；跨 TU 用例借用 C facade 生成的静态操作元数据。 */
const cmeta_receiver_operation_set *nested_native_generics_c_generic_methods(size_t field) {
  const cmeta_receiver_operation_set *sets[] = {
    Flat_values_vec_t_receiver_operation_set(),
    Flat_ids_set_t_receiver_operation_set(),
    Flat_counters_map_t_receiver_operation_set()
  };
  return field < sizeof(sets) / sizeof(sets[0]) ? sets[field] : NULL;
}

size_t nested_native_generics_c_size(void) {
  return sizeof(Batch_t);
}
size_t nested_native_generics_c_offset(size_t field) {
  static const size_t offsets[] = {
    offsetof(Batch_t, matrix), offsetof(Batch_t, records), offsetof(Batch_t, groups)
  };
  return field < sizeof(offsets) / sizeof(offsets[0]) ? offsets[field] : SIZE_MAX;
}

static void check_generic_provider(const cmeta_data_desc *data,
                                   const cmeta_generic_desc *constructor,
                                   const cmeta_data_desc *argument0,
                                   const cmeta_data_desc *argument1) {
  const cmeta_type_identity *identity = cmeta_type_identity_of(data->storage_type);
  check_true(cmeta_data_value_copy_supported(data));
  check_true(cmeta_data_value_move_supported(data));
  check_true(cmeta_generic_desc_equal(cmeta_type_identity_constructor(identity), constructor));
  check_true(cmeta_type_identity_equal(cmeta_type_identity_argument(identity, 0u),
             cmeta_type_identity_of(argument0->storage_type)));
  if (argument1 != NULL)
    check_true(cmeta_type_identity_equal(cmeta_type_identity_argument(identity, 1u),
               cmeta_type_identity_of(argument1->storage_type)));
}

spec("generated nested native generic containers") {
  it("publishes group metadata through the common record schema") {
    const cmeta_data_desc *data = NULL;
    const cmeta_data_struct_shape *shape;
    DataBindError error = DATA_BIND_ERROR_INIT;
    ReflectionEntry_t entry, copy;
    ReflectionEntry_init(&entry);
    ReflectionEntry_init(&copy);
    check_equal(ReflectionEntry_cmeta_data(&data, &error), DATA_BIND_OK);
    if (data != NULL) {
      shape = (const cmeta_data_struct_shape *)data->shape;
      check_equal(shape->field_count, (size_t)2u);
      check_equal(shape->fields[1].offset, offsetof(ReflectionEntry_t, label));
      entry.label = tstr_dup("group owner");
      check_not_null(entry.label);
      check_equal(cmeta_data_value_copy(data, &copy, &entry), CMETA_OK);
      ReflectionEntry_clear(&entry);
      check_equal(copy.label, "group owner");
    }
    ReflectionEntry_clear(&copy);
    ReflectionEntry_clear(&entry);
  }

  it("reflects every field across three schema groups with owning lifecycle") {
    enum { WIDE_FIELD_COUNT = 33u, WIDE_POINT_INDEX = 31u, WIDE_TAIL_INDEX = 32u };
    const cmeta_data_desc *data = NULL;
    const cmeta_data_struct_shape *shape;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    ReflectionWide_t value, copy, decoded;
    char *json = NULL;
    size_t length = 0u;
    size_t i;
    check_equal(NestedNative_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;
    ReflectionWide_init(&value);
    ReflectionWide_init(&copy);
    ReflectionWide_init(&decoded);
    check_equal(ReflectionWide_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (data != NULL) {
      shape = (const cmeta_data_struct_shape *)data->shape;
      check_true(cmeta_data_desc_valid(data));
      check_equal(shape->field_count, (size_t)WIDE_FIELD_COUNT);
      check_equal(shape->layout->field_count, (size_t)WIDE_FIELD_COUNT);
      for (i = 0u; i < shape->field_count; ++i) {
        check_equal(shape->fields[i].name, shape->layout->fields[i].name);
        check_equal(shape->fields[i].offset, shape->layout->fields[i].offset);
        check_not_null(shape->fields[i].value);
      }
      check_equal(shape->fields[WIDE_POINT_INDEX].offset, offsetof(ReflectionWide_t, point));
      {
        const cmeta_data_struct_shape *point =
            (const cmeta_data_struct_shape *)shape->fields[WIDE_POINT_INDEX].value->shape;
        check_equal(point->fields[0].name, "x");
        check_equal(point->layout->fields[0].name, "x");
        check_equal(point->fields[0].offset, offsetof(ReflectionPoint_t, x_axis));
        check_equal(point->fields[0].stable_id, "tbe.native.NestedNative.ReflectionPoint_t.x");
      }
      check_equal(shape->fields[WIDE_TAIL_INDEX].offset, offsetof(ReflectionWide_t, tail));
      check_equal(shape->fields[WIDE_TAIL_INDEX].stable_id,
                  "tbe.native.NestedNative.ReflectionWide_t.tail");
      value.f00 = 11u;
      value.f15 = 16u;
      value.f16 = 17u;
      value.f30 = 31u;
      value.point.x_axis = 32;
      value.point.y = 33;
      value.tail = tstr_dup("owned across schema groups");
      check_not_null(value.tail);
      check_equal(cmeta_data_value_copy(data, &copy, &value), CMETA_OK);
      check_true(copy.tail != value.tail);
      ReflectionWide_clear(&value);
      {
        DataBindStatus status = ReflectionWide_to_json(codec, &copy, &json, &length, &error);
        info("wide JSON output: %s", error.message);
        check_equal(status, DATA_BIND_OK);
      }
      if (json != NULL) {
        check_equal(ReflectionWide_from_json(codec, &decoded, json, length, &error), DATA_BIND_OK);
        check_equal(decoded.f00, 11u);
        check_equal(decoded.f15, 16u);
        check_equal(decoded.f16, 17u);
        check_equal(decoded.f30, 31u);
        check_equal(decoded.point.x_axis, 32);
        check_equal(decoded.point.y, 33);
        check_equal(decoded.tail, "owned across schema groups");
      }
    }
    free(json);
    ReflectionWide_clear(&decoded);
    ReflectionWide_clear(&copy);
    ReflectionWide_clear(&value);
    data_bind_free(codec);
  }

  it("uses canonical generated Vec Set Map operation owners across C and C++") {
    check_equal(nested_native_generics_cpp_generic_owners(), 0);
  }

  it("compares independently constructed recursive generic identities across C and C++") {
    check_equal(nested_native_generics_cpp_recursive_identity(), 0);
  }

  it("publishes recursive applied identity and exact canonical providers to C and C++") {
    const cmeta_data_desc *data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_struct_shape *shape;
    const cmeta_data_desc *matrix;
    const cmeta_data_desc *inner;
    const cmeta_type_identity *identity;
    const cmeta_data_desc *mirror = NULL;
    const cmeta_data_desc *user = NULL;
    const cmeta_data_desc *records;
    const cmeta_data_desc *groups;
    size_t field;
    check_equal(Batch_cmeta_data(&data, &error), DATA_BIND_OK);
    check_true(cmeta_data_desc_valid(data));
    check_true(cmeta_data_value_copy_supported(data));
    check_true(cmeta_data_value_move_supported(data));
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_equal(shape->field_count, (size_t)3u);
    matrix = shape->fields[0].value;
    inner = cmeta_data_collection_element_data(matrix);
    check_not_null(inner);
    check_equal(inner->kind, CMETA_DATA_SEQUENCE);
    check_true(cmeta_data_collection_element_data(inner) == &cmeta_data_int32);
    identity = cmeta_type_identity_of(matrix->storage_type);
    check_true(cmeta_generic_desc_equal(
        cmeta_type_identity_constructor(identity), &stl_vec_generic_desc));
    check_true(cmeta_type_identity_argument(identity, 0u) ==
        cmeta_type_identity_of(inner->storage_type));
    check_true(cmeta_declared_type_valid(shape->layout->fields[0].declared_type));
    check_equal(User_cmeta_data(&user, &error), DATA_BIND_OK);
    records = cmeta_data_collection_element_data(shape->fields[1].value);
    groups = cmeta_data_map_value_data(shape->fields[2].value);
    check_generic_provider(shape->fields[1].value, &stl_vec_generic_desc, records, NULL);
    check_generic_provider(records, &stl_map_generic_desc, &cmeta_tstr_cmeta_data, user);
    check_generic_provider(shape->fields[2].value, &stl_map_generic_desc, &cmeta_tstr_cmeta_data, groups);
    check_generic_provider(groups, &stl_vec_generic_desc, user, NULL);
    for (field = 0u; field < shape->field_count; ++field)
      check_true(cmeta_declared_type_valid(shape->layout->fields[field].declared_type));
    check_equal(Mirror_cmeta_data(&mirror, &error), DATA_BIND_OK);
    mirror = ((const cmeta_data_struct_shape *)mirror->shape)->fields[0].value;
    check_true(matrix->storage_type != mirror->storage_type);
    check_true(cmeta_type_equal(matrix->storage_type, mirror->storage_type));
    check_equal(nested_native_generics_cpp_inspect(), 0);
  }

  it("keeps optional and nullable state outside underlying container identity") {
    static const char json[] = "{\"matrix\":[[7]],\"records\":[],\"groups\":{}}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *batch = NULL;
    const cmeta_data_desc *overlay = NULL;
    const cmeta_data_struct_shape *batch_shape;
    const cmeta_data_struct_shape *overlay_shape;
    Batch_t source;
    Overlay_t value;
    check_equal(NestedNative_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(Batch_cmeta_data(&batch, &error), DATA_BIND_OK);
    check_equal(Overlay_cmeta_data(&overlay, &error), DATA_BIND_OK);
    check_not_equal(cmeta_type_require_traits(overlay->storage_type,
                    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY), CMETA_OK);
    batch_shape = (const cmeta_data_struct_shape *)batch->shape;
    overlay_shape = (const cmeta_data_struct_shape *)overlay->shape;
    check_true(cmeta_type_equal(batch_shape->fields[0].value->storage_type,
                               overlay_shape->fields[0].value->storage_type));
    Batch_init(&source);
    Overlay_init(&value);
    check_equal(Batch_from_json(codec, &source, json, sizeof(json) - 1u, &error), DATA_BIND_OK);
    check_equal(cmeta_data_value_copy(overlay_shape->fields[0].value,
                                    &value.matrix, &source.matrix), CMETA_OK);
    value._presence[0] = 1u;
    value._nulls[0] = 1u;
    check_equal(Overlay_from_json(codec, &value, "{}", sizeof("{}") - 1u, &error),
                DATA_BIND_ERR_SCHEMA);
    check_equal(value._presence[0], 1u);
    check_equal(value._nulls[0], 1u);
    Overlay_clear(&value);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
    Overlay_clear(&value);
    Batch_clear(&source);
    data_bind_free(codec);
  }

  it("validates nested records before publishing and preserves the previous owner on failure") {
    static const char initial[] =
        "{\"matrix\":[[7]],\"records\":[{\"first\":{\"id\":1,\"name\":\"Alice\"}}],"
        "\"groups\":{\"team\":[{\"id\":2,\"name\":\"Bob\"}]}}";
    static const char invalid[] =
        "{\"matrix\":[[8]],\"records\":[],\"groups\":{\"team\":[{\"id\":0,\"name\":\"Bad\"}]}}";
    static const char invalid_record[] =
        "{\"matrix\":[[8]],\"records\":[{\"bad\":{\"id\":0,\"name\":\"Bad\"}}],\"groups\":{}}";
    static const char invalid_type[] =
        "{\"matrix\":[[8]],\"records\":[{\"first\":{\"id\":1,\"name\":\"Temporary\"}},"
        "{\"bad\":{\"id\":2,\"name\":7}}],\"groups\":{}}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    Batch_t value;
    char *output = NULL;
    size_t length = 0u;
    check_equal(NestedNative_codec_create(&codec, &error), DATA_BIND_OK);
    Batch_init(&value);
    check_equal(Batch_from_json(codec, &value, initial, sizeof(initial) - 1u, &error), DATA_BIND_OK);
    check_equal(Batch_from_json(codec, &value, invalid, sizeof(invalid) - 1u, &error),
                DATA_BIND_ERR_VALIDATION);
    check_contains(error.path, "groups");
    check_equal(Batch_from_json(codec, &value, invalid_record, sizeof(invalid_record) - 1u, &error),
                DATA_BIND_ERR_VALIDATION);
    check_contains(error.path, "records");
    check_equal(Batch_from_json(codec, &value, invalid_type, sizeof(invalid_type) - 1u, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(Batch_to_json(codec, &value, &output, &length, &error), DATA_BIND_OK);
    check_contains(output, "Bob");
    check_contains(output, "Alice");
    check_false(strstr(output, "Bad") != NULL);
    free(output);
    Batch_clear(&value);
    data_bind_free(codec);
  }

  it("round trips all supported nested shapes and copies their owned records independently") {
    static const char json[] =
        "{\"matrix\":[[1,2],[],[3]],"
        "\"records\":[{\"first\":{\"id\":1,\"name\":\"Alice\"}}],"
        "\"groups\":{\"team\":[{\"id\":2,\"name\":\"Bob\"}]}}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *data = NULL;
    Batch_t value;
    Batch_t copy;
    Batch_t moved;
    Batch_t decoded;
    char *output = NULL;
    size_t length = 0u;
    check_equal(NestedNative_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    Batch_init(&value);
    Batch_init(&copy);
    Batch_init(&moved);
    Batch_init(&decoded);
    check_equal(Batch_cmeta_data(&data, &error), DATA_BIND_OK);
    check_equal(Batch_from_json(codec, &value, json, sizeof(json) - 1u, &error),
                DATA_BIND_OK);
    check_equal(cmeta_data_value_copy(data, &copy, &value), CMETA_OK);
    Batch_clear(&value);
    check_equal(cmeta_data_value_move(data, &moved, &copy), CMETA_OK);
    Batch_clear(&copy);
    check_equal(Batch_to_json(codec, &moved, &output, &length, &error), DATA_BIND_OK);
    check_not_null(output);
    check_contains(output, "Alice");
    check_contains(output, "Bob");
    check_equal(Batch_from_json(codec, &decoded, output, length, &error), DATA_BIND_OK);
    free(output);
    output = NULL;
    check_equal(Batch_to_yaml(codec, &moved, &output, &length, &error), DATA_BIND_OK);
    check_equal(Batch_from_yaml(codec, &decoded, output, length, &error), DATA_BIND_OK);
    free(output);
    output = NULL;
    check_equal(Batch_to_xml(codec, &moved, &output, &length, &error), DATA_BIND_ERR_SCHEMA);
    check_null(output);
    check_equal(length, (size_t)0u);
    {
      uint8_t *binary = NULL;
      check_equal(Batch_to_bin(codec, &moved, &binary, &length, &error), DATA_BIND_ERR_SCHEMA);
      check_null(binary);
      check_equal(length, (size_t)0u);
    }
    Batch_clear(&decoded);
    Batch_clear(&copy);
    Batch_clear(&moved);
    Batch_clear(&copy);
    data_bind_free(codec);
  }
}
