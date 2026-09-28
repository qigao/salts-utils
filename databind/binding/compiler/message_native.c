#include "message_native.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *message_native_strdup(const char *text) {
  size_t length;
  char *copy;
  if (text == NULL) return NULL;
  length = strlen(text);
  if (length == SIZE_MAX) return NULL;
  copy = (char *)malloc(length + 1u);
  if (copy != NULL) memcpy(copy, text, length + 1u);
  return copy;
}

static void message_native_state_destroy(
    databind_compiler_message_native_state *state, size_t count) {
  size_t i;
  if (state == NULL) return;
  for (i = 0u; i < count; ++i) free(state[i].field_name);
  free(state);
}

static int message_native_state_build(
    const IdlDataDecl *message, int nullable,
    databind_compiler_message_native_state **out_state,
    size_t *out_count) {
  databind_compiler_message_native_state *state = NULL;
  size_t count = 0u;
  size_t i;
  size_t index = 0u;

  if (message == NULL || out_state == NULL || out_count == NULL)
    return 0;
  *out_state = NULL;
  *out_count = 0u;

  for (i = 0u; i < message->field_count; ++i) {
    const IdlField *field = &message->fields[i];
    if (nullable ? field->nullable : field->optional) ++count;
  }

  if (count == 0u) return 1;
  state = (databind_compiler_message_native_state *)calloc(
      count, sizeof(*state));
  if (state == NULL) return 0;

  for (i = 0u; i < message->field_count; ++i) {
    const IdlField *field = &message->fields[i];
    if (!(nullable ? field->nullable : field->optional)) continue;
    state[index].field_name = message_native_strdup(field->name);
    state[index].bit = (unsigned)index;
    if (state[index].field_name == NULL) {
      message_native_state_destroy(state, count);
      return 0;
    }
    ++index;
  }

  *out_state = state;
  *out_count = count;
  return 1;
}

static char *message_native_type_identity(
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

void databind_compiler_message_native_destroy(
    databind_compiler_message_native_binding *binding) {
  if (binding == NULL) return;
  free(binding->schema_name);
  free(binding->type_name);
  free(binding->type_identity);
  message_native_state_destroy(binding->presence, binding->presence_count);
  message_native_state_destroy(binding->nulls, binding->null_count);
  memset(binding, 0, sizeof(*binding));
}

int databind_compiler_message_native_build(
    const IdlContract *contract,
    const char *type_name,
    databind_compiler_message_native_binding *out) {
  const IdlDataDecl *message;
  const char *schema_name;

  if (contract == NULL || type_name == NULL || type_name[0] == '\0' ||
      out == NULL)
    return -1;
  memset(out, 0, sizeof(*out));

  schema_name =
      contract->name != NULL && contract->name[0] != '\0'
          ? contract->name
          : "GeneratedSchema";

  message = idl_contract_find_data(contract, type_name);
  if (message == NULL || message->kind != IDL_DATA_MESSAGE)
    return -1;

  out->schema_name = message_native_strdup(schema_name);
  out->type_name = message_native_strdup(type_name);
  out->type_identity = message_native_type_identity(schema_name, type_name);

  if (out->schema_name == NULL || out->type_name == NULL ||
      out->type_identity == NULL ||
      !message_native_state_build(
          message, 0, &out->presence, &out->presence_count) ||
      !message_native_state_build(
          message, 1, &out->nulls, &out->null_count)) {
    databind_compiler_message_native_destroy(out);
    return -1;
  }

  return 0;
}

static int message_native_emit_state_array(
    FILE *file,
    const char *symbol,
    const char *type_name,
    const char *suffix,
    const char *member_name,
    const databind_compiler_message_native_state *state,
    size_t count) {
  size_t i;

  if (count == 0u) return 0;
  if (file == NULL || symbol == NULL || type_name == NULL ||
      suffix == NULL || member_name == NULL || state == NULL)
    return -1;

  if (fprintf(
          file,
          "static const DataBindNativeStateBinding %s__%s[] = {\n",
          symbol, suffix) < 0)
    return -1;

  for (i = 0u; i < count; ++i) {
    if (state[i].field_name == NULL ||
        fprintf(
            file,
            "  {sizeof(DataBindNativeStateBinding), \"%s\", "
            "offsetof(%s_t, %s), %uu},\n",
            state[i].field_name, type_name, member_name, state[i].bit) < 0)
      return -1;
  }

  return fputs("};\n", file) == EOF ? -1 : 0;
}

int databind_compiler_message_native_emit_binding(
    FILE *file,
    const databind_compiler_message_native_binding *binding,
    const char *symbol_prefix) {
  const char *presence_expr = "NULL";
  const char *null_expr = "NULL";
  char presence[640];
  char nulls[640];

  if (file == NULL || binding == NULL || symbol_prefix == NULL ||
      symbol_prefix[0] == '\0' || binding->type_name == NULL)
    return -1;

  if (message_native_emit_state_array(
          file, symbol_prefix, binding->type_name,
          "presence", "_presence",
          binding->presence, binding->presence_count) != 0 ||
      message_native_emit_state_array(
          file, symbol_prefix, binding->type_name,
          "nulls", "_nulls",
          binding->nulls, binding->null_count) != 0)
    return -1;

  if (binding->presence_count != 0u) {
    if (snprintf(
            presence, sizeof(presence), "%s__presence",
            symbol_prefix) <= 0)
      return -1;
    presence_expr = presence;
  }

  if (binding->null_count != 0u) {
    if (snprintf(
            nulls, sizeof(nulls), "%s__nulls",
            symbol_prefix) <= 0)
      return -1;
    null_expr = nulls;
  }

  return fprintf(
             file,
             "static DataBindStatus %s__databind_message_native_binding(\n"
             "    DataBindNativeTypeBinding *out, DataBindError *error) {\n"
             "  const cmeta_data_desc *data = NULL;\n"
             "  DataBindStatus status;\n"
             "  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;\n"
             "  status = %s_cmeta_data(&data, error);\n"
             "  if (status != DATA_BIND_OK) return status;\n"
             "  DataBindNativeTypeBinding value =\n"
             "      DATA_BIND_NATIVE_TYPE_BINDING_INIT(\"%s\", data);\n"
             "  value.presence = %s;\n"
             "  value.presence_count = %zuu;\n"
             "  value.nulls = %s;\n"
             "  value.null_count = %zuu;\n"
             "  *out = value;\n"
             "  return DATA_BIND_OK;\n"
             "}\n",
             symbol_prefix,
             binding->type_name,
             binding->type_name,
             presence_expr, binding->presence_count,
             null_expr, binding->null_count) < 0
             ? -1
             : 0;
}
