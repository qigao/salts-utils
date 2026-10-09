#include "native_source_ir.h"

int main(int argc, char **argv) {
  databind_native_source_field fields[] = {
      {"count", "uint32_t", 1, 0, DATABIND_NATIVE_TRIVIAL},
      {"delta", "int16_t", 0, 1, DATABIND_NATIVE_TRIVIAL},
      {"active", "bool", 1, 1, DATABIND_NATIVE_TRIVIAL}
  };
  databind_native_source_record record = {"Packet", 3u, fields};
  databind_native_source_ir ir = {1u, &record, "NativeFixture", "1"};
  if (argc != 2 && argc != 4) return 1;
  if (argc == 4) {
    ir.schema_name = argv[2];
    ir.schema_version = argv[3];
  }
  return databind_native_source_ir_write_header(&ir, argv[1]) == 0 ? 0 : 1;
}
