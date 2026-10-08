#include "native_source_ir.h"

#include <stdint.h>
#include <stdio.h>
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


/* Deliberately private: the public C/codec entry still uses Binary admission.
 * This writes only the lossless fixed-width scalar subset accepted by build(). */
static int native_c_type(const char *name) {
  size_t i;
  if (name == NULL) return 0;
  for (i = 0u; i < sizeof(NATIVE_SCALARS)/sizeof(NATIVE_SCALARS[0]); ++i)
    if (strcmp(name, NATIVE_SCALARS[i].c) == 0) return 1;
  return 0;
}

static int native_identifier(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  if (p == NULL || !((*p >= 'A' && *p <= 'Z') ||
                     (*p >= 'a' && *p <= 'z') || *p == '_'))
    return 0;
  for (++p; *p; ++p)
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') || *p == '_'))
      return 0;
  return 1;
}

int databind_native_source_ir_write_header(
    const databind_native_source_ir *ir, const char *path) {
  FILE *out;
  size_t i, j;
  int failed = 0;
  if (ir == NULL || path == NULL || path[0] == '\0' ||
      (ir->record_count != 0u && ir->records == NULL))
    return -1;
  for (i = 0u; i < ir->record_count; ++i) {
    const databind_native_source_record *record = &ir->records[i];
    if (!native_identifier(record->name) ||
        (record->field_count != 0u && record->fields == NULL))
      return -1;
    for (j = 0u; j < record->field_count; ++j)
      if (!native_identifier(record->fields[j].name) ||
          !native_c_type(record->fields[j].c_type))
        return -1; /* Renderer only accepts canonical lowered C types. */
  }
  out = fopen(path, "wb");
  if (out == NULL) return -1;
  if (fprintf(out, "#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n\n") < 0)
    failed = 1;
  for (i = 0u; i < ir->record_count && !failed; ++i) {
    const databind_native_source_record *record = &ir->records[i];
    if (fprintf(out, "typedef struct %s {\n", record->name) < 0)
      failed = 1;
    for (j = 0u; j < record->field_count && !failed; ++j)
      if (fprintf(out, "    %s %s;\n", record->fields[j].c_type,
                  record->fields[j].name) < 0)
        failed = 1;
    if (!failed && fprintf(out, "} %s;\n\n", record->name) < 0)
      failed = 1;
  }
  if (ferror(out)) failed = 1;
  if (fclose(out) != 0) failed = 1;
  if (failed) {
    (void)remove(path);
    return -1;
  }
  return 0;
}
