#include "data_bind_cmeta_adapter_plan.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum adapter_carrier {
  ADAPTER_CARRIER_VOID = 0,
  ADAPTER_CARRIER_U32 = 1,
  ADAPTER_CARRIER_U64 = 2
} adapter_carrier;

static bool producer_identifier_valid(const char *text) {
  size_t i;
  unsigned char ch;
  if (text == NULL || text[0] == '\0') return false;
  ch = (unsigned char)text[0];
  if (!((ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') || ch == '_'))
    return false;
  for (i = 1u; text[i] != '\0'; ++i) {
    ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') || ch == '_'))
      return false;
  }
  return true;
}

static bool producer_write(
    DataBindCMetaAdapterPlanWriteFn write,
    void *context,
    const char *text,
    size_t size) {
  return size == 0u || write(context, text, size);
}

static bool producer_literal(
    DataBindCMetaAdapterPlanWriteFn write,
    void *context,
    const char *text) {
  return producer_write(write, context, text, strlen(text));
}

static bool producer_size(
    DataBindCMetaAdapterPlanWriteFn write,
    void *context,
    size_t value) {
  char buffer[32];
  int written = snprintf(buffer, sizeof(buffer), "%zu", value);
  return written > 0 && (size_t)written < sizeof(buffer) &&
         producer_write(write, context, buffer, (size_t)written);
}

static bool producer_c_string(
    DataBindCMetaAdapterPlanWriteFn write,
    void *context,
    const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  char escaped[5];

  if (!producer_literal(write, context, "\"")) return false;
  for (; *p != '\0'; ++p) {
    switch (*p) {
    case '\\':
      if (!producer_literal(write, context, "\\\\")) return false;
      break;
    case '"':
      if (!producer_literal(write, context, "\\\"")) return false;
      break;
    case '\n':
      if (!producer_literal(write, context, "\\n")) return false;
      break;
    case '\r':
      if (!producer_literal(write, context, "\\r")) return false;
      break;
    case '\t':
      if (!producer_literal(write, context, "\\t")) return false;
      break;
    default:
      if (*p >= 0x20u && *p < 0x7fu) {
        char ch = (char)*p;
        if (!producer_write(write, context, &ch, 1u)) return false;
      } else {
        int written = snprintf(escaped, sizeof(escaped), "\\%03o", (unsigned)*p);
        if (written != 4 ||
            !producer_write(write, context, escaped, (size_t)written))
          return false;
      }
      break;
    }
  }
  return producer_literal(write, context, "\"");
}

static bool producer_symbol(
    DataBindCMetaAdapterPlanWriteFn write,
    void *context,
    const char *prefix,
    const char *suffix) {
  return producer_literal(write, context, prefix) &&
         producer_literal(write, context, suffix);
}

static bool producer_carrier(
    const cmeta_type_desc *type,
    bool allow_void,
    adapter_carrier *out) {
  if (out == NULL || type == NULL) return false;
  if (allow_void && cmeta_type_equal(type, &cmeta_type_void)) {
    *out = ADAPTER_CARRIER_VOID;
    return true;
  }
  if (cmeta_type_equal(type, &cmeta_type_uint32)) {
    *out = ADAPTER_CARRIER_U32;
    return true;
  }
  if (cmeta_type_equal(type, &cmeta_type_uint64)) {
    *out = ADAPTER_CARRIER_U64;
    return true;
  }
  return false;
}

static const char *producer_carrier_suffix(adapter_carrier carrier) {
  switch (carrier) {
  case ADAPTER_CARRIER_VOID:
    return "_adapter_carrier_void";
  case ADAPTER_CARRIER_U32:
    return "_adapter_carrier_u32";
  case ADAPTER_CARRIER_U64:
    return "_adapter_carrier_u64";
  }
  return NULL;
}

static DataBindCMetaAdapterPlanStatus producer_validate(
    const cmeta_function_desc *const *functions,
    size_t count,
    size_t *out_error_index) {
  size_t i;
  size_t j;

  for (i = 0u; i < count; ++i) {
    const cmeta_function_desc *function = functions[i];
    adapter_carrier carrier;

    if (!cmeta_function_desc_valid(function)) {
      if (out_error_index != NULL) *out_error_index = i;
      return DATA_BIND_CMETA_ADAPTER_PLAN_INVALID_DESCRIPTOR;
    }

    if (!producer_carrier(function->return_type, true, &carrier)) {
      if (out_error_index != NULL) *out_error_index = i;
      return DATA_BIND_CMETA_ADAPTER_PLAN_UNSUPPORTED_TYPE;
    }

    for (j = 0u; j < function->param_count; ++j) {
      if (!producer_carrier(function->params[j].type, false, &carrier)) {
        if (out_error_index != NULL) *out_error_index = i;
        return DATA_BIND_CMETA_ADAPTER_PLAN_UNSUPPORTED_TYPE;
      }
    }
  }

  return DATA_BIND_CMETA_ADAPTER_PLAN_OK;
}

static bool producer_emit_header_begin(
    DataBindCMetaAdapterPlanWriteFn write,
    void *context,
    const char *prefix) {
  return
      producer_literal(write, context,
          "/* generated static adapter plan; do not edit */\n#ifndef ") &&
      producer_symbol(write, context, prefix, "_adapter_plan_generated_h") &&
      producer_literal(write, context, "\n#define ") &&
      producer_symbol(write, context, prefix, "_adapter_plan_generated_h") &&
      producer_literal(write, context,
          "\n\n#include <stddef.h>\n#include <stdint.h>\n\n"
          "typedef enum ") &&
      producer_symbol(write, context, prefix, "_adapter_carrier") &&
      producer_literal(write, context, " {\n  ") &&
      producer_symbol(write, context, prefix, "_adapter_carrier_void") &&
      producer_literal(write, context, " = 0,\n  ") &&
      producer_symbol(write, context, prefix, "_adapter_carrier_u32") &&
      producer_literal(write, context, " = 1,\n  ") &&
      producer_symbol(write, context, prefix, "_adapter_carrier_u64") &&
      producer_literal(write, context, "\n} ") &&
      producer_symbol(write, context, prefix, "_adapter_carrier") &&
      producer_literal(write, context, ";\n\ntypedef struct ") &&
      producer_symbol(write, context, prefix, "_adapter_param_plan") &&
      producer_literal(write, context,
          " {\n"
          "  const char *name;\n"
          "  uint32_t flags;\n"
          "  ") &&
      producer_symbol(write, context, prefix, "_adapter_carrier") &&
      producer_literal(write, context, " carrier;\n} ") &&
      producer_symbol(write, context, prefix, "_adapter_param_plan") &&
      producer_literal(write, context, ";\n\ntypedef struct ") &&
      producer_symbol(write, context, prefix, "_adapter_function_plan") &&
      producer_literal(write, context,
          " {\n"
          "  size_t source_ordinal;\n"
          "  const char *function_name;\n"
          "  const ") &&
      producer_symbol(write, context, prefix, "_adapter_param_plan") &&
      producer_literal(write, context,
          " *params;\n"
          "  size_t param_count;\n"
          "  ") &&
      producer_symbol(write, context, prefix, "_adapter_carrier") &&
      producer_literal(write, context,
          " return_carrier;\n"
          "  uint32_t effects;\n"
          "  uint32_t properties;\n"
          "} ") &&
      producer_symbol(write, context, prefix, "_adapter_function_plan") &&
      producer_literal(write, context, ";\n\n");
}

static bool producer_emit_param_array(
    DataBindCMetaAdapterPlanWriteFn write,
    void *context,
    const char *prefix,
    const cmeta_function_desc *function,
    size_t function_index) {
  size_t i;

  if (function->param_count == 0u) return true;

  if (!producer_literal(write, context, "static const ") ||
      !producer_symbol(write, context, prefix, "_adapter_param_plan") ||
      !producer_literal(write, context, " ") ||
      !producer_symbol(write, context, prefix, "_adapter_params_") ||
      !producer_size(write, context, function_index) ||
      !producer_literal(write, context, "[] = {\n"))
    return false;

  for (i = 0u; i < function->param_count; ++i) {
    adapter_carrier carrier;
    const char *suffix;
    if (!producer_carrier(function->params[i].type, false, &carrier))
      return false;
    suffix = producer_carrier_suffix(carrier);
    if (suffix == NULL ||
        !producer_literal(write, context, "  {") ||
        !producer_c_string(write, context, function->params[i].name) ||
        !producer_literal(write, context, ", ") ||
        !producer_size(write, context, (size_t)function->params[i].flags) ||
        !producer_literal(write, context, "u, ") ||
        !producer_symbol(write, context, prefix, suffix) ||
        !producer_literal(write, context, "},\n"))
      return false;
  }
  return producer_literal(write, context, "};\n\n");
}

static bool producer_emit_function_rows(
    DataBindCMetaAdapterPlanWriteFn write,
    void *context,
    const char *prefix,
    const cmeta_function_desc *const *functions,
    size_t count) {
  size_t i;

  if (!producer_literal(write, context, "static const ") ||
      !producer_symbol(write, context, prefix, "_adapter_function_plan") ||
      !producer_literal(write, context, " ") ||
      !producer_symbol(write, context, prefix, "_adapter_functions") ||
      !producer_literal(write, context, "[] = {\n"))
    return false;

  for (i = 0u; i < count; ++i) {
    const cmeta_function_desc *function = functions[i];
    adapter_carrier result;
    const char *suffix;

    if (!producer_carrier(function->return_type, true, &result))
      return false;
    suffix = producer_carrier_suffix(result);
    if (suffix == NULL ||
        !producer_literal(write, context, "  {") ||
        !producer_size(write, context, i) ||
        !producer_literal(write, context, "u, ") ||
        !producer_c_string(write, context, function->name) ||
        !producer_literal(write, context, ", "))
      return false;

    if (function->param_count == 0u) {
      if (!producer_literal(write, context, "NULL"))
        return false;
    } else if (!producer_symbol(write, context, prefix, "_adapter_params_") ||
               !producer_size(write, context, i)) {
      return false;
    }

    if (!producer_literal(write, context, ", ") ||
        !producer_size(write, context, function->param_count) ||
        !producer_literal(write, context, "u, ") ||
        !producer_symbol(write, context, prefix, suffix) ||
        !producer_literal(write, context, ", ") ||
        !producer_size(write, context, (size_t)function->effects) ||
        !producer_literal(write, context, "u, ") ||
        !producer_size(write, context, (size_t)function->properties) ||
        !producer_literal(write, context, "u},\n"))
      return false;
  }

  return producer_literal(write, context, "};\n\nstatic const size_t ") &&
         producer_symbol(write, context, prefix, "_adapter_function_count") &&
         producer_literal(write, context, " = ") &&
         producer_size(write, context, count) &&
         producer_literal(write, context, "u;\n\n#endif /* ") &&
         producer_symbol(write, context, prefix, "_adapter_plan_generated_h") &&
         producer_literal(write, context, " */\n");
}

DataBindCMetaAdapterPlanStatus data_bind_cmeta_adapter_plan_emit(
    const DataBindCMetaFunctionManifest *manifest,
    const DataBindCMetaAdapterPlanConfig *config,
    DataBindCMetaAdapterPlanWriteFn write,
    void *write_context,
    size_t *out_error_index) {
  const cmeta_function_desc **functions;
  DataBindCMetaAdapterPlanStatus status;
  size_t i;

  if (out_error_index != NULL) *out_error_index = SIZE_MAX;
  if (manifest == NULL || config == NULL || write == NULL ||
      manifest->count == 0u || manifest->at == NULL ||
      !producer_identifier_valid(config->symbol_prefix))
    return DATA_BIND_CMETA_ADAPTER_PLAN_INVALID_ARGUMENT;
  if (manifest->count > SIZE_MAX / sizeof(*functions))
    return DATA_BIND_CMETA_ADAPTER_PLAN_OUT_OF_MEMORY;

  functions = (const cmeta_function_desc **)calloc(
      manifest->count, sizeof(*functions));
  if (functions == NULL)
    return DATA_BIND_CMETA_ADAPTER_PLAN_OUT_OF_MEMORY;

  for (i = 0u; i < manifest->count; ++i)
    functions[i] = manifest->at(manifest->context, i);

  status = producer_validate(functions, manifest->count, out_error_index);
  if (status == DATA_BIND_CMETA_ADAPTER_PLAN_OK) {
    if (!producer_emit_header_begin(
            write, write_context, config->symbol_prefix)) {
      status = DATA_BIND_CMETA_ADAPTER_PLAN_WRITE_FAILED;
    } else {
      for (i = 0u; i < manifest->count; ++i) {
        if (!producer_emit_param_array(
                write, write_context, config->symbol_prefix, functions[i], i)) {
          status = DATA_BIND_CMETA_ADAPTER_PLAN_WRITE_FAILED;
          break;
        }
      }
      if (status == DATA_BIND_CMETA_ADAPTER_PLAN_OK &&
          !producer_emit_function_rows(
              write, write_context, config->symbol_prefix,
              functions, manifest->count))
        status = DATA_BIND_CMETA_ADAPTER_PLAN_WRITE_FAILED;
    }
  }

  free(functions);
  return status;
}

const char *data_bind_cmeta_adapter_plan_status_name(
    DataBindCMetaAdapterPlanStatus status) {
  switch (status) {
  case DATA_BIND_CMETA_ADAPTER_PLAN_OK:
    return "ok";
  case DATA_BIND_CMETA_ADAPTER_PLAN_INVALID_ARGUMENT:
    return "invalid_argument";
  case DATA_BIND_CMETA_ADAPTER_PLAN_INVALID_DESCRIPTOR:
    return "invalid_descriptor";
  case DATA_BIND_CMETA_ADAPTER_PLAN_UNSUPPORTED_TYPE:
    return "unsupported_type";
  case DATA_BIND_CMETA_ADAPTER_PLAN_WRITE_FAILED:
    return "write_failed";
  case DATA_BIND_CMETA_ADAPTER_PLAN_OUT_OF_MEMORY:
    return "out_of_memory";
  }
  return "unknown";
}
