#include <node_tree.h>
#include <schema_cmeta.h>
#include "cmeta_graph_generated.h"
#include <salts_cmeta_data.h>
#include <stdio.h>

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
    const cmeta_data_enum_shape *flags = (const cmeta_data_enum_shape *)shape->fields[0].value->shape;
    if (flags->meta->count != 2u || flags->meta->items[1].value != 2)
      return 12;
  }
  return 0;
}
