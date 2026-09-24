#include "service_native.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const Node *native_child(const Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP || name == NULL) return NULL;
  for (i = 0u; i < parent->data.map.count; ++i) {
    const Node *child = parent->data.map.items[i];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static const Node *native_list(const Node *parent, const char *name) {
  const Node *child = native_child(parent, name);
  return child != NULL && child->type == NODE_LIST ? child : NULL;
}

static const char *native_string(const Node *parent, const char *name) {
  const Node *child = native_child(parent, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

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

static const Node *native_message(
    const Node *root, const char *name) {
  const Node *messages = native_list(root, "messages");
  size_t i;
  if (messages == NULL || name == NULL) return NULL;
  for (i = 0u; i < messages->data.list.count; ++i) {
    const Node *message = messages->data.list.items[i];
    const char *candidate = native_string(message, "name");
    if (candidate != NULL && strcmp(candidate, name) == 0) return message;
  }
  return NULL;
}

static int native_unsigned(const char *text, unsigned *out) {
  uint64_t value = 0u;
  const unsigned char *p;
  if (text == NULL || text[0] == '\0' || out == NULL) return 0;
  for (p = (const unsigned char *)text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return 0;
    value = value * 10u + (uint64_t)(*p - '0');
    if (value > UINT32_MAX) return 0;
  }
  *out = (unsigned)value;
  return 1;
}

static void native_presence_clear(
    databind_compiler_service_native_presence *presence,
    size_t count) {
  size_t i;
  if (presence == NULL) return;
  for (i = 0u; i < count; ++i) free(presence[i].field_name);
  free(presence);
}

static int native_presence_build(
    const Node *message,
    databind_compiler_service_native_presence **out_presence,
    size_t *out_count) {
  const Node *fields;
  databind_compiler_service_native_presence *presence = NULL;
  size_t count = 0u;
  size_t i;
  size_t index = 0u;

  if (out_presence == NULL || out_count == NULL || message == NULL)
    return 0;
  *out_presence = NULL;
  *out_count = 0u;

  fields = native_list(message, "fields");
  if (fields == NULL) return 1;

  for (i = 0u; i < fields->data.list.count; ++i)
    if (native_child(fields->data.list.items[i], "is_optional") != NULL)
      ++count;

  if (count == 0u) return 1;
  presence = (databind_compiler_service_native_presence *)calloc(
      count, sizeof(*presence));
  if (presence == NULL) return 0;

  for (i = 0u; i < fields->data.list.count; ++i) {
    const Node *field = fields->data.list.items[i];
    const char *field_name;
    const char *bit_text;
    unsigned bit;
    if (native_child(field, "is_optional") == NULL) continue;
    field_name = native_string(field, "name");
    bit_text = native_string(field, "optional_bit_index");
    if (field_name == NULL || !native_unsigned(bit_text, &bit)) {
      native_presence_clear(presence, count);
      return 0;
    }
    presence[index].field_name = native_strdup(field_name);
    presence[index].bit = bit;
    if (presence[index].field_name == NULL) {
      native_presence_clear(presence, count);
      return 0;
    }
    ++index;
  }

  *out_presence = presence;
  *out_count = count;
  return 1;
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

static void native_errors_clear(
    databind_compiler_service_native_error *errors,
    size_t count) {
  size_t i;
  if (errors == NULL) return;
  for (i = 0u; i < count; ++i) {
    free(errors[i].type_name);
    free(errors[i].type_identity);
  }
  free(errors);
}

static int native_error_message_trivially_owned(
    const Node *root, const char *type_name) {
  const Node *message = native_message(root, type_name);
  const Node *fields;
  size_t i;

  if (message == NULL ||
      native_child(message, "cmeta_graph_supported") == NULL)
    return 0;

  fields = native_list(message, "fields");
  if (fields == NULL) return 0;

  for (i = 0u; i < fields->data.list.count; ++i) {
    const char *requirement =
        native_string(fields->data.list.items[i],
                      "cmeta_native_requirement");
    if (requirement == NULL ||
        (strcmp(requirement, "fixed_value") != 0 &&
         strcmp(requirement, "enum_domain") != 0))
      return 0;
  }
  return 1;
}

static int native_errors_build(
    const Node *root,
    const char *schema_name,
    const Node *operation_node,
    databind_compiler_service_native_error **out_errors,
    size_t *out_count) {
  const Node *errors;
  databind_compiler_service_native_error *result = NULL;
  size_t i;

  if (out_errors == NULL || out_count == NULL ||
      root == NULL || schema_name == NULL || operation_node == NULL)
    return 0;

  *out_errors = NULL;
  *out_count = 0u;
  errors = native_list(operation_node, "errors");
  if (errors == NULL || errors->data.list.count == 0u)
    return 1;

  result = (databind_compiler_service_native_error *)calloc(
      errors->data.list.count, sizeof(*result));
  if (result == NULL) return 0;

  for (i = 0u; i < errors->data.list.count; ++i) {
    const Node *item = errors->data.list.items[i];
    const char *type_name =
        item != NULL && item->type == NODE_STRING
            ? item->data.string_val
            : NULL;

    if (type_name == NULL ||
        !native_error_message_trivially_owned(root, type_name)) {
      native_errors_clear(result, errors->data.list.count);
      return 0;
    }

    result[i].type_name = native_strdup(type_name);
    result[i].type_identity =
        native_type_identity(schema_name, type_name);
    result[i].kind_value = (unsigned)(i + 1u);

    if (result[i].type_name == NULL ||
        result[i].type_identity == NULL) {
      native_errors_clear(result, errors->data.list.count);
      return 0;
    }
  }

  *out_errors = result;
  *out_count = errors->data.list.count;
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
  native_presence_clear(
      operation->request_presence, operation->request_presence_count);
  native_presence_clear(
      operation->response_presence, operation->response_presence_count);
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
    const Node *root,
    const char *schema_name,
    const char *service_name,
    const Node *operation_node,
    databind_compiler_service_native_operation *out) {
  const char *operation_name = native_string(operation_node, "name");
  const char *request_type = native_string(operation_node, "request_type");
  const char *response_type = native_string(operation_node, "response_type");
  const Node *request_message;
  const Node *response_message;

  if (out == NULL || operation_name == NULL ||
      request_type == NULL || response_type == NULL)
    return 0;

  request_message = native_message(root, request_type);
  response_message = native_message(root, response_type);
  if (request_message == NULL || response_message == NULL ||
      native_child(request_message, "cmeta_graph_supported") == NULL ||
      native_child(response_message, "cmeta_graph_supported") == NULL)
    return 0;

  out->schema_name = native_strdup(schema_name);
  out->service_name = native_strdup(service_name);
  out->operation_name = native_strdup(operation_name);
  out->qualified_service =
      native_join2(schema_name, service_name, '.');
  out->qualified_operation =
      native_join3(schema_name, service_name, operation_name, '.');
  out->symbol = native_symbol(schema_name, service_name, operation_name);
  out->request_type = native_strdup(request_type);
  out->response_type = native_strdup(response_type);
  out->request_type_identity =
      native_type_identity(schema_name, request_type);
  out->response_type_identity =
      native_type_identity(schema_name, response_type);

  if (!native_presence_build(
          request_message,
          &out->request_presence,
          &out->request_presence_count) ||
      !native_presence_build(
          response_message,
          &out->response_presence,
          &out->response_presence_count) ||
      !native_errors_build(
          root, schema_name, operation_node,
          &out->errors, &out->error_count))
    return 0;

  return out->schema_name != NULL &&
         out->service_name != NULL &&
         out->operation_name != NULL &&
         out->qualified_service != NULL &&
         out->qualified_operation != NULL &&
         out->symbol != NULL &&
         out->request_type != NULL &&
         out->response_type != NULL &&
         out->request_type_identity != NULL &&
         out->response_type_identity != NULL;
}

int databind_compiler_service_native_build_selected(
    const Node *canonical_ir,
    databind_compiler_service_native_select_fn select_service,
    void *select_context,
    databind_compiler_service_native_ir *out) {
  const Node *schema;
  const Node *services;
  const char *schema_name;
  size_t total = 0u;
  size_t i, j, index = 0u;

  if (out == NULL) return -1;
  memset(out, 0, sizeof(*out));
  if (canonical_ir == NULL) return -1;

  schema = native_child(canonical_ir, "schema");
  schema_name = native_string(schema, "schema_name");
  if (schema_name == NULL || schema_name[0] == '\0')
    schema_name = "GeneratedSchema";

  services = native_list(canonical_ir, "services");
  if (services == NULL) return -1;

  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *service = services->data.list.items[i];
    const char *service_name = native_string(service, "name");
    const Node *operations;

    if (service_name == NULL || service_name[0] == '\0') return -1;
    if (select_service != NULL &&
        !select_service(select_context, service_name))
      continue;

    operations = native_list(service, "operations");
    if (operations == NULL) return -1;
    if (total > SIZE_MAX - operations->data.list.count) return -1;
    total += operations->data.list.count;
  }
  if (total == 0u) return -1;

  out->operations = (databind_compiler_service_native_operation *)calloc(
      total, sizeof(*out->operations));
  if (out->operations == NULL) return -1;
  out->operation_count = total;

  for (i = 0u; i < services->data.list.count; ++i) {
    const Node *service = services->data.list.items[i];
    const char *service_name = native_string(service, "name");
    const Node *operations;

    if (service_name == NULL || service_name[0] == '\0') goto fail;
    if (select_service != NULL &&
        !select_service(select_context, service_name))
      continue;

    operations = native_list(service, "operations");
    if (operations == NULL) goto fail;

    for (j = 0u; j < operations->data.list.count; ++j, ++index) {
      size_t prior;
      if (!native_operation_fill(
              canonical_ir, schema_name, service_name,
              operations->data.list.items[j], &out->operations[index]))
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
    const Node *canonical_ir,
    databind_compiler_service_native_ir *out) {
  return databind_compiler_service_native_build_selected(
      canonical_ir, NULL, NULL, out);
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

  return fprintf(
             file,
             "  } payload;\n"
             "} %s__error;\n"
             "int %s(const %s_t *request, %s_t *response, "
             "%s__error *error);\n",
             operation->symbol,
             operation->symbol,
             operation->request_type,
             operation->response_type,
             operation->symbol) < 0
             ? -1
             : 0;
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
            "CMETA_FUNCTION_METADATA_AS_ABI(\n"
            "    %s, \"%s\", fallible, &cmeta_type_int, "
            "CMETA_ABI_SCALAR,\n"
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
          "CMETA_FUNCTION_METADATA_AS_ABI(\n"
          "    %s, \"%s\", fallible, &cmeta_type_int, "
          "CMETA_ABI_SCALAR,\n"
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

static int native_emit_presence_array(
    FILE *file,
    const char *symbol,
    const char *type_name,
    const char *suffix,
    const databind_compiler_service_native_presence *presence,
    size_t count) {
  size_t i;
  if (count == 0u) return 0;
  if (file == NULL || symbol == NULL || type_name == NULL ||
      suffix == NULL || presence == NULL)
    return -1;

  if (fprintf(
          file,
          "static const DataBindNativePresenceBinding "
          "%s__%s_presence[] = {\n",
          symbol, suffix) < 0)
    return -1;

  for (i = 0u; i < count; ++i) {
    if (presence[i].field_name == NULL ||
        fprintf(
            file,
            "  {sizeof(DataBindNativePresenceBinding), \"%s\", "
            "offsetof(%s_t, _presence), %uu},\n",
            presence[i].field_name, type_name, presence[i].bit) < 0)
      return -1;
  }

  return fputs("};\n", file) == EOF ? -1 : 0;
}

int databind_compiler_service_native_emit_binding(
    FILE *file,
    const databind_compiler_service_native_operation *operation) {
  const char *request_presence_expr;
  const char *response_presence_expr;
  char request_presence[640];
  char response_presence[640];

  if (file == NULL || operation == NULL ||
      operation->symbol == NULL ||
      operation->request_type == NULL ||
      operation->response_type == NULL)
    return -1;
  if (operation->error_count != 0u) return -1;

  if (native_emit_presence_array(
          file, operation->symbol, operation->request_type, "request",
          operation->request_presence,
          operation->request_presence_count) != 0 ||
      native_emit_presence_array(
          file, operation->symbol, operation->response_type, "response",
          operation->response_presence,
          operation->response_presence_count) != 0)
    return -1;

  if (operation->request_presence_count != 0u) {
    if (snprintf(
            request_presence, sizeof(request_presence),
            "%s__request_presence", operation->symbol) <= 0)
      return -1;
    request_presence_expr = request_presence;
  } else {
    request_presence_expr = "NULL";
  }

  if (operation->response_presence_count != 0u) {
    if (snprintf(
            response_presence, sizeof(response_presence),
            "%s__response_presence", operation->symbol) <= 0)
      return -1;
    response_presence_expr = response_presence;
  } else {
    response_presence_expr = "NULL";
  }

  return fprintf(
             file,
             "DataBindStatus %s__databind_native_binding(\n"
             "    DataBindNativeTypeBinding *request_out,\n"
             "    DataBindNativeTypeBinding *response_out,\n"
             "    DataBindServiceNativeBinding *service_out,\n"
             "    DataBindError *error) {\n"
             "  const cmeta_data_desc *request_data = NULL;\n"
             "  const cmeta_data_desc *response_data = NULL;\n"
             "  DataBindStatus status;\n"
             "  if (request_out == NULL || response_out == NULL || "
             "service_out == NULL)\n"
             "    return DATA_BIND_ERR_INVALID_ARG;\n"
             "  status = %s_cmeta_data(&request_data, error);\n"
             "  if (status != DATA_BIND_OK) return status;\n"
             "  status = %s_cmeta_data(&response_data, error);\n"
             "  if (status != DATA_BIND_OK) return status;\n"
             "  *request_out = (DataBindNativeTypeBinding){\n"
             "      sizeof(DataBindNativeTypeBinding),\n"
             "      DATA_BIND_BINDING_PLAN_ABI_VERSION,\n"
             "      \"%s\", request_data, %s, %zuu};\n"
             "  *response_out = (DataBindNativeTypeBinding){\n"
             "      sizeof(DataBindNativeTypeBinding),\n"
             "      DATA_BIND_BINDING_PLAN_ABI_VERSION,\n"
             "      \"%s\", response_data, %s, %zuu};\n"
             "  *service_out = (DataBindServiceNativeBinding){\n"
             "      sizeof(DataBindServiceNativeBinding),\n"
             "      DATA_BIND_BINDING_PLAN_ABI_VERSION,\n"
             "      &%s__function_meta, request_out, response_out};\n"
             "  return DATA_BIND_OK;\n"
             "}\n",
             operation->symbol,
             operation->request_type,
             operation->response_type,
             operation->request_type,
             request_presence_expr,
             operation->request_presence_count,
             operation->response_type,
             response_presence_expr,
             operation->response_presence_count,
             operation->symbol) < 0
             ? -1
             : 0;
}
