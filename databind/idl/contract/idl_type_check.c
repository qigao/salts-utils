#include "idl_contract_internal.h"

#include <stdio.h>
#include <string.h>

/* IDL vocabulary is logical, not a CMeta native-storage whitelist.
 * varint, for example, is a legal logical scalar even where a selected
 * Binary/native projection cannot represent it. Those backends fail closed
 * during their own admission, never during source-language type checking. */
int idl_logical_builtin_type(const char *name, size_t length) {
  static const char *const names[] = {
      "bool",
      "int8_t", "int8", "i8", "int16_t", "int16", "i16",
      "int32_t", "int32", "i32", "int64_t", "int64", "i64",
      "uint8_t", "uint8", "u8", "byte",
      "uint16_t", "uint16", "u16",
      "uint32_t", "uint32", "u32",
      "uint64_t", "uint64", "u64",
      "float", "f32", "double", "f64",
      "string", "bytes", "uuid", "datetime", "date", "time",
      "duration", "decimal", "bigint", "money", "varint",
  };
  size_t i;
  if (name == NULL || length == 0u) return 0;
  for (i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
    if (strlen(names[i]) == length &&
        memcmp(names[i], name, length) == 0)
      return 1;
  }
  return 0;
}

static const IdlDataDecl *idl_find_named_data(
    const IdlContract *contract, const char *name, size_t length) {
  size_t i;
  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *decl = &contract->data[i];
    if (decl->name != NULL && strlen(decl->name) == length &&
        memcmp(decl->name, name, length) == 0)
      return decl;
  }
  return NULL;
}

static int idl_type_error(
    IdlDiagnostic *diagnostic, const char *owner, const char *field,
    const char *reason, const char *type, size_t length) {
  char message[256];
  const int shown = (int)(length < 64u ? length : 64u);
  snprintf(message, sizeof(message), "Data '%s' field '%s': %s '%.*s'",
           owner != NULL ? owner : "<unnamed>",
           field != NULL ? field : "<unnamed>",
           reason, shown, type != NULL ? type : "");
  idl_contract_diagnostic_set(
      diagnostic, IDL_SEMANTIC_ERROR, -1, -1, message);
  return 0;
}

/* idl_type_ref_parse checks whole-input grammar, constructor arity, and global
 * depth/node/byte limits. Recurse only over its already-admitted argument
 * spans; never infer the logical type from a native CMeta descriptor. */
static int idl_check_type_expression(
    const IdlContract *contract, const char *expression, size_t length,
    const char *owner, const char *field, IdlDiagnostic *diagnostic) {
  IdlTypeRef parsed;
  size_t i;
  if (expression == NULL ||
      !idl_type_ref_parse(expression, length, &parsed))
    return idl_type_error(diagnostic, owner, field,
                          "malformed logical type", expression, length);
  if (parsed.collection_kind == IDL_COLLECTION_NONE) {
    if (idl_logical_builtin_type(parsed.name, parsed.name_length) ||
        idl_find_named_data(contract, parsed.name, parsed.name_length) != NULL)
      return 1;
    return idl_type_error(diagnostic, owner, field,
                          "unknown logical type", parsed.name,
                          parsed.name_length);
  }
  for (i = 0u; i < parsed.argument_count; ++i) {
    if (!idl_check_type_expression(
            contract, parsed.arguments[i], parsed.argument_lengths[i],
            owner, field, diagnostic))
      return 0;
  }
  return 1;
}

static int idl_check_field(
    const IdlContract *contract, const IdlDataDecl *decl,
    const IdlField *field, IdlDiagnostic *diagnostic) {
  const char *inner = field->inner_type;
  const char *key = field->key_type;
  const char *value = field->value_type;

  switch (field->collection_kind) {
  case IDL_COLLECTION_NONE:
    return idl_check_type_expression(
        contract, field->type_name,
        field->type_name != NULL ? strlen(field->type_name) : 0u,
        decl->name, field->name, diagnostic);

  case IDL_COLLECTION_GROUP: {
    const IdlDataDecl *group =
        inner != NULL ? idl_find_named_data(contract, inner, strlen(inner)) : NULL;
    if (group == NULL || group->kind != IDL_DATA_GROUP)
      return idl_type_error(
          diagnostic, decl->name, field->name,
          "group element must name a declared group", inner,
          inner != NULL ? strlen(inner) : 0u);
    return 1;
  }

  case IDL_COLLECTION_ARRAY:
  case IDL_COLLECTION_LIST:
  case IDL_COLLECTION_SET:
    return idl_check_type_expression(
        contract, inner, inner != NULL ? strlen(inner) : 0u,
        decl->name, field->name, diagnostic);

  case IDL_COLLECTION_MAP:
    return idl_check_type_expression(
               contract, key, key != NULL ? strlen(key) : 0u,
               decl->name, field->name, diagnostic) &&
           idl_check_type_expression(
               contract, value, value != NULL ? strlen(value) : 0u,
               decl->name, field->name, diagnostic);

  default:
    return idl_type_error(
        diagnostic, decl->name, field->name,
        "unsupported logical collection kind",
        field->type_name,
        field->type_name != NULL ? strlen(field->type_name) : 0u);
  }
}

int idl_contract_validate_types(
    const IdlContract *contract, IdlDiagnostic *diagnostic) {
  size_t i, j;
  char message[256];
  if (contract == NULL) return 0;
  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *decl = &contract->data[i];

    /* A declared Data symbol cannot shadow the core scalar vocabulary. */
    if (decl->name != NULL &&
        idl_logical_builtin_type(decl->name, strlen(decl->name))) {
      snprintf(message, sizeof(message),
               "Data declaration '%s' shadows a logical builtin type",
               decl->name);
      idl_contract_diagnostic_set(
          diagnostic, IDL_SEMANTIC_ERROR, -1, -1, message);
      return 0;
    }

    for (j = 0u; j < decl->field_count; ++j) {
      if (!idl_check_field(contract, decl, &decl->fields[j], diagnostic))
        return 0;
    }
  }
  return 1;
}
