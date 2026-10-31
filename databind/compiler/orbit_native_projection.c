#include "orbit_native_projection.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Only canonical decimal, positive, non-wrapping IDs are permitted. */
static int orbit_positive_u64(const char *text, uint64_t *out) {
  uint64_t value = 0u;
  const unsigned char *p = (const unsigned char *)text;
  if (p == NULL || *p == 0u || out == NULL) return 0;
  while (*p != 0u) {
    uint64_t digit;
    if (*p < '0' || *p > '9') return 0;
    digit = (uint64_t)(*p++ - '0');
    if (value > (UINT64_MAX - digit) / 10u) return 0;
    value = value * 10u + digit;
  }
  if (value == 0u) return 0;
  *out = value;
  return 1;
}

static int orbit_identifier(const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (!p || !((*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') || *p == '_')) return 0;
  for (++p; *p; ++p)
    if (!((*p >= 'A' && *p <= 'Z') ||
          (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') || *p == '_'))
      return 0;
  return 1;
}

static const IdlAnnotation *orbit_annotation(const IdlOperation *op) {
  if (op == NULL) return NULL;
  return idl_annotation_find(
      op->annotations, op->annotation_count, "orbit", 0u);
}

static int orbit_scalar_record(const IdlContract *contract,
                               const char *type, const char **out_member) {
  const IdlDataDecl *record;
  const IdlField *field;
  if (contract == NULL || type == NULL || out_member == NULL) return 0;
  record = idl_contract_find_data(contract, type);
  if (record == NULL || record->kind != IDL_DATA_MESSAGE ||
      record->field_count != 1u || record->fields == NULL)
    return 0;
  field = &record->fields[0];
  if (!orbit_identifier(field->name) ||
      field->type_name == NULL || strcmp(field->type_name, "uint64") != 0 ||
      field->collection_kind != IDL_COLLECTION_NONE ||
      field->optional || field->nullable ||
      field->default_value != NULL || field->length != NULL ||
      field->annotation_count != 0u || field->constraint_count != 0u)
    return 0;
  *out_member = field->name;
  return 1;
}

static int orbit_operation_ids(const IdlOperation *op,
                               uint64_t *out_method,
                               uint32_t *out_event) {
  const IdlAnnotation *tag = orbit_annotation(op);
  uint64_t method = 0u;
  uint64_t event = 0u;
  if (tag == NULL || idl_annotation_count(
          op->annotations, op->annotation_count, "orbit") != 1u ||
      tag->bare || tag->argument_count != 2u ||
      tag->arguments == NULL ||
      !orbit_positive_u64(tag->arguments[0], &method) ||
      !orbit_positive_u64(tag->arguments[1], &event) ||
      event > UINT32_MAX)
    return 0;
  *out_method = method;
  *out_event = (uint32_t)event;
  return 1;
}

int databind_compiler_orbit_native_selected(const IdlContract *contract) {
  size_t s, o;
  if (contract == NULL) return 0;
  for (s = 0u; s < contract->service_count; ++s)
    for (o = 0u; o < contract->services[s].operation_count; ++o)
      if (orbit_annotation(&contract->services[s].operations[o]) != NULL)
        return 1;
  return 0;
}

int databind_compiler_orbit_native_admitted(const IdlContract *contract) {
  size_t s, i, j;
  if (contract == NULL) return 0;
  for (s = 0u; s < contract->service_count; ++s) {
    const IdlService *service = &contract->services[s];
    int marked = 0;
    uint64_t previous_id = 0u;
    if (!orbit_identifier(contract->name) ||
        !orbit_identifier(service->name))
      return 0;
    for (i = 0u; i < service->operation_count; ++i)
      if (orbit_annotation(&service->operations[i]) != NULL) marked = 1;
    if (!marked) continue;

    for (i = 0u; i < service->operation_count; ++i) {
      const IdlOperation *op = &service->operations[i];
      const char *request_member = NULL;
      const char *response_member = NULL;
      uint64_t method;
      uint32_t event;
      if (!orbit_identifier(op->name) ||
          !orbit_operation_ids(op, &method, &event) ||
          method <= previous_id || op->error_count != 0u ||
          !orbit_scalar_record(contract, op->request_type, &request_member) ||
          !orbit_scalar_record(contract, op->response_type, &response_member)) {
        fprintf(stderr,
            "Orbit native unary requires [orbit(method,event)] ordered IDs, "
            "no typed errors and required single-uint64 messages: %s.%s\n",
            service->name, op->name);
        return 0;
      }
      previous_id = method;
      for (j = 0u; j < i; ++j) {
        uint64_t old_method;
        uint32_t old_event;
        if (!orbit_operation_ids(
                &service->operations[j], &old_method, &old_event) ||
            old_event == event) {
          fprintf(stderr, "Orbit native unary duplicate Event ID: %s.%s\n",
                  service->name, op->name);
          return 0;
        }
      }
    }
  }
  return 1;
}

static const databind_compiler_service_native_operation *
orbit_native_find(const databind_compiler_service_native_ir *services,
                  const char *service, const char *operation) {
  size_t i;
  if (services == NULL) return NULL;
  for (i = 0u; i < services->operation_count; ++i) {
    const databind_compiler_service_native_operation *item =
        &services->operations[i];
    if (item->service_name != NULL && item->operation_name != NULL &&
        strcmp(item->service_name, service) == 0 &&
        strcmp(item->operation_name, operation) == 0)
      return item;
  }
  return NULL;
}

static int orbit_native_emit_method(FILE *file,
    const databind_compiler_service_native_operation *native,
    const char *request_member, const char *response_member,
    uint64_t method) {
  const char *symbol = native != NULL ? native->symbol : NULL;
  const char *request = native != NULL ? native->request_type : NULL;
  const char *response = native != NULL ? native->response_type : NULL;
  if (!file || !symbol || !request || !response ||
      !orbit_identifier(symbol) ||
      !orbit_identifier(request_member) || !orbit_identifier(response_member))
    return 0;

  /*
   * Context is caller-owned, single-call and address-stable until terminal.
   * On rejection the callback is NOT invoked and the context can be reused.
   * On success callback may run even before call_async returns.
   *
   * Response views are valid ONLY during this callback. For errors, pass NULL
   * and preserve the actual Orbit terminal/application status without
   * synthesizing a successful business response.
   */
  return fprintf(file,
    "typedef void (*%s__orbit_done_fn)(void *user, uint64_t token,\n"
    "    orbit_call_terminal terminal, int application_status,\n"
    "    const %s *response);\n"
    "typedef struct %s__orbit_context {\n"
    "    %s__orbit_done_fn done;\n"
    "    void *user;\n"
    "} %s__orbit_context;\n"
    "static inline void %s__orbit_on_terminal(void *opaque,\n"
    "    uint64_t token, orbit_call_terminal terminal, int application_status,\n"
    "    uint64_t scalar) {\n"
    "    %s__orbit_context *ctx = (%s__orbit_context *)opaque;\n"
    "    %s record = {0};\n"
    "    const %s *view = NULL;\n"
    "    if (terminal == ORBIT_CALL_TERMINAL_SUCCESS) {\n"
    "        record.%s = scalar;\n"
    "        view = &record;\n"
    "    }\n"
    "    ctx->done(ctx->user, token, terminal, application_status, view);\n"
    "}\n"
    "static inline orbit_call_status %s__orbit_call_async(\n"
    "    orbit_call_port *port, const orbit_service_ref *ref,\n"
    "    const %s *request, %s__orbit_context *context,\n"
    "    %s__orbit_done_fn done, void *user, uint64_t *out_token,\n"
    "    cflow_actor_send_status *out_send) {\n"
    "    if (request == NULL || context == NULL || done == NULL)\n"
    "        return ORBIT_CALL_INVALID_ARGUMENT;\n"
    "    context->done = done;\n"
    "    context->user = user;\n"
    "    return orbit_call_u64_async(port, ref, UINT64_C(%llu),\n"
    "        request->%s, %s__orbit_on_terminal, context,\n"
    "        out_token, out_send);\n"
    "}\n\n",
    symbol, response, symbol, symbol, symbol,
    symbol, symbol, symbol, response, response, response_member,
    symbol, request, symbol, symbol,
    (unsigned long long)method, request_member, symbol) >= 0;
}

int databind_compiler_orbit_native_write_header(
    FILE *file, const IdlContract *contract,
    const databind_compiler_service_native_ir *services) {
  size_t s, i;
  if (file == NULL || contract == NULL || services == NULL ||
      !databind_compiler_orbit_native_admitted(contract))
    return 0;
  if (!databind_compiler_orbit_native_selected(contract)) return 1;
  if (fputs("\n#if defined(DATABIND_NATIVE_ENABLE_ORBIT)\n"
            "/* Compiler projection only; use Orbit::Core as the sole runtime. */\n",
            file) == EOF)
    return 0;

  for (s = 0u; s < contract->service_count; ++s) {
    const IdlService *service = &contract->services[s];
    int marked = 0;
    for (i = 0u; i < service->operation_count; ++i)
      if (orbit_annotation(&service->operations[i]) != NULL) marked = 1;
    if (!marked) continue;

    if (fprintf(file, "static const orbit_service_method "
        "databind_%zu_%s_%zu_%s__orbit_methods[] = {\n",
        strlen(contract->name), contract->name,
        strlen(service->name), service->name) < 0)
      return 0;
    for (i = 0u; i < service->operation_count; ++i) {
      uint64_t method;
      uint32_t event;
      if (!orbit_operation_ids(&service->operations[i], &method, &event) ||
          fprintf(file, "    { UINT64_C(%llu), UINT32_C(%lu), "
                        "&cmeta_type_uint64, ORBIT_METHOD_CALL_TOKEN },\n",
                  (unsigned long long)method, (unsigned long)event) < 0)
        return 0;
    }
    if (fputs("};\n", file) == EOF) return 0;

    for (i = 0u; i < service->operation_count; ++i) {
      const IdlOperation *op = &service->operations[i];
      const databind_compiler_service_native_operation *native =
          orbit_native_find(services, service->name, op->name);
      const char *request_member = NULL;
      const char *response_member = NULL;
      uint64_t method;
      uint32_t event;
      if (!native || !orbit_operation_ids(op, &method, &event) ||
          !orbit_scalar_record(contract, op->request_type, &request_member) ||
          !orbit_scalar_record(contract, op->response_type, &response_member) ||
          !orbit_native_emit_method(
              file, native, request_member, response_member, method))
        return 0;
    }
  }
  return fputs("#endif /* DATABIND_NATIVE_ENABLE_ORBIT */\n",
               file) != EOF;
}
