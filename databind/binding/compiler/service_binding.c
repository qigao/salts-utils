#include "service_binding.h"

#include <stdint.h>
#include <stdio.h>

static int emit_error_array(
    FILE *file,
    const databind_binding_compiler_service *service) {
  size_t i;
  if (service == NULL) return -1;
  if (service->error_count == 0u) return 0;
  if (file == NULL || service->symbol == NULL || service->errors == NULL)
    return -1;

  if (fprintf(
          file,
          "static const DataBindNativeErrorBinding %s__error_bindings[] = {\n",
          service->symbol) < 0)
    return -1;

  for (i = 0u; i < service->error_count; ++i) {
    const databind_binding_compiler_error *error = &service->errors[i];
    if (error->type_name == NULL ||
        error->kind_value != (unsigned)(i + 1u) ||
        fprintf(
            file,
            "  {sizeof(DataBindNativeErrorBinding), \"%s\", %uu, "
            "%s_cmeta_data, offsetof(%s__error, payload.error_%zu)},\n",
            error->type_name, error->kind_value, error->type_name,
            service->symbol, i + 1u) < 0)
      return -1;
  }

  return fputs("};\n", file) == EOF ? -1 : 0;
}

int databind_binding_compiler_emit_service(
    FILE *file,
    const databind_binding_compiler_service *service) {
  const char *error_bindings_expr;
  char request_symbol[640];
  char response_symbol[640];
  char error_bindings[640];
  char error_envelope_size[720];
  char error_kind_offset[720];

  if (file == NULL || service == NULL ||
      service->symbol == NULL ||
      service->request.schema_name == NULL ||
      service->request.type_name == NULL ||
      service->response.type_name == NULL)
    return -1;

  if (snprintf(
          request_symbol, sizeof(request_symbol),
          "%s__request", service->symbol) <= 0 ||
      snprintf(
          response_symbol, sizeof(response_symbol),
          "%s__response", service->symbol) <= 0)
    return -1;

  if (databind_compiler_message_native_emit_binding(
          file, &service->request, request_symbol) != 0 ||
      databind_compiler_message_native_emit_binding(
          file, &service->response, response_symbol) != 0 ||
      emit_error_array(file, service) != 0)
    return -1;

  if (service->error_count != 0u) {
    if (snprintf(error_bindings, sizeof(error_bindings),
                 "%s__error_bindings", service->symbol) <= 0 ||
        snprintf(error_envelope_size, sizeof(error_envelope_size),
                 "sizeof(%s__error)", service->symbol) <= 0 ||
        snprintf(error_kind_offset, sizeof(error_kind_offset),
                 "offsetof(%s__error, kind)", service->symbol) <= 0)
      return -1;
    error_bindings_expr = error_bindings;
  } else {
    error_bindings_expr = "NULL";
    snprintf(error_envelope_size, sizeof(error_envelope_size), "0u");
    snprintf(error_kind_offset, sizeof(error_kind_offset), "0u");
  }

  return fprintf(
             file,
             "DataBindStatus %s__databind_native_binding(\n"
             "    DataBindNativeTypeBinding *request_out,\n"
             "    DataBindNativeTypeBinding *response_out,\n"
             "    DataBindServiceNativeBinding *service_out,\n"
             "    DataBindError *error) {\n"
             "  DataBindStatus status;\n"
             "  if (request_out == NULL || response_out == NULL || "
             "service_out == NULL)\n"
             "    return DATA_BIND_ERR_INVALID_ARG;\n"
             "  status = %s__databind_message_native_binding("
             "request_out, error);\n"
             "  if (status != DATA_BIND_OK) return status;\n"
             "  status = %s__databind_message_native_binding("
             "response_out, error);\n"
             "  if (status != DATA_BIND_OK) return status;\n"
             "  *service_out = (DataBindServiceNativeBinding){\n"
             "      sizeof(DataBindServiceNativeBinding),\n"
             "      DATA_BIND_BINDING_PLAN_ABI_VERSION,\n"
             "      &%s__function_meta, request_out, response_out,\n"
             "      %s, %zuu, %s, %s, %s, %s};\n"
             "  return DATA_BIND_OK;\n"
             "}\n",
             service->symbol,
             request_symbol,
             response_symbol,
             service->symbol,
             error_bindings_expr,
             service->error_count,
             service->error_count != 0u ? "2u" : "SIZE_MAX",
             error_envelope_size,
             error_kind_offset,
             service->error_count != 0u ? "sizeof(uint32_t)" : "0u") < 0
             ? -1
             : 0;
}
