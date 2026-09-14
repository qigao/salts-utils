#include "cmeta_graph_generated.h"
#include "tinytest.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <stddef.h>
#include <string.h>

spec("generated native CMeta graph") {
  it("publishes structural metadata and validates the public descriptor") {
    const cmeta_data_desc *data = NULL;
    const TbeTypedDescriptor *descriptor = Sample_typed_descriptor();
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(descriptor);
    check_equal(Sample_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    check(descriptor != NULL && descriptor->native_data == data);
    if (data && descriptor) {
      const cmeta_data_struct_shape *shape = data->shape;
      const cmeta_data_struct_shape *point = shape->fields[0].value->shape;
      const cmeta_data_enum_shape *state = shape->fields[1].value->shape;
      TbeTypedDescriptor descriptor_copy = *descriptor;
      cmeta_data_desc copy = *data;
      cmeta_type_desc storage_copy = *data->storage_type;
      cmeta_type_identity identity_copy = *storage_copy.identity;

      storage_copy.identity = &identity_copy;
      copy.storage_type = &storage_copy;
      descriptor_copy.native_data = &copy;
      check(cmeta_data_desc_valid(data));
      check_equal(data->kind, CMETA_DATA_STRUCT);
      check_equal(data->storage_type->size, sizeof(Sample_t));
      check_equal(data->storage_type->align, _Alignof(Sample_t));
      check_equal(shape->field_count, 3u);
      check_equal(shape->fields[0].offset, offsetof(Sample_t, point));
      check_equal(shape->fields[1].offset, offsetof(Sample_t, state));
      check_equal(point->field_count, 2u);
      check_equal(point->fields[1].offset, offsetof(Point_t, y));
      check(cmeta_type_equal(point->fields[0].value->storage_type,
                             salts_int32_cmeta_data.storage_type));
      check_equal(shape->fields[1].value->kind, CMETA_DATA_ENUM);
      check_equal(shape->fields[1].value->storage_type->size, sizeof(State_t));
      check_not_null(cmeta_data_enum_ops_of(shape->fields[1].value));
      check_equal(state->meta->count, 2u);
      check_equal(state->meta->items[1].value, 7);
      check_equal(state->meta->items[1].symbol, "Ready");
      check_equal(shape->fields[2].name, "count");
      check_null(cmeta_data_struct_find_field(shape, "wire_count"));
      check_null(cmeta_data_struct_find_field(shape, "old_count"));
      check(cmeta_type_equal(copy.storage_type, data->storage_type));
      check_equal(tbe_typed_descriptor_validate(&descriptor_copy, &error),
                  DATA_BIND_OK);

      storage_copy.size += 1u;
      check_equal(tbe_typed_descriptor_validate(&descriptor_copy, &error),
                  DATA_BIND_ERR_SCHEMA);
      storage_copy = *data->storage_type;
      {
        cmeta_data_struct_shape altered = *shape;
        cmeta_data_field_desc fields[3];
        memcpy(fields, shape->fields, sizeof(fields));
        fields[2].value = &salts_uint32_cmeta_data;
        altered.fields = fields;
        copy.shape = &altered;
        check_equal(tbe_typed_descriptor_validate(&descriptor_copy, &error),
                    DATA_BIND_ERR_SCHEMA);
      }
      check(descriptor->overlay->fields[2].flags & TBE_TYPED_FIELD_WIRE_OFFSET);
      check_equal(descriptor->overlay->fields[2].wire_offset, 14u);
      check(descriptor->overlay->fields[2].wire_offset != shape->fields[2].offset);
      check_null(descriptor->overlay->fields[0].object_type);
      check_not_null(descriptor->overlay->fields[0].nested_overlay);
    }
  }

  it("keeps structural publication independent from descriptor overlay support") {
    const cmeta_data_desc *sentinel = &salts_int32_cmeta_data;
    const cmeta_data_desc *data = sentinel;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(Unsupported_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check(data == sentinel);
    check(error.path[0] != '\0');
    check_equal(OptionalStorage_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    check_equal(data->kind, CMETA_DATA_STRUCT);
    sentinel = data;
    check_equal(UnsupportedNested_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check(data == sentinel);
  }

#ifndef TBE_CMETA_NODE_FRONTEND_SMOKE
  it("keeps defaults and wire aliases in the schema overlay") {
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    const TbeTypedDescriptor *descriptor = Sample_typed_descriptor();
    const cmeta_data_desc *data = descriptor ? descriptor->native_data : NULL;

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    if (codec && descriptor && data) {
      check_equal(tbe_typed_validate_schema(codec, "Sample", descriptor->overlay,
                                            &error), DATA_BIND_OK);
      check(data_bind_schema_field_at(codec, "Sample", 2u, &field));
      check(field.has_default);
      check_equal(field.default_value, "9");
      check_equal(field.name, "count");
      check_equal(field.offset, 14u);
      check(field.has_cmeta_kind);
      check_equal(field.cmeta_kind, CMETA_DATA_SINT);
      {
        const cmeta_data_struct_shape *shape = data->shape;
        cmeta_type_desc copied = *shape->fields[2].value->storage_type;
        cmeta_type_identity identity = *copied.identity;
        copied.identity = &identity;
        check_not_null(field.cmeta_data);
        check(cmeta_type_equal(field.cmeta_data->storage_type, &copied));
        check_equal(field.cmeta_data->stable_id,
                    shape->fields[2].value->stable_id);
        check(data_bind_schema_field_at(codec, "Sample", 0u, &field));
        check_equal(field.cmeta_kind, CMETA_DATA_STRUCT);
        check_null(field.cmeta_data);
        check(data_bind_schema_field_at(codec, "Sample", 1u, &field));
        check_equal(field.cmeta_kind, CMETA_DATA_ENUM);
        check_null(field.cmeta_data);
      }
      data_bind_free(codec);
    }
  }
#endif

  it("accepts depth 32 and rejects depth 33 without partial publication") {
    const cmeta_data_desc *data = NULL;
    const cmeta_data_desc *published;
    const TbeTypedDescriptor *descriptor = Depth32_typed_descriptor();
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(descriptor);
    check_equal(tbe_typed_descriptor_validate(descriptor, &error), DATA_BIND_OK);
    check_equal(Depth32_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    published = data;
    check_equal(Depth33_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check(data == published);
    check_equal(error.code, DATA_BIND_ERR_SCHEMA);
    check_equal(error.path, "Depth33");
    check_not_null(strstr(error.message, "CMeta"));
  }

  it("does not publish generated uint8 boolean storage as native bool") {
    const cmeta_data_desc *data = &salts_int32_cmeta_data;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check(_Generic(((BoolStorage_t *)0)->value, uint8_t: 1, default: 0));
    check_equal(BoolStorage_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check(data == &salts_int32_cmeta_data);
  }

  it("rejects a uint64 enum domain without truncating constants") {
    const cmeta_data_desc *data = &salts_int32_cmeta_data;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(sizeof(WideDomain_t), sizeof(uint64_t));
    check(WideDomain_Maximum == UINT64_MAX);
    check_equal(WideEnumStorage_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check(data == &salts_int32_cmeta_data);
  }

  it("publishes provider-backed UUID structural metadata without a descriptor") {
    const cmeta_data_desc *data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(UuidStorage_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (data) {
      const cmeta_data_struct_shape *shape = data->shape;
      const cmeta_data_desc *uuid = shape->fields[0].value;
      cmeta_type_desc copied = salts_uuid_cmeta_type;
      cmeta_type_identity identity = *copied.identity;
      copied.identity = &identity;
      check(salts_uuid_cmeta_data_valid(uuid));
      check_equal(uuid->kind, CMETA_DATA_STRING);
      check(cmeta_type_equal(uuid->storage_type, &copied));
      check_equal(uuid->storage_type->size, sizeof(((UuidStorage_t *)0)->value));
    }
  }
}
