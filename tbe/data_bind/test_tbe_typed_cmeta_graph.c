#include "cmeta_graph_generated.h"
#include "tinytest.h"

#include <salts_cmeta_data.h>
#include <salts_cmeta_fixed_width.h>

#include <stddef.h>
#include <string.h>

static unsigned reject_fixed_copy_hits;

static cmeta_status reject_fixed_copy(void *destination, const void *source) {
  (void)source;
  ++reject_fixed_copy_hits;
  if (destination != NULL) ((uint8_t *)destination)[0] = 0xffu;
  return CMETA_CALLBACK_ERROR;
}

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
      const cmeta_data_enum_bits_ops *state =
          cmeta_data_enum_bits_ops_of(shape->fields[1].value);
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
      check_not_null(state);
      if (!state) return;
      check_null(shape->fields[1].value->shape);
      check_null(shape->fields[1].value->enum_ops);
      check_equal(state->domain->count, 2u);
      check_equal(state->domain->items[1].bits, 7u);
      check_equal(state->domain->items[1].symbol, "Ready");
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
    check_equal(OptionalStorage_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check(data == sentinel);
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

  it("publishes generated uint8 boolean storage through canonical Bool8") {
    const cmeta_data_desc *data = &salts_int32_cmeta_data;
    const cmeta_data_desc *native_bool;
    const cmeta_data_struct_shape *shape;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check(_Generic(((BoolStorage_t *)0)->value, uint8_t: 1, default: 0));
    check_equal(BoolStorage_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (!data) return;
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_not_null(shape);
    if (!shape || shape->field_count != 1u) return;
    native_bool = shape->fields[0].value;
    check_not_null(native_bool);
    if (!native_bool) return;
    check_equal(native_bool->kind, CMETA_DATA_BOOL);
    check(cmeta_type_equal(native_bool->storage_type,
                           &salts_bool8_cmeta_type));
    check_not_null(cmeta_data_fixed_ops_of(native_bool));
    check(!cmeta_type_equal(native_bool->storage_type,
                            cmeta_data_bool.storage_type));
  }

  it("requires exact canonical providers for generated fixed values") {
    static const char json[] =
        "{\"enabled\":true,\"id\":\"00000000-0000-0000-0000-000000000000\","
        "\"digest\":\"0123456789abcdef\"}";
    const TbeTypedDescriptor *descriptor = FixedValues_typed_descriptor();
    const cmeta_data_desc *native = descriptor ? descriptor->native_data : NULL;
    const cmeta_data_struct_shape *shape = native ? native->shape : NULL;
    const cmeta_struct_desc *layout = shape ? shape->layout : NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    FixedValues_t destination;
    FixedValues_t before;
    uint8_t *wire = NULL;
    char *encoded = NULL;
    size_t encoded_len = 0u;
    size_t wire_len = 0u;
    size_t fixed_extent = 0u;
    size_t index;

    check_not_null(descriptor);
    check_equal(tbe_typed_descriptor_validate(descriptor, &error), DATA_BIND_OK);
    check_not_null(shape);
    check_not_null(layout);
    if (!descriptor || !shape || !layout || shape->field_count != 3u) return;

    check_equal(shape->fields[0].value->kind, CMETA_DATA_BOOL);
    check_equal(shape->fields[0].value->storage_type->size,
                sizeof(((FixedValues_t *)0)->enabled));
    check_equal(shape->fields[0].value->storage_type->align,
                _Alignof(uint8_t));
    check_not_null(cmeta_data_fixed_ops_of(shape->fields[0].value));
    check_equal(cmeta_data_fixed_extent(shape->fields[0].value, &fixed_extent),
                CMETA_OK);
    check_equal(fixed_extent, sizeof(((FixedValues_t *)0)->enabled));
    check(salts_uuid_cmeta_data_valid(shape->fields[1].value));
    check_equal(shape->fields[1].value->storage_type->size,
                sizeof(((FixedValues_t *)0)->id));
    check_equal(shape->fields[1].value->storage_type->align,
                _Alignof(salts_uuid_t));
    check_equal(cmeta_data_fixed_extent(shape->fields[1].value, &fixed_extent),
                CMETA_OK);
    check_equal(fixed_extent, sizeof(((FixedValues_t *)0)->id));
    check_equal(shape->fields[2].value->kind, CMETA_DATA_BYTES);
    check_equal(shape->fields[2].value->storage_type->size,
                sizeof(((FixedValues_t *)0)->digest));
    check_equal(layout->fields[2].size,
                sizeof(((FixedValues_t *)0)->digest));
    check_equal(cmeta_data_fixed_extent(shape->fields[2].value, &fixed_extent),
                CMETA_OK);
    check_equal(fixed_extent, sizeof(((FixedValues_t *)0)->digest));

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    if (!codec) return;
    memset(&destination, 0xa5, sizeof(destination));
    FixedValues_init(&destination);
    check_equal(destination.enabled, 0u);
    check_equal(cmeta_data_fixed_is_zero(shape->fields[1].value,
                                         &destination.id, &(bool){false}),
                CMETA_OK);
    check_equal(FixedValues_from_json(codec, &destination, json, strlen(json),
                                      &error),
                DATA_BIND_OK);
    check_equal(destination.enabled, 1u);
    for (index = 0u; index < sizeof(destination.id.bytes); ++index)
      check_equal(destination.id.bytes[index], 0u);
    check_equal(memcmp(destination.digest, "0123456789abcdef",
                       sizeof(destination.digest)), 0);
    check_equal(FixedValues_to_json(codec, &destination, &encoded,
                                    &encoded_len, &error), DATA_BIND_OK);
    check_not_null(encoded);
    if (encoded)
      check_not_null(strstr(encoded, "\"digest\":\"0123456789abcdef\""));
    check_equal(FixedValues_to_bin(&destination, &wire, &wire_len, &error),
                DATA_BIND_OK);
    check_not_null(wire);
    if (wire) {
      FixedValues_t decoded;
      memset(&decoded, 0xa5, sizeof(decoded));
      FixedValues_init(&decoded);
      check_equal(FixedValues_from_bin(codec, &decoded, wire, wire_len, &error),
                  DATA_BIND_OK);
      check_equal(memcmp(&decoded, &destination, sizeof(decoded)), 0);
      FixedValues_clear(&decoded);
      check_equal(memcmp(&decoded, &(FixedValues_t){0}, sizeof(decoded)), 0);
    }
    tbe_typed_serialized_free(encoded);
    tbe_typed_serialized_free(wire);
    encoded = NULL;
    wire = NULL;

    memset(&destination, 0xa5, sizeof(destination));
    before = destination;

    for (index = 0u; index < 3u; ++index) {
      TbeTypedDescriptor altered_descriptor = *descriptor;
      cmeta_data_desc altered_root = *native;
      cmeta_data_struct_shape altered_shape = *shape;
      cmeta_data_field_desc altered_fields[3];
      cmeta_data_desc altered_value = *shape->fields[index].value;
      cmeta_type_desc altered_storage = *altered_value.storage_type;
      DataBindStatus status;

      memcpy(altered_fields, shape->fields, sizeof(altered_fields));
      if (index == 0u)
        altered_storage.size += 1u;
      else if (index == 1u)
        altered_storage.align += 1u;
      else
        altered_storage.size -= 1u;
      altered_value.storage_type = &altered_storage;
      altered_fields[index].value = &altered_value;
      altered_shape.fields = altered_fields;
      altered_root.shape = &altered_shape;
      altered_descriptor.native_data = &altered_root;

      status = tbe_typed_descriptor_parse(
          codec, "FixedValues", &altered_descriptor, DATA_BIND_FORMAT_JSON,
          json, strlen(json), 0u, &destination, &error);
      check_equal(status, DATA_BIND_ERR_SCHEMA);
      check_not_null(strstr(error.path, index == 0u ? "FixedValues.enabled" :
                                        index == 1u ? "FixedValues.id" :
                                                      "FixedValues.digest"));
      check_equal(memcmp(&destination, &before, sizeof(destination)), 0);
    }

    {
      TbeTypedDescriptor altered_descriptor = *descriptor;
      cmeta_data_desc altered_root = *native;
      cmeta_data_struct_shape altered_shape = *shape;
      cmeta_data_field_desc altered_fields[3];
      cmeta_data_desc altered_bytes = *shape->fields[2].value;
      cmeta_data_fixed_ops altered_ops =
          *cmeta_data_fixed_ops_of(shape->fields[2].value);
      uint8_t output[64];
      uint8_t output_before[64];
      char *failed_text = (char *)(uintptr_t)1u;
      size_t failed_len = 19u;

      memcpy(altered_fields, shape->fields, sizeof(altered_fields));
      altered_ops.copy = reject_fixed_copy;
      altered_bytes.fixed_ops = &altered_ops;
      altered_fields[2].value = &altered_bytes;
      altered_shape.fields = altered_fields;
      altered_root.shape = &altered_shape;
      altered_descriptor.native_data = &altered_root;
      memset(output, 0x5a, sizeof(output));
      memcpy(output_before, output, sizeof(output));
      check_equal(FixedValues_from_json(codec, &destination, json, strlen(json),
                                        &error), DATA_BIND_OK);
      reject_fixed_copy_hits = 0u;

      check_equal(tbe_typed_descriptor_serialize(
                      codec, "FixedValues", &altered_descriptor, &destination,
                      DATA_BIND_FORMAT_JSON, &failed_text, &failed_len, &error),
                  DATA_BIND_ERR_TYPE_MISMATCH);
      check_equal(reject_fixed_copy_hits, 1u);
      check_null(failed_text);
      check_equal(failed_len, 0u);
      failed_len = 0u;
      check_equal(tbe_typed_descriptor_serialize_binary_into(
                      &altered_descriptor, &destination, output, sizeof(output),
                      &failed_len, &error), DATA_BIND_ERR_TYPE_MISMATCH);
      check_equal(reject_fixed_copy_hits, 2u);
      check_equal(memcmp(output, output_before, sizeof(output)), 0);
    }
    FixedValues_clear(&destination);
    check_equal(memcmp(&destination, &(FixedValues_t){0}, sizeof(destination)), 0);
    data_bind_free(codec);
  }

  it("publishes exact signed and unsigned enum storage domains") {
    const cmeta_data_desc *signed8 = NULL;
    const cmeta_data_desc *unsigned8 = NULL;
    const cmeta_data_desc *signed64 = NULL;
    const cmeta_data_struct_shape *signed8_shape;
    const cmeta_data_struct_shape *unsigned8_shape;
    const cmeta_data_struct_shape *signed64_shape;
    const cmeta_data_desc *signed8_value;
    const cmeta_data_desc *unsigned8_value;
    const cmeta_data_desc *signed64_value;
    Signed8Domain_t signed8_storage = 0;
    Unsigned8Domain_t unsigned8_storage = 0;
    Signed64Domain_t signed64_storage = 0;
    uint64_t value = 0;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(Signed8Storage_cmeta_data(&signed8, &error), DATA_BIND_OK);
    check_equal(Unsigned8Storage_cmeta_data(&unsigned8, &error), DATA_BIND_OK);
    check_equal(Signed64Storage_cmeta_data(&signed64, &error), DATA_BIND_OK);
    check_not_null(signed8);
    check_not_null(unsigned8);
    check_not_null(signed64);
    if (!signed8 || !unsigned8 || !signed64) return;

    signed8_shape = (const cmeta_data_struct_shape *)signed8->shape;
    unsigned8_shape = (const cmeta_data_struct_shape *)unsigned8->shape;
    signed64_shape = (const cmeta_data_struct_shape *)signed64->shape;
    check_not_null(signed8_shape);
    check_not_null(unsigned8_shape);
    check_not_null(signed64_shape);
    if (!signed8_shape || !unsigned8_shape || !signed64_shape) return;
    check_equal(signed8_shape->field_count, 1u);
    check_equal(unsigned8_shape->field_count, 1u);
    check_equal(signed64_shape->field_count, 1u);
    if (signed8_shape->field_count != 1u ||
        unsigned8_shape->field_count != 1u ||
        signed64_shape->field_count != 1u)
      return;
    check_not_null(signed8_shape->fields);
    check_not_null(unsigned8_shape->fields);
    check_not_null(signed64_shape->fields);
    if (!signed8_shape->fields || !unsigned8_shape->fields ||
        !signed64_shape->fields)
      return;
    signed8_value = signed8_shape->fields[0].value;
    unsigned8_value = unsigned8_shape->fields[0].value;
    signed64_value = signed64_shape->fields[0].value;
    check_not_null(signed8_value);
    check_not_null(unsigned8_value);
    check_not_null(signed64_value);
    if (!signed8_value || !unsigned8_value || !signed64_value) return;
    check_equal(signed8_value->storage_type->size, sizeof(int8_t));
    check_equal(unsigned8_value->storage_type->size, sizeof(uint8_t));
    check_equal(signed64_value->storage_type->size, sizeof(int64_t));
    check_not_null(cmeta_data_enum_bits_ops_of(signed8_value));
    check_not_null(cmeta_data_enum_bits_ops_of(unsigned8_value));
    check_not_null(cmeta_data_enum_bits_ops_of(signed64_value));
    if (!cmeta_data_enum_bits_ops_of(signed8_value) ||
        !cmeta_data_enum_bits_ops_of(unsigned8_value) ||
        !cmeta_data_enum_bits_ops_of(signed64_value)) return;
    check_equal(signed8_value->enum_bits_ops->domain->signedness, CMETA_ENUM_SIGNED);
    check_equal(signed8_value->enum_bits_ops->domain->bits, 8u);
    check_equal(unsigned8_value->enum_bits_ops->domain->signedness, CMETA_ENUM_UNSIGNED);
    check_equal(unsigned8_value->enum_bits_ops->domain->bits, 8u);
    check_equal(signed64_value->enum_bits_ops->domain->signedness, CMETA_ENUM_SIGNED);
    check_equal(signed64_value->enum_bits_ops->domain->bits, 64u);
    check_equal(cmeta_data_enum_assign_bits(signed8_value, &signed8_storage,
                                            UINT64_C(128)), CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(signed8_value, &signed8_storage, &value),
                CMETA_OK);
    check_equal(value, UINT64_C(128));
    check_equal(signed8_storage, INT8_MIN);
    check_equal(cmeta_data_enum_assign_bits(unsigned8_value, &unsigned8_storage,
                                       UINT8_MAX), CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(unsigned8_value, &unsigned8_storage, &value),
                CMETA_OK);
    check_equal(value, UINT8_MAX);
    check_equal(cmeta_data_enum_assign_bits(signed64_value, &signed64_storage,
                                            UINT64_C(1) << 63), CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(signed64_value, &signed64_storage, &value),
                CMETA_OK);
    check_equal(value, UINT64_C(1) << 63);
    check_equal(signed64_storage, INT64_MIN);
  }

  it("round-trips UINT64_MAX through canonical bits and descriptor binary APIs") {
    const TbeTypedDescriptor *descriptor = WideEnumStorage_typed_descriptor();
    DataBind *codec = NULL;
    const cmeta_data_desc *data;
    const cmeta_data_desc *enum_data;
    const cmeta_data_struct_shape *shape;
    WideEnumStorage_t object = {0};
    WideEnumStorage_t decoded = {0};
    uint64_t value = 0u;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(sizeof(WideDomain_t), sizeof(uint64_t));
    check(WideDomain_Maximum == UINT64_MAX);
    check_not_null(descriptor);
    if (!descriptor) return;
    check_equal(tbe_typed_descriptor_validate(descriptor, &error), DATA_BIND_OK);
    data = descriptor->native_data;
    check_not_null(data);
    if (!data) return;
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_not_null(shape);
    if (!shape) return;
    check_equal(shape->field_count, 1u);
    if (shape->field_count != 1u) return;
    check_not_null(shape->fields);
    if (!shape->fields) return;
    enum_data = shape->fields[0].value;
    check_not_null(enum_data);
    if (!enum_data) return;
    check_equal(enum_data->kind, CMETA_DATA_ENUM);
    check_equal(enum_data->storage_type->size, sizeof(uint64_t));
    check_not_null(cmeta_data_enum_bits_ops_of(enum_data));

    check_equal(tbe_typed_descriptor_init(descriptor, &object, &error),
                DATA_BIND_OK);
    check_equal(tbe_typed_descriptor_init(descriptor, &decoded, &error),
                DATA_BIND_OK);
    check_equal(cmeta_data_enum_assign_bits(enum_data, &object.value,
                                            UINT64_MAX), CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(enum_data, &object.value, &value),
                CMETA_OK);
    check(value == UINT64_MAX);
    check(object.value == UINT64_MAX);
    check_equal(tbe_typed_descriptor_serialize_binary(
                    descriptor, &object, &wire, &wire_len, &error),
                DATA_BIND_OK);
    check_not_null(wire);
    check_equal(wire_len, sizeof(uint64_t));
    if (wire) {
      check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
      check_not_null(codec);
      check_equal(tbe_typed_descriptor_parse(
                      codec, "WideEnumStorage", descriptor,
                      DATA_BIND_FORMAT_BINARY, wire, wire_len, 0u,
                      &decoded, &error),
                  DATA_BIND_OK);
      value = 0u;
      check_equal(cmeta_data_enum_read_bits(
                      enum_data, &decoded.value, &value),
                  CMETA_OK);
      check(value == UINT64_MAX);
      check(decoded.value == UINT64_MAX);
    }
    data_bind_free(codec);
    tbe_typed_serialized_free(wire);
    check_equal(tbe_typed_descriptor_clear(descriptor, &decoded, &error),
                DATA_BIND_OK);
    check_equal(tbe_typed_descriptor_clear(descriptor, &object, &error),
                DATA_BIND_OK);
  }

  it("rejects an unknown enum through canonical bits and preserves the object") {
    static const char json[] = "{\"value\":42}";
    const TbeTypedDescriptor *descriptor = Signed8Storage_typed_descriptor();
    const cmeta_data_desc *data;
    const cmeta_data_desc *enum_data;
    const cmeta_data_struct_shape *shape;
    DataBind *codec = NULL;
    Signed8Domain_t candidate = 0;
    Signed8Storage_t object = {.value = Signed8Domain_Maximum};
    Signed8Storage_t before = object;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(descriptor);
    if (!descriptor) return;
    data = descriptor->native_data;
    check_not_null(data);
    if (!data) return;
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_not_null(shape);
    if (!shape) return;
    check_equal(shape->field_count, 1u);
    if (shape->field_count != 1u) return;
    check_not_null(shape->fields);
    if (!shape->fields) return;
    enum_data = shape->fields[0].value;
    check_not_null(enum_data);
    if (!enum_data) return;
    check_equal(cmeta_data_enum_assign_bits(enum_data, &candidate,
                                            UINT64_C(42)),
                CMETA_INVALID_ARGUMENT);
    check_equal(candidate, 0);

    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(tbe_typed_descriptor_parse(
                    codec, "Signed8Storage", descriptor,
                    DATA_BIND_FORMAT_JSON, json, sizeof(json) - 1u, 0u,
                    &object, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(object.value, before.value);
    check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
    data_bind_free(codec);
  }

  it("accepts a flags mask and rejects invalid bits without mutation") {
    static const char invalid_json[] = "{\"value\":4}";
    const TbeTypedDescriptor *descriptor = FlagStorage_typed_descriptor();
    const cmeta_data_desc *data;
    const cmeta_data_desc *enum_data;
    const cmeta_data_struct_shape *shape;
    DataBind *codec = NULL;
    FlagStorage_t object = {0};
    FlagStorage_t before;
    uint64_t bits = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(descriptor);
    if (!descriptor) return;
    data = descriptor->native_data;
    check_not_null(data);
    if (!data) return;
    shape = (const cmeta_data_struct_shape *)data->shape;
    check_not_null(shape);
    if (!shape) return;
    check_equal(shape->field_count, 1u);
    if (shape->field_count != 1u) return;
    check_not_null(shape->fields);
    if (!shape->fields) return;
    enum_data = shape->fields[0].value;
    check_not_null(enum_data);
    if (!enum_data) return;
    check_not_null(cmeta_data_enum_bits_ops_of(enum_data));
    check_equal(cmeta_data_enum_assign_bits(
                    enum_data, &object.value,
                    UINT64_C(1) | UINT64_C(2)),
                CMETA_OK);
    check_equal(cmeta_data_enum_read_bits(enum_data, &object.value, &bits),
                CMETA_OK);
    check_equal(bits, UINT64_C(3));
    check_equal(object.value, Permission_Read | Permission_Write);
    check_equal(cmeta_data_enum_bits_restore_zero(enum_data, &object.value),
                CMETA_OK);
    before = object;
    check_equal(cmeta_data_enum_assign_bits(enum_data, &object.value,
                                            UINT64_C(4)),
                CMETA_INVALID_ARGUMENT);
    check_equal(object.value, before.value);

    check_equal(cmeta_data_enum_assign_bits(
                    enum_data, &object.value,
                    UINT64_C(1) | UINT64_C(2)),
                CMETA_OK);
    before = object;
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(tbe_typed_descriptor_parse(
                    codec, "FlagStorage", descriptor,
                    DATA_BIND_FORMAT_JSON, invalid_json,
                    sizeof(invalid_json) - 1u, 0u, &object, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(error.code, DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(object.value, before.value);
    data_bind_free(codec);
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
