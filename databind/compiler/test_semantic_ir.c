#include "semantic_ir.h"
#include "native_lowering.h"

#include <salts_cmeta_data.h>
#include <acutest.h>
#include <string.h>

static Node *add_record(Node *root, const char *name) {
  Node *messages = create_node_list("messages");
  Node *record = create_node_map(NULL);
  Node *fields = create_node_list("fields");
  TEST_ASSERT(root != NULL && messages != NULL && record != NULL && fields != NULL);
  TEST_ASSERT(map_add(root, messages) == 0);
  TEST_ASSERT(map_add(record, create_node_string("name", name)) == 0);
  TEST_ASSERT(map_add(record, fields) == 0);
  TEST_ASSERT(list_add(messages, record) == 0);
  return record;
}

static Node *add_field(Node *record, const char *owner, const char *name,
                       const char *type) {
  Node *fields = NULL;
  Node *field = create_node_map(NULL);
  size_t i;
  for (i = 0u; i < record->data.map.count; ++i)
    if (record->data.map.items[i]->name != NULL &&
        strcmp(record->data.map.items[i]->name, "fields") == 0)
      fields = record->data.map.items[i];
  TEST_ASSERT(fields != NULL && field != NULL);
  TEST_ASSERT(map_add(field, create_node_string("owner_name", owner)) == 0);
  TEST_ASSERT(map_add(field, create_node_string("name", name)) == 0);
  TEST_ASSERT(map_add(field, create_node_string("type", type)) == 0);
  TEST_ASSERT(list_add(fields, field) == 0);
  return field;
}

static void semantic_ir_keeps_projection_data_out(void) {
  Node *root = create_node_map("root");
  Node *record = add_record(root, "Sample");
  Node *field = add_field(record, "Sample", "count", "uint32");
  databind_semantic_field_ir ir = {0};

  TEST_CHECK(databind_semantic_field_build(root, field, &ir));
  TEST_CHECK(ir.struct_size == sizeof(ir));
  TEST_CHECK(ir.abi_version == DATABIND_SEMANTIC_IR_ABI_VERSION);
  TEST_CHECK(strcmp(ir.owner_name, "Sample") == 0);
  TEST_CHECK(strcmp(ir.name, "count") == 0);
  TEST_CHECK(strcmp(ir.declared_type, "uint32") == 0);
  TEST_CHECK(ir.semantic.kind == CMETA_DATA_UINT);
  TEST_CHECK(strcmp(ir.semantic.schema_kind, "scalar") == 0);
  TEST_CHECK(ir.semantic.data != NULL);
  TEST_CHECK(strcmp(ir.semantic.data->stable_id, "salts.uint32.data") == 0);

  node_free(root);
}

static void semantic_ir_normalizes_container_shape(void) {
  Node *root = create_node_map("root");
  Node *record = add_record(root, "Sample");
  Node *field = add_field(record, "Sample", "items", "int32");
  databind_semantic_field_ir ir = {0};

  TEST_ASSERT(map_add(field, create_node_string("is_list", "1")) == 0);
  TEST_ASSERT(map_add(field, create_node_string("inner_type", "int32")) == 0);

  TEST_CHECK(databind_semantic_field_build(root, field, &ir));
  TEST_CHECK(ir.semantic.kind == CMETA_DATA_SEQUENCE);
  TEST_CHECK(strcmp(ir.semantic.schema_kind, "list") == 0);
  TEST_CHECK(ir.semantic.data == &cmeta_data_sequence);

  node_free(root);
}

static void semantic_ir_retains_logical_state(void) {
  Node *root = create_node_map("root");
  Node *record = add_record(root, "Sample");
  Node *field = add_field(record, "Sample", "value", "string");
  databind_semantic_field_ir ir = {0};

  TEST_ASSERT(map_add(field, create_node_string("is_optional", "1")) == 0);
  TEST_ASSERT(map_add(field, create_node_string("is_nullable", "1")) == 0);

  TEST_CHECK(databind_semantic_field_build(root, field, &ir));
  TEST_CHECK(ir.semantic.kind == CMETA_DATA_STRING);
  TEST_CHECK(ir.is_optional == 1);
  TEST_CHECK(ir.is_nullable == 1);

  node_free(root);
}

static void native_lowering_selects_explicit_tstr_provider(void) {
  Node *root = create_node_map("root");
  Node *record = add_record(root, "Sample");
  Node *field = add_field(record, "Sample", "text", "string");
  databind_semantic_field_ir semantic = {0};
  databind_native_field_ir native = {0};

  TEST_CHECK(databind_semantic_field_build(root, field, &semantic));
  TEST_CHECK(semantic.semantic.kind == CMETA_DATA_STRING);
  TEST_CHECK(semantic.semantic.data == NULL);
  TEST_CHECK(databind_native_field_lower_generated_c(&semantic, &native));
  TEST_CHECK(native.struct_size == sizeof(native));
  TEST_CHECK(native.abi_version == DATABIND_NATIVE_FIELD_IR_ABI_VERSION);
  TEST_CHECK(strcmp(native.c_storage_type, "tstr") == 0);
  TEST_CHECK(native.native_data == &salts_tstr_cmeta_data);
  TEST_CHECK(native.native_type == &salts_tstr_cmeta_type);
  TEST_CHECK(strcmp(native.native_data_symbol, "salts_tstr_cmeta_data") == 0);
  TEST_CHECK(strcmp(native.native_type_symbol, "salts_tstr_cmeta_type") == 0);
  TEST_CHECK(native.provider_external == 1);

  node_free(root);
}

static void native_lowering_keeps_unimplemented_state_fail_closed(void) {
  Node *root = create_node_map("root");
  Node *record = add_record(root, "Sample");
  Node *field = add_field(record, "Sample", "text", "string");
  databind_semantic_field_ir semantic = {0};
  databind_native_field_ir native = {0};

  TEST_ASSERT(map_add(field, create_node_string("is_optional", "1")) == 0);
  TEST_CHECK(databind_semantic_field_build(root, field, &semantic));
  native.struct_size = 777u;
  TEST_CHECK(!databind_native_field_lower_generated_c(&semantic, &native));
  TEST_CHECK(native.struct_size == 777u);

  node_free(root);
}

static void semantic_ir_rejects_unknown_type(void) {
  Node *root = create_node_map("root");
  Node *record = add_record(root, "Sample");
  Node *field = add_field(record, "Sample", "value", "Missing");
  databind_semantic_field_ir ir = {0};

  TEST_CHECK(!databind_semantic_field_build(root, field, &ir));
  node_free(root);
}

TEST_LIST = {
    {"semantic IR exposes canonical CMeta scalar identity",
     semantic_ir_keeps_projection_data_out},
    {"semantic IR normalizes container semantics",
     semantic_ir_normalizes_container_shape},
    {"semantic IR retains logical presence/nullability",
     semantic_ir_retains_logical_state},
    {"generated-C native lowering selects canonical tstr provider",
     native_lowering_selects_explicit_tstr_provider},
    {"generated-C native lowering fails closed for state overlays",
     native_lowering_keeps_unimplemented_state_fail_closed},
    {"semantic IR fails closed for unresolved types",
     semantic_ir_rejects_unknown_type},
    {NULL, NULL},
};
