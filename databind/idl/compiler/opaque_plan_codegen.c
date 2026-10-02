#include "opaque_plan_codegen.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int opaque_identifier_valid(const char *text) {
  size_t i;
  if (text == NULL || text[0] == '\0' ||
      !((text[0] >= 'A' && text[0] <= 'Z') ||
        (text[0] >= 'a' && text[0] <= 'z') || text[0] == '_'))
    return 0;
  for (i = 1u; text[i] != '\0'; ++i) {
    const unsigned char ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') || ch == '_'))
      return 0;
  }
  return 1;
}

int databind_compiler_opaque_plan_admit(const char *type_name) {
  return type_name != NULL && strcmp(type_name, "bytes") == 0 ? 0 : -1;
}

int databind_compiler_opaque_plan_emit(
    FILE *file, const char *symbol_prefix, size_t max_bytes) {
  if (file == NULL || !opaque_identifier_valid(symbol_prefix) ||
      max_bytes == 0u || max_bytes > UINT32_MAX)
    return -1;

  return fprintf(
             file,
             "#ifndef DATABIND_GENERATED_%s_OPAQUE_PLAN_INCLUDED\n"
             "#define DATABIND_GENERATED_%s_OPAQUE_PLAN_INCLUDED\n\n"
             "#include <data_bind_opaque_plan.h>\n\n"
             "static const DataBindOpaquePlan %s_opaque_plan = {\n"
             "  sizeof(DataBindOpaquePlan), DATA_BIND_OPAQUE_PLAN_ABI_VERSION,\n"
             "  \"bytes\", DATA_BIND_OPAQUE_STATE_VALUE, %zuu\n"
             "};\n"
             "static inline const DataBindOpaquePlan *\n"
             "%s_databind_opaque_plan(void) {\n"
             "  return &%s_opaque_plan;\n"
             "}\n\n"
             "#endif /* DATABIND_GENERATED_%s_OPAQUE_PLAN_INCLUDED */\n",
             symbol_prefix, symbol_prefix,
             symbol_prefix, max_bytes,
             symbol_prefix, symbol_prefix,
             symbol_prefix) < 0
             ? -1
             : 0;
}
