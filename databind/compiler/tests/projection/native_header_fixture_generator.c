#include "native_source_ir.h"

int main(int argc, char **argv) {
  databind_native_source_field fields[] = {
      {"count", "uint32_t", 1, 0},
      {"delta", "int16_t", 0, 1},
      {"active", "bool", 1, 1}
  };
  databind_native_source_record record = {"Packet", 3u, fields};
  databind_native_source_ir ir = {1u, &record};
  if (argc != 2) return 1;
  return databind_native_source_ir_write_header(&ir, argv[1]) == 0 ? 0 : 1;
}
