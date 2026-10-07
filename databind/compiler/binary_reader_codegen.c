#include "binary_reader_codegen.h"
#include "binary_layout_lowering.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int binary_codegen_identifier_valid(const char *text) {
  size_t i;
  if (text == NULL || text[0] == '\0' ||
      !((text[0] >= 'A' && text[0] <= 'Z') ||
        (text[0] >= 'a' && text[0] <= 'z') || text[0] == '_')) return 0;
  for (i = 1u; text[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') || ch == '_')) return 0;
  }
  return 1;
}

static int binary_codegen_c_string(FILE *file, const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (file == NULL || text == NULL || fputc('"', file) == EOF) return -1;
  for (; *p != '\0'; ++p) {
    if (*p == '\\' || *p == '"') {
      if (fputc('\\', file) == EOF || fputc((int)*p, file) == EOF) return -1;
    } else if (*p < 0x20u || *p >= 0x7fu) {
      if (fprintf(file, "\\x%02X", (unsigned)*p) < 0) return -1;
    } else if (fputc((int)*p, file) == EOF) return -1;
  }
  return fputc('"', file) == EOF ? -1 : 0;
}

static const char *binary_codegen_token_kind(cserde_token_kind kind) {
  switch (kind) {
  case CSERDE_BOOL: return "CSERDE_BOOL";
  case CSERDE_SINT: return "CSERDE_SINT";
  case CSERDE_UINT: return "CSERDE_UINT";
  case CSERDE_FLOAT: return "CSERDE_FLOAT";
  case CSERDE_STRING: return "CSERDE_STRING";
  case CSERDE_BYTES: return "CSERDE_BYTES";
  case CSERDE_MAP_BEGIN: return "CSERDE_MAP_BEGIN";
  case CSERDE_ARRAY_BEGIN: return "CSERDE_ARRAY_BEGIN";
  default: return NULL;
  }
}

static const char *binary_codegen_representation(size_t representation) {
  switch (representation) {
  case DATA_BIND_BINARY_REP_FIXED: return "DATA_BIND_BINARY_REP_FIXED";
  case DATA_BIND_BINARY_REP_VAR_DATA: return "DATA_BIND_BINARY_REP_VAR_DATA";
  case DATA_BIND_BINARY_REP_GROUP: return "DATA_BIND_BINARY_REP_GROUP";
  case DATA_BIND_BINARY_REP_COUNTED: return "DATA_BIND_BINARY_REP_COUNTED";
  case DATA_BIND_BINARY_REP_CURSOR_FIXED: return "DATA_BIND_BINARY_REP_CURSOR_FIXED";
  default: return NULL;
  }
}

static int binary_codegen_symbol(char *out, size_t out_size,
                                  const char *prefix, const char *type_name) {
  int written;
  if (out == NULL || out_size == 0u ||
      !binary_codegen_identifier_valid(prefix) ||
      !binary_codegen_identifier_valid(type_name)) return 0;
  written = snprintf(out, out_size, "%s_binary_%s", prefix, type_name);
  return written > 0 && (size_t)written < out_size;
}

static int binary_codegen_flags(FILE *file, unsigned flags) {
  static const struct { unsigned value; const char *name; } names[] = {
      {DATA_BIND_BINARY_FIELD_OPTIONAL, "DATA_BIND_BINARY_FIELD_OPTIONAL"},
      {DATA_BIND_BINARY_FIELD_NULLABLE, "DATA_BIND_BINARY_FIELD_NULLABLE"},
      {DATA_BIND_BINARY_FIELD_ENUM_BITS, "DATA_BIND_BINARY_FIELD_ENUM_BITS"}};
  int wrote = 0;
  size_t i;
  if (flags == 0u) return fputs("0u", file) == EOF ? -1 : 0;
  for (i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
    if ((flags & names[i].value) == 0u) continue;
    if (wrote && fputs(" | ", file) == EOF) return -1;
    if (fputs(names[i].name, file) == EOF) return -1;
    wrote = 1;
  }
  return wrote ? 0 : -1;
}

int databind_compiler_binary_reader_admit(
    const IdlContract *contract, const databind_binary_format_plan *format_plan,
    const char *type_name) {
  databind_binary_execution_graph *graph = NULL;
  databind_binary_layout_status status = databind_binary_execution_graph_build(
      contract, format_plan, type_name, &graph, NULL);
  databind_binary_execution_graph_destroy(graph);
  return status == DATABIND_BINARY_LAYOUT_OK ? 0 : -1;
}

/* Static emission formats the same already-admitted plan used by runtime
 * schemas; it makes no independent representation or admission decisions. */
static int binary_codegen_emit_plan(
    FILE *file, const DataBindBinaryLayoutPlan *layout, const char *symbol_prefix) {
  char symbol[512];
  size_t i;
  int has_children = 0, has_arrays = 0;
  const char *type_name = layout->type_name;
  if (!binary_codegen_symbol(symbol, sizeof(symbol), symbol_prefix, type_name)) return -1;
  if (fprintf(file,
      "#ifndef DATABIND_GENERATED_%s_READER_INCLUDED\n"
      "#define DATABIND_GENERATED_%s_READER_INCLUDED\n\n"
      "#include <data_bind_binary_reader.h>\n"
      "#include <data_bind_binary_writer.h>\n"
      "#include <data_bind_format_provider.h>\n\n", symbol, symbol) < 0) return -1;

  for (i = 0u; i < layout->field_count; ++i) {
    if (layout->array_plans[i] != NULL) has_arrays = 1;
    if (layout->child_plans[i] != NULL) {
      has_children = 1;
      if (binary_codegen_emit_plan(file, layout->child_plans[i], symbol_prefix) != 0) return -1;
    }
  }
  if (has_children) {
    if (fprintf(file, "static const DataBindBinaryLayoutPlan *const %s_children[] = {\n", symbol) < 0) return -1;
    for (i = 0u; i < layout->field_count; ++i) {
      if (layout->child_plans[i] != NULL) {
        char child_symbol[512];
        if (!binary_codegen_symbol(child_symbol, sizeof(child_symbol), symbol_prefix,
                                    layout->child_plans[i]->type_name) ||
            fprintf(file, "  &%s_plan,\n", child_symbol) < 0) return -1;
      } else if (fputs("  NULL,\n", file) == EOF) return -1;
    }
    if (fputs("};\n\n", file) == EOF) return -1;
  }
  if (has_arrays) {
    for (i = 0u; i < layout->field_count; ++i) {
      const DataBindBinaryArrayPlan *array = layout->array_plans[i];
      const char *token;
      if (array == NULL) continue;
      token = binary_codegen_token_kind(array->element_token_kind);
      if (token == NULL || fprintf(file,
          "static const DataBindBinaryArrayPlan %s_array_%zu = {\n"
          "  sizeof(DataBindBinaryArrayPlan), %zuu, %zuu, %s, %uu, ",
          symbol, i, array->count, array->element_extent, token, array->element_scalar_bits) < 0 ||
          binary_codegen_flags(file, array->element_flags) != 0 ||
          fputs("\n};\n", file) == EOF) return -1;
    }
    if (fprintf(file, "static const DataBindBinaryArrayPlan *const %s_arrays[] = {\n", symbol) < 0) return -1;
    for (i = 0u; i < layout->field_count; ++i) {
      if (layout->array_plans[i] != NULL) {
        if (fprintf(file, "  &%s_array_%zu,\n", symbol, i) < 0) return -1;
      } else if (fputs("  NULL,\n", file) == EOF) return -1;
    }
    if (fputs("};\n\n", file) == EOF) return -1;
  }
  if (layout->field_count != 0u) {
    if (fprintf(file, "static const DataBindBinaryFieldPlan %s_fields[] = {\n", symbol) < 0) return -1;
    for (i = 0u; i < layout->field_count; ++i) {
      const DataBindBinaryFieldPlan *field = &layout->fields[i];
      const char *token = binary_codegen_token_kind(field->token_kind);
      const char *representation = binary_codegen_representation(field->representation);
      if (token == NULL || representation == NULL ||
          fputs("  {sizeof(DataBindBinaryFieldPlan), ", file) == EOF ||
          binary_codegen_c_string(file, field->field_name) != 0 ||
          fprintf(file, ", %s, %uu, %zuu, %zuu, %uu, %uu, ", token, field->scalar_bits,
                   field->wire_offset, field->wire_extent, field->optional_bit, field->nullable_bit) < 0 ||
          binary_codegen_flags(file, field->flags) != 0 ||
          fprintf(file, ", %s, %zuu},\n", representation, field->tail_prefix_bytes) < 0) return -1;
    }
    if (fputs("};\n\n", file) == EOF) return -1;
  }
  if (fprintf(file,
      "static const DataBindBinaryLayoutPlan %s_plan = {\n"
      "  sizeof(DataBindBinaryLayoutPlan), DATA_BIND_BINARY_LAYOUT_PLAN_ABI_VERSION,\n"
      "  ", symbol) < 0 ||
      binary_codegen_c_string(file, type_name) != 0 ||
      fprintf(file, ",\n  %d, %zuu, %zuu, %zuu, %zuu, %zuu,\n  ",
               layout->wire_big_endian ? 1 : 0, layout->fixed_block_size,
               layout->presence_offset, layout->presence_size,
               layout->null_offset, layout->null_size) < 0) return -1;
  if (layout->field_count != 0u) {
    if (fprintf(file, "%s_fields, %zuu, ", symbol, layout->field_count) < 0) return -1;
  } else if (fputs("NULL, 0u, ", file) == EOF) return -1;
  if (has_children) {
    if (fprintf(file, "%s_children, ", symbol) < 0) return -1;
  } else if (fputs("NULL, ", file) == EOF) return -1;
  if (has_arrays) {
    if (fprintf(file, "%s_arrays\n};\n\n", symbol) < 0) return -1;
  } else if (fputs("NULL\n};\n\n", file) == EOF) return -1;

  if (fprintf(
          file,
          "static DataBindStatus %s_open(\n"
          "    const char *data, size_t len, size_t max_depth,\n"
          "    cserde_reader **out_reader, void **out_owner,\n"
          "    DataBindError *error) {\n"
          "  return data_bind_binary_reader_open(\n"
          "      &%s_plan, data, len, max_depth,\n"
          "      out_reader, out_owner, error);\n"
          "}\n"
          "static void %s_close(cserde_reader *reader, void *owner) {\n"
          "  data_bind_binary_reader_close(reader, owner);\n"
          "}\n"
          "static DataBindStatus %s_writer_open(\n"
          "    DataBindWriteFn write, void *write_user, size_t max_depth,\n"
          "    cserde_writer **out_writer, void **out_owner,\n"
          "    DataBindError *error) {\n"
          "  return data_bind_binary_writer_open(\n"
          "      &%s_plan, write, write_user, max_depth,\n"
          "      out_writer, out_owner, error);\n"
          "}\n"
          "static DataBindStatus %s_writer_close(\n"
          "    cserde_writer *writer, void *owner, DataBindError *error) {\n"
          "  return data_bind_binary_writer_close(writer, owner, error);\n"
          "}\n"
          "static const DataBindFormatProvider %s_provider =\n"
          "    DATA_BIND_FORMAT_PROVIDER_WITH_SELECTION_AND_WRITER_INIT(\n"
          "        DATA_BIND_FORMAT_BINARY, %s_open, %s_close, NULL,\n"
          "        %s_writer_open, %s_writer_close);\n\n"
          "static inline const DataBindBinaryLayoutPlan *\n"
          "%s_databind_binary_layout_plan(void) {\n"
          "  return &%s_plan;\n"
          "}\n"
          "static inline const DataBindFormatProvider *\n"
          "%s_databind_binary_provider(void) {\n"
          "  return &%s_provider;\n"
          "}\n\n"
          "#endif /* DATABIND_GENERATED_%s_READER_INCLUDED */\n",
          symbol, symbol,
          symbol,
          symbol, symbol,
          symbol,
          symbol,
          symbol, symbol,
          symbol, symbol,
          symbol, symbol,
          symbol, symbol,
          symbol) < 0)
    goto cleanup;

  return 0;
cleanup:
  return -1;
}

int databind_compiler_binary_reader_emit(
    FILE *file, const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *type_name, const char *symbol_prefix) {
  databind_binary_execution_graph *graph = NULL;
  databind_binary_layout_status status;
  int result = -1;
  if (file == NULL || !binary_codegen_identifier_valid(type_name) ||
      !binary_codegen_identifier_valid(symbol_prefix)) return -1;
  status = databind_binary_execution_graph_build(
      contract, format_plan, type_name, &graph, NULL);
  if (status == DATABIND_BINARY_LAYOUT_OK)
    result = binary_codegen_emit_plan(
        file, databind_binary_execution_graph_root(graph), symbol_prefix);
  databind_binary_execution_graph_destroy(graph);
  return result;
}
