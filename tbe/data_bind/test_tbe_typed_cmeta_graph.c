#include "tinytest.h"
#include "tbe_typed_internal.h"
#include <salts_cmeta_fixed_width.h>
#include <string.h>

/* Include the real generated implementation to inspect internal metadata.
 * The central descriptor ABI is unchanged and the private validator is not
 * installed; the generated public header exposes per-record graph getters. */
#include "cmeta_graph_generated.c"

spec("generated native CMeta graph") {
  it("publishes a validated graph from real generated native storage") {
#ifndef TBE_GENERATED_CMETA_GRAPH
    check_true(0 && "generated native CMeta graph is missing");
#else
    const cmeta_data_desc *data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(Sample_cmeta_data(&data, &error), DATA_BIND_OK);
    check_true(data != NULL);
    if (data) {
      const cmeta_data_struct_shape *shape = data->shape;
      const cmeta_data_struct_shape *point = shape->fields[0].value->shape;
      const cmeta_data_enum_shape *state = shape->fields[1].value->shape;
      cmeta_data_desc copy = *data;
      cmeta_type_desc storage_copy = *data->storage_type;
      cmeta_type_identity identity_copy = *storage_copy.identity;
      storage_copy.identity = &identity_copy;
      copy.storage_type = &storage_copy;
      check_true(cmeta_data_desc_valid(data));
      check_equal(data->kind, CMETA_DATA_STRUCT);
      check_equal(data->storage_type->size, sizeof(Sample_t));
      check_equal(data->storage_type->align, _Alignof(Sample_t));
      check_equal(shape->field_count, 3u);
      check_equal(shape->fields[0].offset, offsetof(Sample_t, point));
      check_equal(shape->fields[1].offset, offsetof(Sample_t, state));
      check_equal(point->field_count, 2u);
      check_equal(point->fields[1].offset, offsetof(Point_t, y));
      check_true(cmeta_type_equal(point->fields[0].value->storage_type,
                                 salts_int32_cmeta_data.storage_type));
      check_equal(shape->fields[1].value->kind, CMETA_DATA_ENUM);
      check_equal(shape->fields[1].value->storage_type->size, sizeof(State_t));
      check_equal(state->meta->count, 2u);
      check_equal(state->meta->items[1].value, 7);
      check_equal(state->meta->items[1].symbol, "Ready");
      check_equal(shape->fields[2].name, "count");
      check_true(cmeta_data_struct_find_field(shape, "wire_count") == NULL);
      check_true(cmeta_data_struct_find_field(shape, "old_count") == NULL);
      check_true(cmeta_type_equal(copy.storage_type, data->storage_type));
      check_equal(tbe_typed_cmeta_graph_validate(&Sample_TYPED_TYPE, &copy, &error), DATA_BIND_OK);
      storage_copy.size += 1u;
      check_equal(tbe_typed_cmeta_graph_validate(&Sample_TYPED_TYPE, &copy, &error), DATA_BIND_ERR_SCHEMA);
      storage_copy = *data->storage_type;
      {
        cmeta_data_struct_shape altered = *shape;
        cmeta_data_field_desc fields[3];
        memcpy(fields, shape->fields, sizeof(fields));
        fields[2].value = &salts_uint32_cmeta_data;
        altered.fields = fields;
        copy.shape = &altered;
        check_equal(tbe_typed_cmeta_graph_validate(&Sample_TYPED_TYPE, &copy, &error), DATA_BIND_ERR_SCHEMA);
      }
      check_true(Sample_TYPED_FIELDS[2].flags & TBE_TYPED_FIELD_WIRE_OFFSET);
      check_equal(Sample_TYPED_FIELDS[2].wire_offset, 14u);
      check_true(Sample_TYPED_FIELDS[2].wire_offset != shape->fields[2].offset);
    }
#endif
  }
  it("rejects unsupported and optional storage without publishing partial output") {
#ifndef TBE_GENERATED_CMETA_GRAPH
    check_true(0 && "generated native CMeta graph is missing");
#else
    const cmeta_data_desc *sentinel = &salts_int32_cmeta_data;
    const cmeta_data_desc *data = sentinel;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(Unsupported_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check_true(data == sentinel);
    check_true(error.path[0] != '\0');
    check_equal(OptionalStorage_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check_true(data == sentinel);
    check_equal(UnsupportedNested_cmeta_data(&data, &error), DATA_BIND_ERR_SCHEMA);
    check_true(data == sentinel);
#endif
  }
#ifndef TBE_CMETA_NODE_FRONTEND_SMOKE
  it("keeps defaults and wire aliases in the runtime schema overlay") {
#ifndef TBE_GENERATED_CMETA_GRAPH
    check_true(0 && "generated native CMeta graph is missing");
#else
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    const cmeta_data_desc *data = NULL;
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    if (codec) {
      check_equal(Sample_cmeta_data(&data, &error), DATA_BIND_OK);
      check_equal(tbe_typed_validate_schema(codec, "Sample", &Sample_TYPED_TYPE, &error), DATA_BIND_OK);
      check_true(data_bind_schema_field_at(codec, "Sample", 2u, &field));
      check_true(field.has_default);
      check_equal(field.default_value, "9");
      check_equal(field.name, "count");
      check_equal(field.offset, 14u);
      data_bind_free(codec);
    }
#endif
  }
#endif
}
