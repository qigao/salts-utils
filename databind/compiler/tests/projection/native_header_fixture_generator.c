#include "native_source_ir.h"

int main(int argc, char **argv) {
  databind_native_source_field fields[] = {
      {"count", "uint32_t", 1, 0, DATABIND_NATIVE_TRIVIAL, NULL, 0, NULL},
      {"delta", "int16_t", 0, 1, DATABIND_NATIVE_TRIVIAL, NULL, 0, NULL},
      {"active", "bool", 1, 1, DATABIND_NATIVE_TRIVIAL, NULL, 0, NULL}
  };
  databind_native_source_record record = {"Packet", 3u, fields};
  databind_native_source_ir ir = {1u, &record, "NativeFixture", "1"};
  if (argc == 3 && argv[2][0] == 'n') {
    databind_native_source_field leaf_fields[] = {
        {"text", "databind_native_text", 0, 0, DATABIND_NATIVE_OWNED_TEXT, NULL, 0, NULL},
        {"data", "databind_native_bytes", 0, 0, DATABIND_NATIVE_OWNED_BYTES, NULL, 0, NULL}
    };
    databind_native_source_field parent_fields[] = {
        {"leaf", "NativeLeaf", 0, 0, DATABIND_NATIVE_OWNED_RECORD, NULL, 0, NULL},
        {"label", "databind_native_text", 1, 1, DATABIND_NATIVE_OWNED_TEXT, NULL, 0, NULL}
    };
    databind_native_source_field root_fields[] = {
        {"parent", "NativeParent", 0, 0, DATABIND_NATIVE_OWNED_RECORD, NULL, 0, NULL},
        {"tail", "databind_native_bytes", 0, 0, DATABIND_NATIVE_OWNED_BYTES, NULL, 0, NULL}
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
        {"title", "databind_native_text", 1, 1, DATABIND_NATIVE_OWNED_TEXT, NULL, 0, NULL},
        {"payload", "databind_native_bytes", 0, 0, DATABIND_NATIVE_OWNED_BYTES, NULL, 0, NULL},
        {"count", "uint32_t", 0, 0, DATABIND_NATIVE_TRIVIAL, NULL, 0, NULL}
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
