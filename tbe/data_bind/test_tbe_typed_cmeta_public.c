#include <node_tree.h>
#include <schema_cmeta.h>
#include "cmeta_graph_generated.h"
#include <salts_cmeta_data.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Public-only, release-build-safe checks: no private validator or generated
 * implementation include may make this consumer link accidentally. */
int main(void) {
  const struct cmeta_data_desc *data = NULL;
  const struct cmeta_data_desc *sentinel;
  DataBindError error = DATA_BIND_ERROR_INIT;

  if (Sample_cmeta_data(&data, &error) != DATA_BIND_OK || data == NULL) {
    fputs("public graph getter did not publish a valid graph\n", stderr);
    return 1;
  }
  sentinel = data;
  if (Unsupported_cmeta_data(&data, &error) != DATA_BIND_ERR_SCHEMA) {
    fputs("unsupported public graph request did not fail\n", stderr);
    return 2;
  }
  if (data != sentinel) {
    fputs("failed public graph request changed the output sentinel\n", stderr);
    return 3;
  }
  if (error.code != DATA_BIND_ERR_SCHEMA || error.path[0] == '\0' || error.message[0] == '\0')
    return 13;
  /* Mutation: a successful graph query retains a previous failure. Seed the
   * location too, because native graph failures have no source position. */
  error.line = 23;
  error.column = 7;
  if (Sample_cmeta_data(&data, &error) != DATA_BIND_OK || data == NULL)
    return 14;
  if (error.code != DATA_BIND_OK || error.path[0] != '\0' || error.message[0] != '\0' ||
      error.line != -1 || error.column != -1) {
    fprintf(stderr, "successful public graph request retained stale error: code=%d, "
                    "path='%s', message='%s', line=%d, column=%d\n",
            (int)error.code, error.path, error.message, error.line, error.column);
    return 15;
  }
  if (UuidStorage_cmeta_data(&data, &error) != DATA_BIND_OK || data == NULL)
    return 4;
  {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    Node *root = create_node_map("root");
    Node *field = create_node_map(NULL);
    schema_cmeta_field_type semantic;
    int valid;
    if (root == NULL || field == NULL) { node_free(root); node_free(field); return 5; }
    if (map_add(field, create_node_string("type", "uuid")) != 0) {
      node_free(root); node_free(field); return 6;
    }
    valid = schema_cmeta_field_resolve(root, field, &semantic) &&
        semantic.kind == CMETA_DATA_CUSTOM && salts_uuid_cmeta_data_valid(semantic.data) &&
        salts_uuid_cmeta_data_valid(shape->fields[0].value) &&
        cmeta_type_equal(semantic.data->storage_type, shape->fields[0].value->storage_type);
    node_free(field); node_free(root);
    if (!valid) return 7;
  }
  sentinel = data;
  if (data_bind_schema_field_cmeta_data(NULL, "Shape", 0u, &data, &error) != DATA_BIND_ERR_INVALID_ARG ||
      data != sentinel || error.code != DATA_BIND_ERR_INVALID_ARG)
    return 8;
  if (Scalars_cmeta_data(&data, &error) != DATA_BIND_OK || data == NULL)
    return 9;
  {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    const cmeta_data_field_desc *field = cmeta_data_struct_find_field(shape, "u64c");
    if (shape->field_count != 29u || field == NULL ||
        !cmeta_type_equal(field->value->storage_type, &salts_uint64_cmeta_type))
      return 10;
  }
  if (FlagStorage_cmeta_data(&data, &error) != DATA_BIND_OK || data == NULL)
    return 11;
  {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    const cmeta_data_desc *enum_data = shape->fields[0].value;
    const cmeta_data_enum_shape *flags = (const cmeta_data_enum_shape *)shape->fields[0].value->shape;
    Permission_t value = 0;
    int64_t bits = 0;
    if (flags->meta->count != 2u || flags->meta->items[1].value != 2 ||
        cmeta_data_enum_ops_of(enum_data) == NULL ||
        cmeta_data_enum_assign(enum_data, &value,
                               Permission_Read | Permission_Write) != CMETA_OK ||
        cmeta_data_enum_read(enum_data, &value, &bits) != CMETA_OK ||
        bits != (Permission_Read | Permission_Write))
      return 12;
  }
  sentinel = data;
  if (WideEnumStorage_cmeta_data(&data, &error) != DATA_BIND_OK ||
      data == sentinel)
    return 28;
  {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    const cmeta_data_desc *enum_data = shape->fields[0].value;
    if (enum_data->kind != CMETA_DATA_ENUM ||
        enum_data->storage_type->size != sizeof(uint64_t) ||
        cmeta_data_enum_ops_of(enum_data) == NULL)
      return 29;
  }
  {
    const TbeTypedDescriptor *descriptor = FixedValues_typed_descriptor();
    const cmeta_data_desc *fixed = descriptor ? descriptor->native_data : NULL;
    const cmeta_data_struct_shape *shape =
        fixed ? (const cmeta_data_struct_shape *)fixed->shape : NULL;
    size_t extent = 0u;
    if (descriptor == NULL ||
        tbe_typed_descriptor_validate(descriptor, &error) != DATA_BIND_OK ||
        shape == NULL || shape->field_count != 3u)
      return 26;
    if (shape->fields[0].value->kind != CMETA_DATA_BOOL ||
        shape->fields[0].value->storage_type->size !=
            sizeof(((FixedValues_t *)0)->enabled) ||
        !salts_uuid_cmeta_data_valid(shape->fields[1].value) ||
        shape->fields[2].value->kind != CMETA_DATA_BYTES ||
        shape->fields[2].value->storage_type->size !=
            sizeof(((FixedValues_t *)0)->digest) ||
        cmeta_data_fixed_extent(shape->fields[2].value, &extent) != CMETA_OK ||
        extent != sizeof(((FixedValues_t *)0)->digest))
      return 27;
  }
  {
    static const char json[] =
        "{\"point\":{\"x\":3,\"y\":4.5},\"state\":7,\"wire_count\":7}";
    const TbeTypedDescriptor *descriptor = Sample_typed_descriptor();
    const cmeta_data_desc *root_data;
    const cmeta_data_struct_shape *root_shape;
    const cmeta_data_field_desc *count_field;
    cmeta_data_desc root_data_copy;
    cmeta_data_struct_shape root_shape_copy;
    cmeta_data_field_desc fields_copy[3];
    cmeta_data_desc count_data_copy;
    cmeta_type_desc count_type_copy;
    cmeta_type_identity count_identity_copy;
    TbeTypedDescriptor descriptor_copy;
    DataBind *codec = NULL;
    Sample_t actual = {0};
    Sample_t before;
    DataBindStatus status;
    size_t count_index;

    if (descriptor == NULL || descriptor->native_data == NULL) return 16;
    root_data = descriptor->native_data;
    root_shape = (const cmeta_data_struct_shape *)root_data->shape;
    if (root_shape == NULL || root_shape->field_count != 3u) return 23;
    count_field = cmeta_data_struct_find_field(root_shape, "count");
    if (count_field == NULL) return 24;
    count_index = (size_t)(count_field - root_shape->fields);

    descriptor_copy = *descriptor;
    root_data_copy = *root_data;
    root_shape_copy = *root_shape;
    memcpy(fields_copy, root_shape->fields, sizeof(fields_copy));
    count_data_copy = *count_field->value;
    count_type_copy = *count_data_copy.storage_type;
    count_identity_copy = *count_type_copy.identity;
    count_type_copy.identity = &count_identity_copy;
    count_data_copy.storage_type = &count_type_copy;
    fields_copy[count_index].value = &count_data_copy;
    root_shape_copy.fields = fields_copy;
    root_data_copy.shape = &root_shape_copy;

    if (&count_data_copy == count_field->value) return 17;
    if (!cmeta_type_equal(count_data_copy.storage_type,
                          count_field->value->storage_type))
      return 18;
    descriptor_copy.native_data = &root_data_copy;
    if (Graph_codec_create(&codec, &error) != DATA_BIND_OK || codec == NULL)
      return 25;
    status = tbe_typed_descriptor_parse(codec, "Sample", &descriptor_copy,
                                        DATA_BIND_FORMAT_JSON, json,
                                        strlen(json), 0u, &actual, &error);
    if (status != DATA_BIND_OK || actual.count != 7) {
      data_bind_free(codec);
      return 19;
    }

    {
      cmeta_data_desc bad_root_data = root_data_copy;
      cmeta_data_struct_shape bad_root_shape = root_shape_copy;
      cmeta_data_field_desc bad_fields[3];
      memcpy(bad_fields, fields_copy, sizeof(bad_fields));
      bad_fields[count_index].offset = offsetof(Sample_t, state);
      bad_root_shape.fields = bad_fields;
      bad_root_data.shape = &bad_root_shape;
      actual.point.x = 91;
      actual.point.y = 8.25;
      actual.state = State_Ready;
      actual.count = 31;
      before = actual;
      descriptor_copy.native_data = &bad_root_data;
      status = tbe_typed_descriptor_parse(codec, "Sample", &descriptor_copy,
                                          DATA_BIND_FORMAT_JSON, json,
                                          strlen(json), 0u, &actual, &error);
      data_bind_free(codec);
      if (status != DATA_BIND_ERR_SCHEMA) return 20;
      if (strstr(error.path, "Sample.count") == NULL) return 21;
      if (memcmp(&actual, &before, sizeof(actual)) != 0) return 22;
    }
  }
  return 0;
}
