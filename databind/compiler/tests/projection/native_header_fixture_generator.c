#include "native_source_ir.h"

int main(int argc, char **argv) {
  databind_native_source_field fields[] = {
      {"count", "uint32_t", 1, 0, DATABIND_NATIVE_TRIVIAL, NULL, 0, NULL, NULL, NULL, NULL, NULL},
      {"delta", "int16_t", 0, 1, DATABIND_NATIVE_TRIVIAL, NULL, 0, NULL, NULL, NULL, NULL, NULL},
      {"active", "bool", 1, 1, DATABIND_NATIVE_TRIVIAL, NULL, 0, NULL, NULL, NULL, NULL, NULL}
  };
  databind_native_source_record record = {"Packet", 3u, fields};
  databind_native_source_ir ir = {1u, &record, "NativeFixture", "1"};
  if (argc == 3 && argv[2][0] == 'v') {
    databind_native_source_field value_fields[] = {
        {"count", "uint32_t", 0, 0, DATABIND_NATIVE_TRIVIAL,
         NULL, 0, NULL, NULL, NULL, NULL, NULL},
        {"delta", "int16_t", 0, 0, DATABIND_NATIVE_TRIVIAL,
         NULL, 0, NULL, NULL, NULL, NULL, NULL},
        {"active", "bool", 0, 0, DATABIND_NATIVE_TRIVIAL,
         NULL, 0, NULL, NULL, NULL, NULL, NULL}
    };
    databind_native_source_record value = {"ValuePacket", 3u, value_fields};
    databind_native_source_ir value_ir = {1u, &value, "NativeValue", "1"};
    return databind_native_source_ir_write_header(&value_ir, argv[1]) == 0 ? 0 : 1;
  }
  if (argc == 3 && argv[2][0] == 'r') {
    databind_native_source_field item_fields[] = {
        {"label", "databind_native_text", 0, 0, DATABIND_NATIVE_OWNED_TEXT,
         NULL, 0, NULL, NULL, NULL, NULL, NULL},
        {"payload", "databind_native_bytes", 0, 0, DATABIND_NATIVE_OWNED_BYTES,
         NULL, 0, NULL, NULL, NULL, NULL, NULL}
    };
    databind_native_source_field holder_fields[] = {
        {"items", "vec_t", 0, 0, DATABIND_NATIVE_OWNED_SEQUENCE,
         "RecordItem", 0, NULL, NULL, NULL, NULL, NULL},
        {"byname", "map_t", 0, 0, DATABIND_NATIVE_OWNED_MAP,
         NULL, 0, NULL, "string", "RecordItem",
         "databind_native_text_cmeta_type", NULL}
    };
    databind_native_source_record records[] = {
        {"RecordItem", 2u, item_fields},
        {"RecordHolder", 2u, holder_fields}
    };
    databind_native_source_ir record_ir = {
        2u, records, "RecordElementFixture", "1"
    };
    return databind_native_source_ir_write_header(&record_ir, argv[1]) == 0 ? 0 : 1;
  }
  if (argc == 3 && argv[2][0] == 't') {
    databind_native_source_field owned_containers[] = {
        {"labels", "vec_t", 0, 0, DATABIND_NATIVE_OWNED_SEQUENCE,
         "string", 0, "databind_native_text_cmeta_type", NULL, NULL, NULL, NULL},
        {"buffers", "vec_t", 0, 0, DATABIND_NATIVE_OWNED_SEQUENCE,
         "bytes", 0, "databind_native_bytes_cmeta_type", NULL, NULL, NULL, NULL},
        {"attrs", "map_t", 0, 0, DATABIND_NATIVE_OWNED_MAP,
         NULL, 0, NULL, "string", "bytes",
         "databind_native_text_cmeta_type", "databind_native_bytes_cmeta_type"},
        {"tags", "set_t", 0, 0, DATABIND_NATIVE_OWNED_SET,
         "string", 0, "databind_native_text_cmeta_type", NULL, NULL, NULL, NULL}
    };
    databind_native_source_record record = {
        "OwnedContainers", 4u, owned_containers
    };
    databind_native_source_ir container_ir = {
        1u, &record, "OwnedContainerFixture", "1"
    };
    return databind_native_source_ir_write_header(&container_ir, argv[1]) == 0 ? 0 : 1;
  }
  if (argc == 3 && argv[2][0] == 'a') {
    databind_native_source_field fields[] = {
        {"lookup", "map_t", 0, 0, DATABIND_NATIVE_OWNED_MAP,
         NULL, 0, NULL, "uint32", "int32", "cmeta_type_uint32", "cmeta_type_int32"},
        {"unique", "set_t", 0, 0, DATABIND_NATIVE_OWNED_SET,
         "uint32", 1, "cmeta_type_uint32", NULL, NULL, NULL, NULL}
    };
    databind_native_source_record record = {"AssocPacket", 2u, fields};
    databind_native_source_ir ir = {1u, &record, "AssocFixture", "1"};
    return databind_native_source_ir_write_header(&ir, argv[1]) == 0 ? 0 : 1;
  }
  if (argc == 3 && argv[2][0] == 's') {
    databind_native_source_field scalar_sequence[] = {
        {"numbers", "vec_t", 0, 0, DATABIND_NATIVE_OWNED_SEQUENCE,
         "uint32", 1, "cmeta_type_uint32"}
    };
    databind_native_source_record item = {"SequencePacket", 1u, scalar_sequence};
    databind_native_source_ir source_ir = {1u, &item, "SequenceFixture", "1"};
    return databind_native_source_ir_write_header(&source_ir, argv[1]) == 0 ? 0 : 1;
  }
  if (argc == 3 && argv[2][0] == 'n') {
    databind_native_source_field leaf_fields[] = {
        {"text", "databind_native_text", 0, 0, DATABIND_NATIVE_OWNED_TEXT, NULL, 0, NULL, NULL, NULL, NULL, NULL},
        {"data", "databind_native_bytes", 0, 0, DATABIND_NATIVE_OWNED_BYTES, NULL, 0, NULL, NULL, NULL, NULL, NULL}
    };
    databind_native_source_field parent_fields[] = {
        {"leaf", "NativeLeaf", 0, 0, DATABIND_NATIVE_OWNED_RECORD, NULL, 0, NULL, NULL, NULL, NULL, NULL},
        {"label", "databind_native_text", 1, 1, DATABIND_NATIVE_OWNED_TEXT, NULL, 0, NULL, NULL, NULL, NULL, NULL}
    };
    databind_native_source_field root_fields[] = {
        {"parent", "NativeParent", 0, 0, DATABIND_NATIVE_OWNED_RECORD, NULL, 0, NULL, NULL, NULL, NULL, NULL},
        {"tail", "databind_native_bytes", 0, 0, DATABIND_NATIVE_OWNED_BYTES, NULL, 0, NULL, NULL, NULL, NULL, NULL}
    };
    databind_native_source_record nested[] = {
        {"NativeLeaf", 2u, leaf_fields},
        {"NativeParent", 2u, parent_fields},
        {"NativeRoot", 2u, root_fields}
    };
    databind_native_source_ir nested_ir = {3u, nested, "NestedFixture", "1"};
    return databind_native_source_ir_write_header(&nested_ir, argv[1]) == 0 ? 0 : 1;
  }
  if (argc == 3 && argv[2][0] == 'o') {
    databind_native_source_field owned[] = {
        {"title", "databind_native_text", 1, 1, DATABIND_NATIVE_OWNED_TEXT, NULL, 0, NULL, NULL, NULL, NULL, NULL},
        {"payload", "databind_native_bytes", 0, 0, DATABIND_NATIVE_OWNED_BYTES, NULL, 0, NULL, NULL, NULL, NULL, NULL},
        {"count", "uint32_t", 0, 0, DATABIND_NATIVE_TRIVIAL, NULL, 0, NULL, NULL, NULL, NULL, NULL}
    };
    databind_native_source_record item = {"OwnedPacket", 3u, owned};
    databind_native_source_ir owned_ir = {1u, &item, "OwnedFixture", "1"};
    return databind_native_source_ir_write_header(&owned_ir, argv[1]) == 0 ? 0 : 1;
  }
  if (argc != 2 && argc != 4) return 1;
  if (argc == 4) {
    ir.schema_name = argv[2];
    ir.schema_version = argv[3];
  }
  return databind_native_source_ir_write_header(&ir, argv[1]) == 0 ? 0 : 1;
}
