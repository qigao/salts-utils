#include "service_native.h"
#include "service_binding.h"
#include "schema_cmeta.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *native_strdup(const char *text) {
  size_t length;
  char *copy;
  if (text == NULL) return NULL;
  length = strlen(text);
  if (length == SIZE_MAX) return NULL;
  copy = (char *)malloc(length + 1u);
  if (copy == NULL) return NULL;
  memcpy(copy, text, length + 1u);
  return copy;
}

static char *native_join2(
    const char *left, const char *right, char separator) {
  size_t a, b;
  char *out;
  if (left == NULL || right == NULL) return NULL;
  a = strlen(left);
  b = strlen(right);
  if (a > SIZE_MAX - b - 2u) return NULL;
  out = (char *)malloc(a + b + 2u);
  if (out == NULL) return NULL;
  memcpy(out, left, a);
  out[a] = separator;
  memcpy(out + a + 1u, right, b + 1u);
  return out;
}

static char *native_join3(
    const char *a, const char *b, const char *c, char separator) {
  char *prefix = native_join2(a, b, separator);
  char *out;
  if (prefix == NULL) return NULL;
  out = native_join2(prefix, c, separator);
  free(prefix);
  return out;
}

static int native_identifier_valid(const char *text) {
  size_t i;
  if (text == NULL || text[0] == '\0') return 0;
  if (!((text[0] >= 'A' && text[0] <= 'Z') ||
        (text[0] >= 'a' && text[0] <= 'z') ||
        text[0] == '_'))
    return 0;
  for (i = 1u; text[i] != '\0'; ++i) {
    unsigned char ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') ||
          (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') ||
          ch == '_'))
      return 0;
  }
  return 1;
}

static size_t native_decimal_digits(size_t value) {
  size_t digits = 1u;
  while (value >= 10u) {
    value /= 10u;
    ++digits;
  }
  return digits;
}

/*
 * Length-prefix every semantic identifier so C symbol lowering is injective.
 *
 * Example:
 *   Image / Codec / Decode
 *     -> databind_5_Image_5_Codec_6_Decode
 *
 * This avoids collisions such as:
 *   A_B / C
 *   A   / B_C
 * that ordinary underscore concatenation cannot distinguish.
 */
static char *native_symbol(
    const char *schema, const char *service, const char *operation) {
  static const char prefix[] = "databind";
  const char *parts[] = {schema, service, operation};
  size_t lengths[3];
  size_t total = sizeof(prefix) - 1u;
  size_t i;
  size_t used;
  char *out;

  for (i = 0u; i < 3u; ++i) {
    if (!native_identifier_valid(parts[i])) return NULL;
    lengths[i] = strlen(parts[i]);
    if (total > SIZE_MAX - 2u -
                    native_decimal_digits(lengths[i]) -
                    lengths[i])
      return NULL;
    total += 2u + native_decimal_digits(lengths[i]) + lengths[i];
  }

  out = (char *)malloc(total + 1u);
  if (out == NULL) return NULL;

  memcpy(out, prefix, sizeof(prefix) - 1u);
  used = sizeof(prefix) - 1u;
  for (i = 0u; i < 3u; ++i) {
    int written;
    out[used++] = '_';
    written = snprintf(
        out + used, total + 1u - used, "%zu", lengths[i]);
    if (written <= 0 || (size_t)written >= total + 1u - used) {
      free(out);
      return NULL;
    }
    used += (size_t)written;
    out[used++] = '_';
    memcpy(out + used, parts[i], lengths[i]);
    used += lengths[i];
  }
  out[used] = '\0';
  return out;
}

static void native_state_clear(
    databind_compiler_service_native_state *state,
    size_t count) {
  size_t i;
  if (state == NULL) return;
  for (i = 0u; i < count; ++i) free(state[i].field_name);
  free(state);
}

static char *native_type_identity(
    const char *schema, const char *type_name) {
  static const char prefix[] = "tbe.native.";
  static const char suffix[] = "_t";
  size_t a, b, total;
  char *out;
  if (schema == NULL || type_name == NULL) return NULL;
  a = strlen(schema);
  b = strlen(type_name);
  if (a > SIZE_MAX - b - sizeof(prefix) - sizeof(suffix) - 1u) return NULL;
  total = sizeof(prefix) - 1u + a + 1u + b + sizeof(suffix) - 1u;
  out = (char *)malloc(total + 1u);
  if (out == NULL) return NULL;
  memcpy(out, prefix, sizeof(prefix) - 1u);
  memcpy(out + sizeof(prefix) - 1u, schema, a);
  out[sizeof(prefix) - 1u + a] = '.';
  memcpy(out + sizeof(prefix) + a, type_name, b);
  memcpy(out + sizeof(prefix) + a + b, suffix, sizeof(suffix));
  return out;
}

static void native_error_fields_clear(
    databind_compiler_service_native_error_field *fields,
    size_t count) {
  size_t i;
  if (fields == NULL) return;
  for (i = 0u; i < count; ++i) {
    free(fields[i].member_name);
    free(fields[i].native_data_symbol);
  }
  free(fields);
}

static void native_errors_clear(
    databind_compiler_service_native_error *errors,
    size_t count) {
  size_t i;
  if (errors == NULL) return;
  for (i = 0u; i < count; ++i) {
    free(errors[i].type_name);
    free(errors[i].type_identity);
    native_error_fields_clear(errors[i].fields, errors[i].field_count);
  }
  free(errors);
}

static const char *native_field_annotation(
    const IdlField *field, const char *name) {
  size_t i;
  if (field == NULL || name == NULL) return NULL;
  for (i = 0u; i < field->annotation_count; ++i)
    if (field->annotations[i].name != NULL &&
        strcmp(field->annotations[i].name, name) == 0)
      return field->annotations[i].value;
  return NULL;
}

static int native_decimal_literal(const char *text) {
  const char *p;
  if (text == NULL || text[0] == '\0') return 0;
  for (p = text; *p != '\0'; ++p)
    if (*p < '0' || *p > '9') return 0;
  return 1;
}

static const char *native_field_member_name(const IdlField *field) {
  const char *override;
  if (field == NULL || field->name == NULL || field->name[0] == '\0')
    return NULL;
  override = native_field_annotation(field, "c");
  return override != NULL && override[0] != '\0' ? override : field->name;
}

static int native_error_field_admitted(
    const IdlContract *contract,
    const IdlField *field,
    const char **out_lifecycle_data_symbol) {
  schema_cmeta_field_type semantic;
  const IdlDataDecl *decl;

  if (out_lifecycle_data_symbol != NULL) *out_lifecycle_data_symbol = NULL;
  if (contract == NULL || field == NULL || field->type_name == NULL)
    return 0;

  /* Historical Service error ABI intentionally rejects overlay state. */
  if (field->optional || field->nullable)
    return 0;

  /* Containers/nested aggregate storage was never admitted by this path. */
  if (field->collection_kind != IDL_COLLECTION_NONE)
    return 0;

  if (strcmp(field->type_name, "string") == 0) {
    if (out_lifecycle_data_symbol != NULL)
      *out_lifecycle_data_symbol = "salts_tstr_cmeta_data";
    return 1;
  }

  if (strcmp(field->type_name, "bytes") == 0) {
    if (native_decimal_literal(field->length))
      return 1;
    if (out_lifecycle_data_symbol != NULL)
      *out_lifecycle_data_symbol = "stl_byte_buffer_cmeta_data";
    return 1;
  }

  if (strcmp(field->type_name, "uuid") == 0)
    return 1;

  decl = idl_contract_find_data(contract, field->type_name);
  if (decl != NULL && decl->kind == IDL_DATA_ENUM)
    return 1;

  if (!schema_cmeta_field_resolve(contract, field, &semantic))
    return 0;

  return semantic.kind == CMETA_DATA_BOOL ||
         semantic.kind == CMETA_DATA_SINT ||
         semantic.kind == CMETA_DATA_UINT ||
         semantic.kind == CMETA_DATA_FLOAT;
}

static int native_error_fields_build(
    const IdlContract *contract,
    const char *type_name,
    databind_compiler_service_native_error_field **out_fields,
    size_t *out_count) {
  const IdlDataDecl *message;
  databind_compiler_service_native_error_field *result = NULL;
  size_t i;

  if (out_fields == NULL || out_count == NULL) return 0;
  *out_fields = NULL;
  *out_count = 0u;

  if (contract == NULL || type_name == NULL) return 0;
  message = idl_contract_find_data(contract, type_name);
  if (message == NULL || message->kind != IDL_DATA_MESSAGE)
    return 0;
  if (message->field_count == 0u) return 1;

  result = (databind_compiler_service_native_error_field *)calloc(
      message->field_count, sizeof(*result));
  if (result == NULL) return 0;

  for (i = 0u; i < message->field_count; ++i) {
    const IdlField *field = &message->fields[i];
    const char *member = native_field_member_name(field);
    const char *lifecycle_data_symbol = NULL;

    if (member == NULL || !native_identifier_valid(member) ||
        !native_error_field_admitted(contract, field, &lifecycle_data_symbol)) {
      native_error_fields_clear(result, message->field_count);
      return 0;
    }

    result[i].member_name = native_strdup(member);
    if (lifecycle_data_symbol != NULL)
      result[i].native_data_symbol = native_strdup(lifecycle_data_symbol);

    if (result[i].member_name == NULL ||
        (lifecycle_data_symbol != NULL &&
         result[i].native_data_symbol == NULL)) {
      native_error_fields_clear(result, message->field_count);
      return 0;
    }
  }

  *out_fields = result;
  *out_count = message->field_count;
  return 1;
}

static int native_errors_build(
    const IdlContract *contract,
    const char *schema_name,
    const IdlOperation *operation,
    databind_compiler_service_native_error **out_errors,
    size_t *out_count) {
  databind_compiler_service_native_error *result = NULL;
  size_t i;

  if (out_errors == NULL || out_count == NULL ||
      contract == NULL || schema_name == NULL || operation == NULL)
    return 0;

  *out_errors = NULL;
  *out_count = 0u;
  if (operation->error_count == 0u) return 1;
  if (operation->error_types == NULL) return 0;

  result = (databind_compiler_service_native_error *)calloc(
      operation->error_count, sizeof(*result));
  if (result == NULL) return 0;

  for (i = 0u; i < operation->error_count; ++i) {
    const char *type_name = operation->error_types[i];

    if (type_name == NULL ||
        !native_error_fields_build(
            contract, type_name,
            &result[i].fields, &result[i].field_count)) {
      native_errors_clear(result, operation->error_count);
      return 0;
    }

    result[i].type_name = native_strdup(type_name);
    result[i].type_identity =
        native_type_identity(schema_name, type_name);
    result[i].kind_value = (unsigned)(i + 1u);

    if (result[i].type_name == NULL ||
        result[i].type_identity == NULL) {
      native_errors_clear(result, operation->error_count);
      return 0;
    }
  }

  *out_errors = result;
  *out_count = operation->error_count;
  return 1;
}


static void native_operation_clear(
    databind_compiler_service_native_operation *operation) {
  if (operation == NULL) return;
  free(operation->schema_name);
  free(operation->service_name);
  free(operation->operation_name);
  free(operation->qualified_service);
  free(operation->qualified_operation);
  free(operation->symbol);
  free(operation->request_type);
  free(operation->response_type);
  free(operation->request_type_identity);
  free(operation->response_type_identity);
  native_state_clear(
      operation->request_presence, operation->request_presence_count);
  native_state_clear(
      operation->request_nulls, operation->request_null_count);
  native_state_clear(
      operation->response_presence, operation->response_presence_count);
  native_state_clear(
      operation->response_nulls, operation->response_null_count);
  native_errors_clear(operation->errors, operation->error_count);
  memset(operation, 0, sizeof(*operation));
}

void databind_compiler_service_native_destroy(
    databind_compiler_service_native_ir *ir) {
  size_t i;
  if (ir == NULL) return;
  for (i = 0u; i < ir->operation_count; ++i)
    native_operation_clear(&ir->operations[i]);
  free(ir->operations);
  memset(ir, 0, sizeof(*ir));
}

static int native_operation_fill(
    const IdlContract *contract,
    const char *schema_name,
    const char *service_name,
    const IdlOperation *operation,
    databind_compiler_service_native_operation *out) {
  databind_compiler_message_native_binding request = {0};
  databind_compiler_message_native_binding response = {0};
  const char *operation_name;
  const char *request_type;
  const char *response_type;
  int ok = 0;

  if (contract == NULL || out == NULL || operation == NULL)
    return 0;

  operation_name = operation->name;
  request_type = operation->request_type;
  response_type = operation->response_type;
  if (operation_name == NULL || request_type == NULL || response_type == NULL)
    return 0;

  if (databind_compiler_message_native_build(
          contract, request_type, &request) != 0 ||
      databind_compiler_message_native_build(
          contract, response_type, &response) != 0)
    goto cleanup;

  out->schema_name = native_strdup(schema_name);
  out->service_name = native_strdup(service_name);
  out->operation_name = native_strdup(operation_name);
  out->qualified_service =
      native_join2(schema_name, service_name, '.');
  out->qualified_operation =
      native_join3(schema_name, service_name, operation_name, '.');
  out->symbol = native_symbol(schema_name, service_name, operation_name);

  out->request_type = request.type_name;
  request.type_name = NULL;
  out->request_type_identity = request.type_identity;
  request.type_identity = NULL;
  out->request_presence =
      (databind_compiler_service_native_state *)request.presence;
  request.presence = NULL;
  out->request_presence_count = request.presence_count;
  request.presence_count = 0u;
  out->request_nulls =
      (databind_compiler_service_native_state *)request.nulls;
  request.nulls = NULL;
  out->request_null_count = request.null_count;
  request.null_count = 0u;

  out->response_type = response.type_name;
  response.type_name = NULL;
  out->response_type_identity = response.type_identity;
  response.type_identity = NULL;
  out->response_presence =
      (databind_compiler_service_native_state *)response.presence;
  response.presence = NULL;
  out->response_presence_count = response.presence_count;
  response.presence_count = 0u;
  out->response_nulls =
      (databind_compiler_service_native_state *)response.nulls;
  response.nulls = NULL;
  out->response_null_count = response.null_count;
  response.null_count = 0u;

  if (!native_errors_build(
          contract, schema_name, operation,
          &out->errors, &out->error_count))
    goto cleanup;

  ok = out->schema_name != NULL &&
       out->service_name != NULL &&
       out->operation_name != NULL &&
       out->qualified_service != NULL &&
       out->qualified_operation != NULL &&
       out->symbol != NULL &&
       out->request_type != NULL &&
       out->response_type != NULL &&
       out->request_type_identity != NULL &&
       out->response_type_identity != NULL;

cleanup:
  databind_compiler_message_native_destroy(&request);
  databind_compiler_message_native_destroy(&response);
  return ok;
}


int databind_compiler_service_native_build_selected(
    const IdlContract *contract,
    databind_compiler_service_native_select_fn select_service,
    void *select_context,
    databind_compiler_service_native_ir *out) {
  const char *schema_name;
  size_t total = 0u;
  size_t i, j, index = 0u;

  if (out == NULL) return -1;
  memset(out, 0, sizeof(*out));
  if (contract == NULL) return -1;

  schema_name =
      contract->name != NULL && contract->name[0] != '\0'
          ? contract->name
          : "GeneratedSchema";

  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    if (service->name == NULL || service->name[0] == '\0') return -1;
    if (select_service != NULL &&
        !select_service(select_context, service->name))
      continue;
    if (total > SIZE_MAX - service->operation_count) return -1;
    total += service->operation_count;
  }
  if (total == 0u) return -1;

  out->operations = (databind_compiler_service_native_operation *)calloc(
      total, sizeof(*out->operations));
  if (out->operations == NULL) return -1;
  out->operation_count = total;

  for (i = 0u; i < contract->service_count; ++i) {
    const IdlService *service = &contract->services[i];
    if (select_service != NULL &&
        !select_service(select_context, service->name))
      continue;

    for (j = 0u; j < service->operation_count; ++j, ++index) {
      size_t prior;
      if (!native_operation_fill(
              contract, schema_name, service->name,
              &service->operations[j], &out->operations[index]))
        goto fail;
      for (prior = 0u; prior < index; ++prior)
        if (strcmp(out->operations[prior].symbol,
                   out->operations[index].symbol) == 0)
          goto fail;
    }
  }

  if (index != total) goto fail;
  return 0;

fail:
  databind_compiler_service_native_destroy(out);
  return -1;
}


int databind_compiler_service_native_build(
    const IdlContract *contract,
    databind_compiler_service_native_ir *out) {
  return databind_compiler_service_native_build_selected(
      contract, NULL, NULL, out);
}


static int native_emit_error_variant_helpers(
    FILE *file,
    const databind_compiler_service_native_operation *operation,
    const databind_compiler_service_native_error *error,
    size_t ordinal) {
  size_t i;
  size_t j;

  if (file == NULL || operation == NULL || error == NULL ||
      operation->symbol == NULL || error->type_name == NULL ||
      ordinal == 0u)
    return -1;

  if (fprintf(
          file,
          "static inline DataBindStatus %s__error_%zu_init(%s_t *payload) {\n"
          "  if (payload == NULL) return DATA_BIND_ERR_INVALID_ARG;\n"
          "  memset(payload, 0, sizeof(*payload));\n",
          operation->symbol, ordinal, error->type_name) < 0)
    return -1;

  for (i = 0u; i < error->field_count; ++i) {
    const databind_compiler_service_native_error_field *field =
        &error->fields[i];
    if (field->native_data_symbol == NULL) continue;
    if (field->member_name == NULL || field->native_data_symbol == NULL)
      return -1;
    if (fprintf(
            file,
            "  if (cmeta_data_value_init_zero(&%s, &payload->%s) != CMETA_OK) {\n",
            field->native_data_symbol, field->member_name) < 0)
      return -1;
    for (j = i; j != 0u; --j) {
      const databind_compiler_service_native_error_field *prior =
          &error->fields[j - 1u];
      if (prior->native_data_symbol != NULL &&
          fprintf(
              file,
              "    (void)cmeta_data_value_restore_zero(&%s, &payload->%s);\n",
              prior->native_data_symbol, prior->member_name) < 0)
        return -1;
    }
    if (fputs(
            "    memset(payload, 0, sizeof(*payload));\n"
            "    return DATA_BIND_ERR_RUNTIME;\n"
            "  }\n",
            file) == EOF)
      return -1;
  }

  if (fputs("  return DATA_BIND_OK;\n}\n", file) == EOF)
    return -1;

  if (fprintf(
          file,
          "static inline DataBindStatus %s__error_%zu_clear(%s_t *payload) {\n"
          "  if (payload == NULL) return DATA_BIND_ERR_INVALID_ARG;\n",
          operation->symbol, ordinal, error->type_name) < 0)
    return -1;

  for (i = error->field_count; i != 0u; --i) {
    const databind_compiler_service_native_error_field *field =
        &error->fields[i - 1u];
    if (field->native_data_symbol == NULL) continue;
    if (field->member_name == NULL || field->native_data_symbol == NULL)
      return -1;
    if (fprintf(
            file,
            "  if (cmeta_data_value_restore_zero(&%s, &payload->%s) != CMETA_OK)\n"
            "    return DATA_BIND_ERR_RUNTIME;\n",
            field->native_data_symbol, field->member_name) < 0)
      return -1;
  }

  if (fputs(
          "  memset(payload, 0, sizeof(*payload));\n"
          "  return DATA_BIND_OK;\n"
          "}\n",
          file) == EOF)
    return -1;

  if (fprintf(
          file,
          "static inline DataBindStatus %s__error_%zu_move(\n"
          "    %s_t *destination, %s_t *source) {\n"
          "  DataBindStatus status;\n"
          "  if (destination == NULL || source == NULL)\n"
          "    return DATA_BIND_ERR_INVALID_ARG;\n"
          "  if (destination == source) return DATA_BIND_OK;\n"
          "  status = %s__error_%zu_init(destination);\n"
          "  if (status != DATA_BIND_OK) return status;\n",
          operation->symbol, ordinal,
          error->type_name, error->type_name,
          operation->symbol, ordinal) < 0)
    return -1;

  for (i = 0u; i < error->field_count; ++i) {
    const databind_compiler_service_native_error_field *field =
        &error->fields[i];
    if (field->member_name == NULL) return -1;
    if (field->native_data_symbol == NULL) {
      if (fprintf(
              file,
              "  memcpy(&destination->%s, &source->%s, "
              "sizeof(destination->%s));\n",
              field->member_name, field->member_name,
              field->member_name) < 0)
        return -1;
      continue;
    }
    if (field->native_data_symbol == NULL) return -1;
    if (fprintf(
            file,
            "  if (cmeta_data_value_move(&%s, &destination->%s, "
            "&source->%s) != CMETA_OK) {\n",
            field->native_data_symbol, field->member_name,
            field->member_name) < 0)
      return -1;
    for (j = i; j != 0u; --j) {
      const databind_compiler_service_native_error_field *prior =
          &error->fields[j - 1u];
      if (prior->native_data_symbol != NULL &&
          fprintf(
              file,
              "    (void)cmeta_data_value_move(&%s, &source->%s, "
              "&destination->%s);\n",
              prior->native_data_symbol, prior->member_name,
              prior->member_name) < 0)
        return -1;
    }
    if (fprintf(
            file,
            "    (void)%s__error_%zu_clear(destination);\n"
            "    return DATA_BIND_ERR_RUNTIME;\n"
            "  }\n",
            operation->symbol, ordinal) < 0)
      return -1;
  }

  return fputs("  return DATA_BIND_OK;\n}\n", file) == EOF ? -1 : 0;
}

int databind_compiler_service_native_emit_prototype(
    FILE *file,
    const databind_compiler_service_native_operation *operation) {
  size_t i;

  if (file == NULL || operation == NULL ||
      operation->symbol == NULL ||
      operation->request_type == NULL ||
      operation->response_type == NULL)
    return -1;

  if (operation->error_count == 0u) {
    return fprintf(
               file,
               "int %s(const %s_t *request, %s_t *response);\n",
               operation->symbol,
               operation->request_type,
               operation->response_type) < 0
               ? -1
               : 0;
  }

  if (operation->errors == NULL) return -1;

  if (fprintf(
          file,
          "typedef uint32_t %s__error_kind;\n"
          "enum {\n"
          "  %s__ERROR_NONE = 0",
          operation->symbol, operation->symbol) < 0)
    return -1;

  for (i = 0u; i < operation->error_count; ++i) {
    if (operation->errors[i].type_name == NULL ||
        operation->errors[i].kind_value != (unsigned)(i + 1u) ||
        fprintf(
            file,
            ",\n  %s__ERROR_%zu = %uu",
            operation->symbol, i + 1u,
            operation->errors[i].kind_value) < 0)
      return -1;
  }

  if (fprintf(
          file,
          "\n};\n"
          "#define %s__ERROR_INIT {0}\n"
          "typedef struct %s__error {\n"
          "  %s__error_kind kind;\n"
          "  union {\n",
          operation->symbol,
          operation->symbol, operation->symbol) < 0)
    return -1;

  for (i = 0u; i < operation->error_count; ++i)
    if (fprintf(
            file,
            "    %s_t error_%zu;\n",
            operation->errors[i].type_name, i + 1u) < 0)
      return -1;

  if (fprintf(
          file,
          "  } payload;\n"
          "} %s__error;\n",
          operation->symbol) < 0)
    return -1;

  for (i = 0u; i < operation->error_count; ++i)
    if (native_emit_error_variant_helpers(
            file, operation, &operation->errors[i], i + 1u) != 0)
      return -1;

  if (fprintf(
          file,
          "static inline void %s__error_init(%s__error *error) {\n"
          "  if (error != NULL) memset(error, 0, sizeof(*error));\n"
          "}\n"
          "static inline DataBindStatus %s__error_clear(%s__error *error) {\n"
          "  DataBindStatus status = DATA_BIND_OK;\n"
          "  if (error == NULL) return DATA_BIND_ERR_INVALID_ARG;\n"
          "  switch (error->kind) {\n"
          "  case %s__ERROR_NONE:\n"
          "    %s__error_init(error);\n"
          "    return DATA_BIND_OK;\n",
          operation->symbol, operation->symbol,
          operation->symbol, operation->symbol,
          operation->symbol, operation->symbol) < 0)
    return -1;

  for (i = 0u; i < operation->error_count; ++i)
    if (fprintf(
            file,
            "  case %s__ERROR_%zu:\n"
            "    status = %s__error_%zu_clear(&error->payload.error_%zu);\n"
            "    break;\n",
            operation->symbol, i + 1u,
            operation->symbol, i + 1u, i + 1u) < 0)
      return -1;

  if (fprintf(
          file,
          "  default:\n"
          "    return DATA_BIND_ERR_INVALID_ARG;\n"
          "  }\n"
          "  if (status == DATA_BIND_OK) %s__error_init(error);\n"
          "  return status;\n"
          "}\n"
          "static inline DataBindStatus %s__error_select(\n"
          "    %s__error *error, %s__error_kind kind) {\n"
          "  DataBindStatus status;\n"
          "  if (error == NULL) return DATA_BIND_ERR_INVALID_ARG;\n"
          "  status = %s__error_clear(error);\n"
          "  if (status != DATA_BIND_OK || kind == %s__ERROR_NONE)\n"
          "    return status;\n"
          "  switch (kind) {\n",
          operation->symbol,
          operation->symbol, operation->symbol, operation->symbol,
          operation->symbol, operation->symbol) < 0)
    return -1;

  for (i = 0u; i < operation->error_count; ++i)
    if (fprintf(
            file,
            "  case %s__ERROR_%zu:\n"
            "    status = %s__error_%zu_init(&error->payload.error_%zu);\n"
            "    break;\n",
            operation->symbol, i + 1u,
            operation->symbol, i + 1u, i + 1u) < 0)
      return -1;

  if (fprintf(
          file,
          "  default:\n"
          "    return DATA_BIND_ERR_INVALID_ARG;\n"
          "  }\n"
          "  if (status != DATA_BIND_OK) {\n"
          "    %s__error_init(error);\n"
          "    return status;\n"
          "  }\n"
          "  error->kind = kind;\n"
          "  return DATA_BIND_OK;\n"
          "}\n"
          "static inline DataBindStatus %s__error_move(\n"
          "    %s__error *destination, %s__error *source) {\n"
          "  %s__error_kind kind;\n"
          "  DataBindStatus status;\n"
          "  if (destination == NULL || source == NULL)\n"
          "    return DATA_BIND_ERR_INVALID_ARG;\n"
          "  if (destination == source) return DATA_BIND_OK;\n"
          "  kind = source->kind;\n"
          "  status = %s__error_clear(destination);\n"
          "  if (status != DATA_BIND_OK || kind == %s__ERROR_NONE)\n"
          "    return status;\n"
          "  switch (kind) {\n",
          operation->symbol,
          operation->symbol,
          operation->symbol, operation->symbol, operation->symbol,
          operation->symbol,
          operation->symbol) < 0)
    return -1;

  for (i = 0u; i < operation->error_count; ++i)
    if (fprintf(
            file,
            "  case %s__ERROR_%zu:\n"
            "    status = %s__error_%zu_move(\n"
            "        &destination->payload.error_%zu, &source->payload.error_%zu);\n"
            "    break;\n",
            operation->symbol, i + 1u,
            operation->symbol, i + 1u, i + 1u, i + 1u) < 0)
      return -1;

  if (fprintf(
          file,
          "  default:\n"
          "    return DATA_BIND_ERR_INVALID_ARG;\n"
          "  }\n"
          "  if (status != DATA_BIND_OK) {\n"
          "    %s__error_init(destination);\n"
          "    return status;\n"
          "  }\n"
          "  destination->kind = kind;\n"
          "  source->kind = %s__ERROR_NONE;\n"
          "  return DATA_BIND_OK;\n"
          "}\n"
          "int %s(const %s_t *request, %s_t *response, %s__error *error);\n",
          operation->symbol,
          operation->symbol,
          operation->symbol,
          operation->request_type,
          operation->response_type,
          operation->symbol) < 0)
    return -1;

  return 0;
}

int databind_compiler_service_native_emit_reflection(
    FILE *file,
    const databind_compiler_service_native_operation *operation,
    int emit_accessors) {
  if (file == NULL || operation == NULL ||
      operation->symbol == NULL ||
      operation->qualified_operation == NULL ||
      operation->request_type == NULL ||
      operation->response_type == NULL ||
      operation->request_type_identity == NULL ||
      operation->response_type_identity == NULL)
    return -1;

  if (operation->error_count != 0u) {
    size_t i;
    if (operation->errors == NULL) return -1;
    for (i = 0u; i < operation->error_count; ++i)
      if (operation->errors[i].type_name == NULL ||
          operation->errors[i].type_identity == NULL)
        return -1;

    if (fprintf(
            file,
            "static const cmeta_type_identity %s__request_id =\n"
            "    CMETA_TYPE_ID_ATOM_INIT(\"%s\");\n"
            "static const cmeta_type_desc %s__request_type = {\n"
            "    \"%s_t\", sizeof(%s_t), _Alignof(%s_t),\n"
            "    CMETA_T_OBJECT, NULL, NULL, &%s__request_id};\n"
            "static const cmeta_type_desc %s__request_ptr_type = {\n"
            "    \"const %s_t *\", sizeof(const %s_t *), "
            "_Alignof(const %s_t *),\n"
            "    CMETA_T_POINTER, &%s__request_type, NULL, NULL};\n",
            operation->symbol,
            operation->request_type_identity,
            operation->symbol,
            operation->request_type,
            operation->request_type,
            operation->request_type,
            operation->symbol,
            operation->symbol,
            operation->request_type,
            operation->request_type,
            operation->request_type,
            operation->symbol) < 0)
      return -1;

    if (fprintf(
            file,
            "static const cmeta_type_identity %s__response_id =\n"
            "    CMETA_TYPE_ID_ATOM_INIT(\"%s\");\n"
            "static const cmeta_type_desc %s__response_type = {\n"
            "    \"%s_t\", sizeof(%s_t), _Alignof(%s_t),\n"
            "    CMETA_T_OBJECT, NULL, NULL, &%s__response_id};\n"
            "static const cmeta_type_desc %s__response_ptr_type = {\n"
            "    \"%s_t *\", sizeof(%s_t *), _Alignof(%s_t *),\n"
            "    CMETA_T_POINTER, &%s__response_type, NULL, NULL};\n",
            operation->symbol,
            operation->response_type_identity,
            operation->symbol,
            operation->response_type,
            operation->response_type,
            operation->response_type,
            operation->symbol,
            operation->symbol,
            operation->response_type,
            operation->response_type,
            operation->response_type,
            operation->symbol) < 0)
      return -1;

    if (fprintf(
            file,
            "static const cmeta_type_identity %s__error_id =\n"
            "    CMETA_TYPE_ID_ATOM_INIT(\"tbe.native.%s.error_t\");\n"
            "static const cmeta_type_desc %s__error_type = {\n"
            "    \"%s__error\", sizeof(%s__error), _Alignof(%s__error),\n"
            "    CMETA_T_OBJECT, NULL, NULL, &%s__error_id};\n"
            "static const cmeta_type_desc %s__error_ptr_type = {\n"
            "    \"%s__error *\", sizeof(%s__error *), "
            "_Alignof(%s__error *),\n"
            "    CMETA_T_POINTER, &%s__error_type, NULL, NULL};\n",
            operation->symbol,
            operation->qualified_operation,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return -1;

    if (fprintf(
            file,
            "CMETA_FUNCTION_METADATA_AS_ABI_RESULT(\n"
            "    %s, \"%s\", fallible, &cmeta_type_int, "
            "CMETA_ABI_SCALAR,\n"
            "    CMETA_RESULT_VALUE,\n"
            "    (const %s_t *, request,\n"
            "     CMETA_PARAM_IN | CMETA_PARAM_BORROWED,\n"
            "     &%s__request_ptr_type, CMETA_ABI_OBJECT_POINTER),\n"
            "    (%s_t *, response,\n"
            "     CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,\n"
            "     &%s__response_ptr_type, CMETA_ABI_OBJECT_POINTER),\n"
            "    (%s__error *, error,\n"
            "     CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,\n"
            "     &%s__error_ptr_type, CMETA_ABI_OBJECT_POINTER));\n",
            operation->symbol,
            operation->qualified_operation,
            operation->request_type,
            operation->symbol,
            operation->response_type,
            operation->symbol,
            operation->symbol,
            operation->symbol) < 0)
      return -1;

    if (emit_accessors &&
        fprintf(
            file,
            "const cmeta_function_desc *%s__databind_function(void) {\n"
            "  return &%s__function_meta;\n"
            "}\n"
            "const cmeta_function_abi_desc *%s__databind_function_abi(void) {\n"
            "  return &%s__function_abi_meta;\n"
            "}\n",
            operation->symbol, operation->symbol,
            operation->symbol, operation->symbol) < 0)
      return -1;
    return 0;
  }

  if (fprintf(
          file,
          "static const cmeta_type_identity %s__request_id =\n"
          "    CMETA_TYPE_ID_ATOM_INIT(\"%s\");\n"
          "static const cmeta_type_desc %s__request_type = {\n"
          "    \"%s_t\", sizeof(%s_t), _Alignof(%s_t),\n"
          "    CMETA_T_OBJECT, NULL, NULL, &%s__request_id};\n"
          "static const cmeta_type_desc %s__request_ptr_type = {\n"
          "    \"const %s_t *\", sizeof(const %s_t *), "
          "_Alignof(const %s_t *),\n"
          "    CMETA_T_POINTER, &%s__request_type, NULL, NULL};\n"
          "static const cmeta_type_identity %s__response_id =\n"
          "    CMETA_TYPE_ID_ATOM_INIT(\"%s\");\n"
          "static const cmeta_type_desc %s__response_type = {\n"
          "    \"%s_t\", sizeof(%s_t), _Alignof(%s_t),\n"
          "    CMETA_T_OBJECT, NULL, NULL, &%s__response_id};\n"
          "static const cmeta_type_desc %s__response_ptr_type = {\n"
          "    \"%s_t *\", sizeof(%s_t *), _Alignof(%s_t *),\n"
          "    CMETA_T_POINTER, &%s__response_type, NULL, NULL};\n"
          "CMETA_FUNCTION_METADATA_AS_ABI_RESULT(\n"
          "    %s, \"%s\", unknown, &cmeta_type_int, "
          "CMETA_ABI_SCALAR,\n"
          "    CMETA_RESULT_VALUE,\n"
          "    (const %s_t *, request,\n"
          "     CMETA_PARAM_IN | CMETA_PARAM_BORROWED,\n"
          "     &%s__request_ptr_type, CMETA_ABI_OBJECT_POINTER),\n"
          "    (%s_t *, response,\n"
          "     CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,\n"
          "     &%s__response_ptr_type, CMETA_ABI_OBJECT_POINTER));\n"
          "%s",
          operation->symbol, operation->request_type_identity,
          operation->symbol, operation->request_type,
          operation->request_type, operation->request_type,
          operation->symbol,
          operation->symbol, operation->request_type,
          operation->request_type, operation->request_type,
          operation->symbol,
          operation->symbol, operation->response_type_identity,
          operation->symbol, operation->response_type,
          operation->response_type, operation->response_type,
          operation->symbol,
          operation->symbol, operation->response_type,
          operation->response_type, operation->response_type,
          operation->symbol,
          operation->symbol, operation->qualified_operation,
          operation->request_type, operation->symbol,
          operation->response_type, operation->symbol,
          "") < 0)
    return -1;

  if (emit_accessors &&
      fprintf(
          file,
          "const cmeta_function_desc *%s__databind_function(void) {\n"
          "  return &%s__function_meta;\n"
          "}\n"
          "const cmeta_function_abi_desc *%s__databind_function_abi(void) {\n"
          "  return &%s__function_abi_meta;\n"
          "}\n",
          operation->symbol, operation->symbol,
          operation->symbol, operation->symbol) < 0)
    return -1;

  return 0;
}

int databind_compiler_service_native_emit_execution(
    FILE *file,
    const databind_compiler_service_native_operation *operation,
    int emit_descriptor) {
  int status;

  if (file == NULL || operation == NULL ||
      operation->symbol == NULL ||
      operation->request_type == NULL ||
      operation->response_type == NULL)
    return -1;

  if (operation->error_count == 0u) {
    status = fprintf(
        file,
        "static bool DATA_BIND_NATIVE_CALL %s__databind_invoke(\n"
        "    void *context, void *return_storage, void *const *params,\n"
        "    size_t param_count) {\n"
        "  int result;\n"
        "  (void)context;\n"
        "  if (return_storage == NULL || params == NULL ||\n"
        "      param_count != 2u || params[0] == NULL || params[1] == NULL)\n"
        "    return false;\n"
        "  result = %s((const %s_t *)params[0], (%s_t *)params[1]);\n"
        "  *(int *)return_storage = result;\n"
        "  return true;\n"
        "}\n",
        operation->symbol,
        operation->symbol,
        operation->request_type,
        operation->response_type);
  } else {
    if (operation->errors == NULL) return -1;
    status = fprintf(
        file,
        "static bool DATA_BIND_NATIVE_CALL %s__databind_invoke(\n"
        "    void *context, void *return_storage, void *const *params,\n"
        "    size_t param_count) {\n"
        "  int result;\n"
        "  (void)context;\n"
        "  if (return_storage == NULL || params == NULL ||\n"
        "      param_count != 3u || params[0] == NULL ||\n"
        "      params[1] == NULL || params[2] == NULL)\n"
        "    return false;\n"
        "  result = %s((const %s_t *)params[0], (%s_t *)params[1],\n"
        "              (%s__error *)params[2]);\n"
        "  *(int *)return_storage = result;\n"
        "  return true;\n"
        "}\n",
        operation->symbol,
        operation->symbol,
        operation->request_type,
        operation->response_type,
        operation->symbol);
  }
  if (status < 0) return -1;
  if (!emit_descriptor) return fputc('\n', file) == EOF ? -1 : 0;

  return fprintf(
             file,
             "static const DataBindNativeExecution %s__execution_meta = {\n"
             "  sizeof(DataBindNativeExecution),\n"
             "  DATA_BIND_NATIVE_EXECUTION_ABI_VERSION,\n"
             "  &%s__function_meta,\n"
             "  &%s__function_abi_meta,\n"
             "  NULL,\n"
             "  %s__databind_invoke\n"
             "};\n"
             "const DataBindNativeExecution *%s__databind_execution(void) {\n"
             "  return &%s__execution_meta;\n"
             "}\n\n",
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol) < 0
             ? -1
             : 0;
}

int databind_compiler_service_native_emit_cflow_projection(
    FILE *file,
    const databind_compiler_service_native_operation *operation) {
  if (file == NULL || operation == NULL ||
      operation->symbol == NULL ||
      operation->request_type == NULL ||
      operation->response_type == NULL)
    return -1;

  if (operation->error_count != 0u ||
      operation->request_presence_count != 0u ||
      operation->request_null_count != 0u ||
      operation->response_presence_count != 0u ||
      operation->response_null_count != 0u) {
    return fprintf(
               file,
               "cflow_function_projection_status %s__databind_cflow_projection(\n"
               "    cflow_function_typed_adapter_projection *out) {\n"
               "  if (out == NULL)\n"
               "    return CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT;\n"
               "  *out = (cflow_function_typed_adapter_projection){0};\n"
               "  return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE;\n"
               "}\n\n",
               operation->symbol) < 0
               ? -1
               : 0;
  }

  return fprintf(
             file,
             "static bool %s__databind_cflow_invoke(\n"
             "    const cmeta_callable *self, void *out,\n"
             "    const void *const *args) {\n"
             "  const cmeta_data_desc *response_data = NULL;\n"
             "  int native_status = -1;\n"
             "  void *params[2];\n"
             "  if (self == NULL || out == NULL || args == NULL ||\n"
             "      args[0] == NULL ||\n"
             "      self->capture_size != sizeof(response_data))\n"
             "    return false;\n"
             "  memcpy(&response_data, self->capture.bytes, sizeof(response_data));\n"
             "  if (response_data == NULL ||\n"
             "      cmeta_data_value_init_zero(response_data, out) != CMETA_OK)\n"
             "    return false;\n"
             "  params[0] = (void *)args[0];\n"
             "  params[1] = out;\n"
             "  if (!%s__execution_meta.invoke(\n"
             "          %s__execution_meta.context, &native_status, params, 2u) ||\n"
             "      native_status != 0) {\n"
             "    (void)cmeta_data_value_restore_zero(response_data, out);\n"
             "    return false;\n"
             "  }\n"
             "  return true;\n"
             "}\n"
             "cflow_function_projection_status %s__databind_cflow_projection(\n"
             "    cflow_function_typed_adapter_projection *out) {\n"
             "  DataBindNativeTypeBinding request = {0};\n"
             "  DataBindNativeTypeBinding response = {0};\n"
             "  DataBindServiceNativeBinding service = {0};\n"
             "  DataBindError error = DATA_BIND_ERROR_INIT;\n"
             "  cmeta_callable adapter = {0};\n"
             "  const cmeta_function_desc *function = &%s__function_meta;\n"
             "  const cmeta_data_desc *response_data;\n"
             "  DataBindStatus status;\n"
             "  if (out == NULL)\n"
             "    return CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT;\n"
             "  *out = (cflow_function_typed_adapter_projection){0};\n"
             "  if ((function->effects & CMETA_EFFECT_ASYNC) != 0u)\n"
             "    return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE;\n"
             "  if (!data_bind_native_execution_valid(&%s__execution_meta))\n"
             "    return CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;\n"
             "  status = %s__databind_native_binding(\n"
             "      &request, &response, &service, &error);\n"
             "  if (status != DATA_BIND_OK || service.function != function ||\n"
             "      request.data == NULL || response.data == NULL ||\n"
             "      request.data->storage_type == NULL ||\n"
             "      response.data->storage_type == NULL ||\n"
             "      !cmeta_data_value_traits_supported(request.data) ||\n"
             "      !cmeta_data_value_traits_supported(response.data) ||\n"
             "      cmeta_type_require_traits(\n"
             "          request.data->storage_type,\n"
             "          CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |\n"
             "              CMETA_TRAIT_DESTROY) != CMETA_OK ||\n"
             "      cmeta_type_require_traits(\n"
             "          response.data->storage_type,\n"
             "          CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |\n"
             "              CMETA_TRAIT_DESTROY) != CMETA_OK)\n"
             "    return CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH;\n"
             "  response_data = response.data;\n"
             "  adapter.meta.effects = function->effects;\n"
             "  adapter.meta.properties = function->properties;\n"
             "  adapter.invoke = %s__databind_cflow_invoke;\n"
             "  adapter.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;\n"
             "  adapter.capture_size = sizeof(response_data);\n"
             "  memcpy(adapter.capture.bytes, &response_data, sizeof(response_data));\n"
             "  return cflow_function_typed_adapter_projection_admit(\n"
             "      function, &%s__function_abi_meta, adapter,\n"
             "      request.data->storage_type, response.data->storage_type, out);\n"
             "}\n\n",
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol,
             operation->symbol) < 0
             ? -1
             : 0;
}

int databind_compiler_service_native_emit_binding(
    FILE *file,
    const databind_compiler_service_native_operation *operation) {
  databind_binding_compiler_service binding = {0};
  databind_binding_compiler_error *errors = NULL;
  size_t i;
  int result;

  if (file == NULL || operation == NULL ||
      operation->symbol == NULL ||
      operation->schema_name == NULL ||
      operation->request_type == NULL ||
      operation->response_type == NULL)
    return -1;

  binding.symbol = operation->symbol;
  binding.request.schema_name = operation->schema_name;
  binding.request.type_name = operation->request_type;
  binding.request.type_identity = operation->request_type_identity;
  binding.request.presence =
      (databind_compiler_message_native_state *)operation->request_presence;
  binding.request.presence_count = operation->request_presence_count;
  binding.request.nulls =
      (databind_compiler_message_native_state *)operation->request_nulls;
  binding.request.null_count = operation->request_null_count;

  binding.response.schema_name = operation->schema_name;
  binding.response.type_name = operation->response_type;
  binding.response.type_identity = operation->response_type_identity;
  binding.response.presence =
      (databind_compiler_message_native_state *)operation->response_presence;
  binding.response.presence_count = operation->response_presence_count;
  binding.response.nulls =
      (databind_compiler_message_native_state *)operation->response_nulls;
  binding.response.null_count = operation->response_null_count;

  if (operation->error_count != 0u) {
    errors = (databind_binding_compiler_error *)calloc(
        operation->error_count, sizeof(*errors));
    if (errors == NULL) return -1;
    for (i = 0u; i < operation->error_count; ++i) {
      errors[i].type_name = operation->errors[i].type_name;
      errors[i].kind_value = operation->errors[i].kind_value;
    }
  }
  binding.errors = errors;
  binding.error_count = operation->error_count;

  result = databind_binding_compiler_emit_service(file, &binding);
  free(errors);
  return result;
}

