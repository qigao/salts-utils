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
  {
    Depth33_t lifecycle;
    Depth33_t zero;
    memset(&zero, 0, sizeof(zero));
    memset(&lifecycle, 0xa5, sizeof(lifecycle));
    Depth33_init(&lifecycle);
    if (memcmp(&lifecycle, &zero, sizeof(lifecycle)) != 0) return 38;
    memset(&lifecycle, 0x5a, sizeof(lifecycle));
    Depth33_clear(&lifecycle);
    if (memcmp(&lifecycle, &zero, sizeof(lifecycle)) != 0) return 39;
    Depth33_clear(&lifecycle);
  }
  {
    Sample_t lifecycle;
    memset(&lifecycle, 0xa5, sizeof(lifecycle));
    Sample_init(&lifecycle);
    if (lifecycle.point.x != 0 || lifecycle.point.y != 0.0 ||
        lifecycle.state != 0 || lifecycle.count != 0)
      return 33;
    lifecycle.point.x = 9;
    lifecycle.point.y = 2.5;
    lifecycle.state = State_Ready;
    lifecycle.count = 17;
    Sample_clear(&lifecycle);
    if (lifecycle.point.x != 0 || lifecycle.point.y != 0.0 ||
        lifecycle.state != 0 || lifecycle.count != 0)
      return 34;
  }
  {
    Unsupported_t lifecycle;
    tstr owned;
    memset(&lifecycle, 0xa5, sizeof(lifecycle));
    Unsupported_init(&lifecycle);
    if (lifecycle.bad != NULL)
      return 35;
    owned = tstr_dup("alice");
    if (owned == NULL)
      return 36;
    lifecycle.bad = owned;
    Unsupported_clear(&lifecycle);
    if (lifecycle.bad != NULL)
      return 37;
  }
  {
    const cmeta_data_desc *root = NULL;
    const cmeta_data_struct_shape *shape;
    const cmeta_data_desc *value_data;
    EnumSymbolStorage_t object = {0};
    DataBind *codec = NULL;
    const EnumSymbols_t items[] = {
        EnumSymbols_CMETA_DOMAIN, EnumSymbols_CMETA_ENUM_OPS,
        EnumSymbols_CMETA_BITS_OPS, EnumSymbols_CMETA_DATA, EnumSymbols_CMETA_TYPE,
        EnumSymbols_CMETA_ID, EnumSymbols_CMETA_ITEMS, EnumSymbols_cmeta_is_zero,
        EnumSymbols_cmeta_read, EnumSymbols_cmeta_assign, EnumSymbols_cmeta_restore_zero};
    size_t i;
    if (EnumSymbolStorage_cmeta_data(&root, &error) != DATA_BIND_OK ||
        root == NULL || root->shape == NULL)
      return 30;
    shape = (const cmeta_data_struct_shape *)root->shape;
    if (!shape || shape->field_count != 1u || !shape->fields || !shape->fields[0].value)
      return 31;
    value_data = shape->fields[0].value;
    if (Graph_codec_create(&codec, &error) != DATA_BIND_OK || codec == NULL)
      return 32;
    for (i = 0u; i < sizeof(items) / sizeof(items[0]); ++i) {
      uint64_t bits = 0u;
      uint8_t wire[2] = {0};
      size_t wire_len = 0u;
      if (cmeta_data_enum_bits_restore_zero(value_data, &object.value) != CMETA_OK ||
          cmeta_data_enum_assign_bits(value_data, &object.value, items[i]) != CMETA_OK ||
          cmeta_data_enum_read_bits(value_data, &object.value, &bits) != CMETA_OK ||
          bits != i + 1u || object.value != items[i] ||
          EnumSymbolStorage_to_bin_into(codec, &object, wire, sizeof(wire), &wire_len,
                                        &error) != DATA_BIND_ERR_SCHEMA ||
          wire_len != 0u || wire[0] != 0u || wire[1] != 0u)
        { data_bind_free(codec); return 32; }
    }
    data_bind_free(codec);
  }
  if (Depth33_cmeta_data(&data, &error) != DATA_BIND_ERR_SCHEMA) {
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
    const IdlContract contract = {
        sizeof(IdlContract), IDL_CONTRACT_ABI_VERSION,
        "Public", "1", 0u, NULL, 0u, NULL, 0u, NULL, 0u, NULL};
    const IdlField field = {
        "uuid", "uuid", IDL_COLLECTION_NONE,
        NULL, NULL, NULL, NULL, NULL,
        0, 0, 0u, NULL, 0u, NULL};
    schema_cmeta_field_type semantic;
    int valid = schema_cmeta_field_resolve(&contract, &field, &semantic) &&
        semantic.kind == CMETA_DATA_CUSTOM && salts_uuid_cmeta_data_valid(semantic.data) &&
        salts_uuid_cmeta_data_valid(shape->fields[0].value) &&
        cmeta_type_equal(semantic.data->storage_type, shape->fields[0].value->storage_type);
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
        !cmeta_type_equal(field->value->storage_type, &cmeta_type_uint64))
      return 10;
  }
  {
    const cmeta_data_struct_shape *shape;
    const cmeta_data_desc *root = NULL;
    const cmeta_data_desc *enum_data;
    Permission_t value = 0;
    uint64_t bits = 0u;
    if (FlagStorage_cmeta_data(&root, &error) != DATA_BIND_OK ||
        root == NULL || root->shape == NULL)
      return 11;
    shape = (const cmeta_data_struct_shape *)root->shape;
    if (shape->field_count != 1u || shape->fields == NULL ||
        shape->fields[0].value == NULL)
      return 12;
    enum_data = shape->fields[0].value;
    if (cmeta_data_enum_bits_ops_of(enum_data) == NULL ||
        !cmeta_data_value_move_supported(enum_data) ||
        !cmeta_data_value_move_supported(root) ||
        cmeta_data_enum_assign_bits(enum_data, &value,
                                    UINT64_C(1) | UINT64_C(2)) != CMETA_OK ||
        cmeta_data_enum_read_bits(enum_data, &value, &bits) != CMETA_OK ||
        bits != UINT64_C(3))
      return 12;
  }
  {
    DataBind *codec = NULL;
    int failed;
    const cmeta_data_struct_shape *shape;
    const cmeta_data_desc *root = NULL;
    const cmeta_data_desc *enum_data;
    WideEnumStorage_t object = {0};
    uint8_t wire[sizeof(uint64_t)] = {0};
    size_t wire_len = 0u;
    uint64_t bits = 0u;
    if (WideEnumStorage_cmeta_data(&root, &error) != DATA_BIND_OK ||
        root == NULL || root->shape == NULL)
      return 28;
    shape = (const cmeta_data_struct_shape *)root->shape;
    if (shape->field_count != 1u || shape->fields == NULL ||
        shape->fields[0].value == NULL)
      return 29;
    enum_data = shape->fields[0].value;
    if (Graph_codec_create(&codec, &error) != DATA_BIND_OK || codec == NULL)
      return 29;
    failed = enum_data->kind != CMETA_DATA_ENUM ||
        enum_data->storage_type->size != sizeof(uint64_t) ||
        cmeta_data_enum_bits_ops_of(enum_data) == NULL ||
        !cmeta_data_value_move_supported(enum_data) ||
        !cmeta_data_value_move_supported(root) ||
        cmeta_data_enum_assign_bits(enum_data, &object.value,
                                    UINT64_MAX) != CMETA_OK ||
        WideEnumStorage_to_bin_into(
            codec, &object, wire, sizeof(wire), &wire_len,
            &error) != DATA_BIND_ERR_SCHEMA ||
        wire_len != 0u ||
        cmeta_data_enum_read_bits(enum_data, &object.value,
                                  &bits) != CMETA_OK ||
        bits != UINT64_MAX || object.value != UINT64_MAX;
    data_bind_free(codec);
    if (failed) return 29;
  }
  {
    const cmeta_data_desc *fixed = NULL;
    const cmeta_data_struct_shape *shape = NULL;
    size_t extent = 0u;
    if (FixedValues_cmeta_data(&fixed, &error) != DATA_BIND_OK ||
        fixed == NULL || fixed->shape == NULL)
      return 26;
    shape = (const cmeta_data_struct_shape *)fixed->shape;
    if (shape->field_count != 3u)
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
    const cmeta_data_desc *root_data = NULL;
    const cmeta_data_struct_shape *root_shape;
    const cmeta_data_field_desc *count_field;
    cmeta_data_desc count_data_copy;
    cmeta_type_desc count_type_copy;
    cmeta_type_identity count_identity_copy;
    DataBind *codec = NULL;
    Sample_t actual = {0};

    if (Sample_cmeta_data(&root_data, &error) != DATA_BIND_OK ||
        root_data == NULL || root_data->shape == NULL)
      return 16;
    root_shape = (const cmeta_data_struct_shape *)root_data->shape;
    if (root_shape->field_count != 3u) return 23;
    count_field = cmeta_data_struct_find_field(root_shape, "count");
    if (count_field == NULL || count_field->value == NULL ||
        count_field->value->storage_type == NULL ||
        count_field->value->storage_type->identity == NULL)
      return 24;

    count_data_copy = *count_field->value;
    count_type_copy = *count_data_copy.storage_type;
    count_identity_copy = *count_type_copy.identity;
    count_type_copy.identity = &count_identity_copy;
    count_data_copy.storage_type = &count_type_copy;

    if (&count_data_copy == count_field->value) return 17;
    if (!cmeta_type_equal(count_data_copy.storage_type,
                          count_field->value->storage_type))
      return 18;

    if (Graph_codec_create(&codec, &error) != DATA_BIND_OK || codec == NULL)
      return 25;
    Sample_init(&actual);
    if (Sample_from_json(
            codec, &actual, json, sizeof(json) - 1u, &error) != DATA_BIND_OK ||
        actual.point.x != 3 || actual.point.y != 4.5 ||
        actual.state != State_Ready || actual.count != 7) {
      Sample_clear(&actual);
      data_bind_free(codec);
      return 19;
    }
    Sample_clear(&actual);
    data_bind_free(codec);
  }
  return 0;
}
