#include "binary_reader_codegen.h"

#include "binary_layout_ir.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int binary_codegen_identifier_valid(const char *text) {
  size_t i;
  if (text == NULL || text[0] == '\0' ||
      !((text[0] >= 'A' && text[0] <= 'Z') ||
        (text[0] >= 'a' && text[0] <= 'z') || text[0] == '_'))
    return 0;
  for (i = 1u; text[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') || ch == '_'))
      return 0;
  }
  return 1;
}

static int binary_codegen_c_string(FILE *file, const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (file == NULL || text == NULL || fputc('"', file) == EOF) return -1;
  for (; *p != '\0'; ++p) {
    if (*p == '\\' || *p == '"') {
      if (fputc('\\', file) == EOF || fputc((int)*p, file) == EOF)
        return -1;
    } else if (*p < 0x20u || *p >= 0x7fu) {
      if (fprintf(file, "\\x%02X", (unsigned)*p) < 0) return -1;
    } else if (fputc((int)*p, file) == EOF) {
      return -1;
    }
  }
  return fputc('"', file) == EOF ? -1 : 0;
}

static const char *binary_codegen_token_kind(
    databind_binary_scalar_kind kind) {
  switch (kind) {
  case DATABIND_BINARY_SCALAR_BOOL:
    return "CSERDE_BOOL";
  case DATABIND_BINARY_SCALAR_SINT:
  case DATABIND_BINARY_SCALAR_ENUM_SINT:
    return "CSERDE_SINT";
  case DATABIND_BINARY_SCALAR_UINT:
  case DATABIND_BINARY_SCALAR_ENUM_UINT:
    return "CSERDE_UINT";
  case DATABIND_BINARY_SCALAR_FLOAT:
    return "CSERDE_FLOAT";
  case DATABIND_BINARY_SCALAR_STRING:
    return "CSERDE_STRING";
  case DATABIND_BINARY_SCALAR_BYTES:
    return "CSERDE_BYTES";
  case DATABIND_BINARY_SCALAR_NONE:
  default:
    return NULL;
  }
}

static int binary_codegen_layout_admitted(
    const databind_binary_type_layout *layout) {
  size_t i;
  int has_var_data = 0;
  if (layout == NULL || layout->type_id == NULL)
    return 0;

  for (i = 0u; i < layout->field_count; ++i) {
    const databind_binary_field_layout *field = &layout->fields[i];
    const char *token = binary_codegen_token_kind(field->scalar_kind);
    if (field->field_id == NULL || token == NULL)
      return 0;
    if (field->kind == DATABIND_BINARY_FIELD_FIXED) {
      if (field->scalar_bits == 0u ||
          field->wire_extent != (size_t)(field->scalar_bits / 8u))
        return 0;
    } else if (field->kind == DATABIND_BINARY_FIELD_VAR_DATA) {
      has_var_data = 1;
      if ((field->scalar_kind != DATABIND_BINARY_SCALAR_STRING &&
           field->scalar_kind != DATABIND_BINARY_SCALAR_BYTES) ||
          field->scalar_bits != 0u ||
          field->tail_prefix_bytes != sizeof(uint32_t))
        return 0;
    } else {
      return 0;
    }
  }
  return layout->fixed_block_size != 0u || has_var_data;
}

int databind_compiler_binary_reader_admit(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *type_name) {
  databind_binary_type_layout layout = {0};
  databind_binary_layout_diagnostic diagnostic = {0};
  databind_binary_layout_status status;
  int admitted;

  if (contract == NULL || format_plan == NULL ||
      type_name == NULL || type_name[0] == '\0')
    return -1;
  status = databind_binary_layout_build(
      contract, format_plan, type_name, &layout, &diagnostic);
  if (status != DATABIND_BINARY_LAYOUT_OK) return -1;
  admitted = binary_codegen_layout_admitted(&layout);
  databind_binary_layout_destroy(&layout);
  return admitted ? 0 : -1;
}

static unsigned binary_codegen_flags(
    const databind_binary_field_layout *field) {
  unsigned result = 0u;
  if ((field->flags & DATABIND_BINARY_FIELD_OPTIONAL) != 0u)
    result |= 1u;
  if ((field->flags & DATABIND_BINARY_FIELD_NULLABLE) != 0u)
    result |= 2u;
  return result;
}

static const char *binary_codegen_representation(
    const databind_binary_field_layout *field) {
  if (field == NULL) return NULL;
  if (field->kind == DATABIND_BINARY_FIELD_FIXED)
    return "DATA_BIND_BINARY_REP_FIXED";
  if (field->kind == DATABIND_BINARY_FIELD_VAR_DATA)
    return "DATA_BIND_BINARY_REP_VAR_DATA";
  return NULL;
}

static int binary_codegen_symbol(
    char *out, size_t out_size,
    const char *prefix, const char *type_name) {
  int written;
  if (out == NULL || out_size == 0u ||
      !binary_codegen_identifier_valid(prefix) ||
      !binary_codegen_identifier_valid(type_name))
    return 0;
  written = snprintf(
      out, out_size, "%s_binary_%s", prefix, type_name);
  return written > 0 && (size_t)written < out_size;
}

int databind_compiler_binary_reader_emit(
    FILE *file,
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *type_name,
    const char *symbol_prefix) {
  databind_binary_type_layout layout = {0};
  databind_binary_layout_diagnostic diagnostic = {0};
  databind_binary_layout_status status;
  char symbol[512];
  size_t i;
  int result = -1;

  if (file == NULL || contract == NULL || format_plan == NULL ||
      type_name == NULL || symbol_prefix == NULL ||
      !binary_codegen_symbol(
          symbol, sizeof(symbol), symbol_prefix, type_name))
    return -1;

  status = databind_binary_layout_build(
      contract, format_plan, type_name, &layout, &diagnostic);
  if (status != DATABIND_BINARY_LAYOUT_OK ||
      !binary_codegen_layout_admitted(&layout))
    goto cleanup;

  if (fprintf(
          file,
          "#ifndef DATABIND_GENERATED_%s_READER_INCLUDED\n"
          "#define DATABIND_GENERATED_%s_READER_INCLUDED\n\n"
          "#include <data_bind_binary_reader.h>\n"
          "#include <data_bind_binary_writer.h>\n"
          "#include <data_bind_format_provider.h>\n\n",
          symbol, symbol) < 0)
    goto cleanup;

  if (layout.field_count != 0u) {
    if (fprintf(
            file,
            "static const DataBindBinaryFieldPlan "
            "%s_fields[] = {\n",
            symbol) < 0)
      goto cleanup;

    for (i = 0u; i < layout.field_count; ++i) {
      const databind_binary_field_layout *field = &layout.fields[i];
      const char *token = binary_codegen_token_kind(field->scalar_kind);
      const char *representation = binary_codegen_representation(field);
      unsigned flags = binary_codegen_flags(field);

      if (token == NULL || representation == NULL ||
          fputs("  {sizeof(DataBindBinaryFieldPlan), ", file) == EOF ||
          binary_codegen_c_string(file, field->field_id) != 0 ||
          fprintf(
              file,
              ", %s, %uu, %zuu, %zuu, %uu, %uu, ",
              token, field->scalar_bits,
              field->wire_offset, field->wire_extent,
              field->optional_bit, field->nullable_bit) < 0)
        goto cleanup;

      if (flags == 0u) {
        if (fputs("0u, ", file) == EOF) goto cleanup;
      } else {
        int wrote = 0;
        if ((flags & 1u) != 0u) {
          if (fputs(
                  "DATA_BIND_BINARY_FIELD_OPTIONAL",
                  file) == EOF)
            goto cleanup;
          wrote = 1;
        }
        if ((flags & 2u) != 0u) {
          if (wrote && fputs(" | ", file) == EOF) goto cleanup;
          if (fputs(
                  "DATA_BIND_BINARY_FIELD_NULLABLE",
                  file) == EOF)
            goto cleanup;
        }
        if (fputs(", ", file) == EOF) goto cleanup;
      }
      if (fprintf(
              file, "%s, %zuu},\n",
              representation,
              field->kind == DATABIND_BINARY_FIELD_VAR_DATA
                  ? field->tail_prefix_bytes
                  : 0u) < 0)
        goto cleanup;
    }
    if (fputs("};\n\n", file) == EOF) goto cleanup;
  }

  if (fprintf(
          file,
          "static const DataBindBinaryLayoutPlan %s_plan = {\n"
          "  sizeof(DataBindBinaryLayoutPlan), "
          "DATA_BIND_BINARY_LAYOUT_PLAN_ABI_VERSION,\n"
          "  ",
          symbol) < 0 ||
      binary_codegen_c_string(file, type_name) != 0 ||
      fprintf(
          file,
          ",\n  %d, %zuu, %zuu, %zuu, %zuu, %zuu,\n  ",
          layout.wire_big_endian ? 1 : 0,
          layout.fixed_block_size,
          layout.presence_offset, layout.presence_size,
          layout.null_offset, layout.null_size) < 0)
    goto cleanup;

  if (layout.field_count != 0u) {
    if (fprintf(
            file, "%s_fields, %zuu\n};\n\n",
            symbol, layout.field_count) < 0)
      goto cleanup;
  } else {
    if (fputs("NULL, 0u\n};\n\n", file) == EOF)
      goto cleanup;
  }

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

  result = 0;

cleanup:
  databind_binary_layout_destroy(&layout);
  return result;
}
