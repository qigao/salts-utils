#include "native_source_ir.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct native_scalar_map {
  const char *idl;
  const char *c;
} native_scalar_map;

static const native_scalar_map NATIVE_SCALARS[] = {
    {"uint8", "uint8_t"}, {"uint16", "uint16_t"},
    {"uint32", "uint32_t"}, {"uint64", "uint64_t"},
    {"int8", "int8_t"}, {"int16", "int16_t"},
    {"int32", "int32_t"}, {"int64", "int64_t"},
    {"bool", "bool"},
};

static const char *native_scalar(const char *type) {
  size_t i;
  if (type == NULL) return NULL;
  for (i = 0u; i < sizeof(NATIVE_SCALARS)/sizeof(NATIVE_SCALARS[0]); ++i)
    if (strcmp(type, NATIVE_SCALARS[i].idl) == 0)
      return NATIVE_SCALARS[i].c;
  return NULL;
}

void databind_native_source_ir_destroy(databind_native_source_ir *ir) {
  size_t i;
  if (ir == NULL) return;
  for (i = 0u; i < ir->record_count; ++i)
    free(ir->records[i].fields);
  free(ir->records);
  *ir = (databind_native_source_ir){0};
}

int databind_native_source_ir_build(
    const IdlContract *contract, databind_native_source_ir *out) {
  size_t i, j, count = 0u, index = 0u;
  databind_native_source_ir plan = {0};
  if (contract == NULL || out == NULL || out->records != NULL ||
      out->record_count != 0u)
    return -1;
  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *decl = &contract->data[i];
    if (decl->kind != IDL_DATA_MESSAGE && decl->kind != IDL_DATA_COMPOSITE)
      return -1; /* Other declaration families are not admitted yet. */
    ++count;
  }
  if (count > SIZE_MAX / sizeof(*plan.records)) return -1;
  plan.records = (databind_native_source_record *)calloc(
      count != 0u ? count : 1u, sizeof(*plan.records));
  if (plan.records == NULL) return -1;
  plan.record_count = count;
  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *decl = &contract->data[i];
    databind_native_source_record *record = &plan.records[index++];
    if (decl->name == NULL || decl->name[0] == '\0' ||
        decl->field_count > SIZE_MAX / sizeof(*record->fields))
      goto fail;
    record->name = decl->name;
    record->field_count = decl->field_count;
    record->fields = (databind_native_source_field *)calloc(
        decl->field_count != 0u ? decl->field_count : 1u,
        sizeof(*record->fields));
    if (record->fields == NULL) goto fail;
    for (j = 0u; j < decl->field_count; ++j) {
      const IdlField *field = &decl->fields[j];
      const char *type = native_scalar(field->type_name);
      if (field->name == NULL || field->name[0] == '\0' ||
          field->optional || field->nullable ||
          field->collection_kind != IDL_COLLECTION_NONE ||
          field->default_value != NULL || type == NULL)
        goto fail;
      record->fields[j].name = field->name;
      record->fields[j].c_type = type;
    }
  }
  *out = plan;
  return 0;
fail:
  databind_native_source_ir_destroy(&plan);
  return -1;
}
