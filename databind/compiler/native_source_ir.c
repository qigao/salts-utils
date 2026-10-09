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
          field->collection_kind != IDL_COLLECTION_NONE ||
          field->default_value != NULL || type == NULL)
        goto fail;
      record->fields[j].name = field->name;
      record->fields[j].c_type = type;
      record->fields[j].optional = field->optional != 0;
      record->fields[j].nullable = field->nullable != 0;
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

static int native_field_collides(const databind_native_source_record *record,
                                 size_t index) {
  size_t i;
  const databind_native_source_field *field = &record->fields[index];
  for (i = 0u; i < record->field_count; ++i) {
    if (i != index && strcmp(field->name, record->fields[i].name) == 0)
      return 1;
    if (record->fields[i].optional || record->fields[i].nullable) {
      const char *prefixes[] = {"has_", "is_null_"};
      size_t k;
      for (k = 0u; k < 2u; ++k) {
        const int active = k == 0u ? record->fields[i].optional :
                                     record->fields[i].nullable;
        const size_t prefix_len = strlen(prefixes[k]);
        if (active && strlen(field->name) == prefix_len +
                      strlen(record->fields[i].name) &&
            strncmp(field->name, prefixes[k], prefix_len) == 0 &&
            strcmp(field->name + prefix_len, record->fields[i].name) == 0)
          return 1;
      }
    }
  }
  return 0;
}

static int native_reserved_identifier(const char *s) {
  static const char *const keywords[] = {
      "auto", "break", "case", "char", "const", "continue", "default",
      "do", "double", "else", "enum", "extern", "float", "for",
      "goto", "if", "inline", "int", "long", "register", "restrict",
      "return", "short", "signed", "sizeof", "static", "struct",
      "switch", "typedef", "union", "unsigned", "void", "volatile",
      "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex",
      "_Generic", "_Imaginary", "_Noreturn", "_Static_assert",
      "_Thread_local", "bool", "true", "false"
  };
  size_t i;
  if (s == NULL || s[0] == '_') return 1;
  for (i = 0u; i < sizeof(keywords)/sizeof(keywords[0]); ++i)
    if (strcmp(s, keywords[i]) == 0) return 1;
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
  return !native_reserved_identifier(s);
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
    /* Empty C structs are not portable C11 and emit invalid declarations. */
    if (!native_identifier(record->name) || record->field_count == 0u ||
        record->fields == NULL)
      return -1;
    /* A record typedef must not collide with another record's generated
     * lifecycle functions. _init and _clear have different suffix sizes. */
    for (j = 0u; j < i; ++j) {
      const char *previous = ir->records[j].name;
      const size_t previous_len = strlen(previous);
      const size_t current_len = strlen(record->name);
      static const char *const suffixes[] = {"_init", "_clear"};
      size_t k;
      if (strcmp(previous, record->name) == 0) return -1;
      for (k = 0u; k < sizeof(suffixes)/sizeof(suffixes[0]); ++k) {
        const size_t suffix_len = strlen(suffixes[k]);
        if ((current_len == previous_len + suffix_len &&
             strncmp(record->name, previous, previous_len) == 0 &&
             strcmp(record->name + previous_len, suffixes[k]) == 0) ||
            (previous_len == current_len + suffix_len &&
             strncmp(previous, record->name, current_len) == 0 &&
             strcmp(previous + current_len, suffixes[k]) == 0))
          return -1;
      }
    }
    for (j = 0u; j < record->field_count; ++j)
      if (!native_identifier(record->fields[j].name) ||
          !native_c_type(record->fields[j].c_type) ||
          native_field_collides(record, j))
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
    for (j = 0u; j < record->field_count && !failed; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      if (field->optional && fprintf(out, "    bool has_%s;\n", field->name) < 0)
        failed = 1;
      if (field->nullable && fprintf(out, "    bool is_null_%s;\n", field->name) < 0)
        failed = 1;
      if (!failed && fprintf(out, "    %s %s;\n", field->c_type,
                             field->name) < 0)
        failed = 1;
    }
    if (!failed && fprintf(out, "} %s;\n", record->name) < 0)
      failed = 1;
    if (!failed && fprintf(out,
        "static inline void %s_init(%s *value) {\n"
        "    if (value) *value = (%s){0};\n"
        "}\n"
        "static inline void %s_clear(%s *value) {\n"
        "    if (value) *value = (%s){0};\n"
        "}\n\n",
        record->name, record->name, record->name,
        record->name, record->name, record->name) < 0)
      failed = 1;
  }
  /* Reuse Salts CMeta descriptors without imposing a Salts dependency on
   * the standalone C11 source-only header unless reflection is requested. */
  if (!failed && fprintf(out,
      "#ifdef DATABIND_NATIVE_ENABLE_CMETA\n"
      "#include <cmeta_cmeta_data.h>\n") < 0)
    failed = 1;
  for (i = 0u; i < ir->record_count && !failed; ++i) {
    const databind_native_source_record *record = &ir->records[i];
    if (fprintf(out, "static const cmeta_field_desc %s_native_cmeta_fields[] = {\n",
                record->name) < 0)
      failed = 1;
    for (j = 0u; j < record->field_count && !failed; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      if (fprintf(out,
          "    {\"%s\", \"%s\", offsetof(%s, %s), "
          "sizeof(((%s *)0)->%s), _Alignof(%s), NULL, NULL},\n",
          field->name, field->name, record->name, field->name,
          record->name, field->name, field->c_type) < 0)
        failed = 1;
    }
    if (!failed && fprintf(out, "};\n") < 0)
      failed = 1;
    if (!failed && fprintf(out,
        "static const cmeta_struct_desc %s_native_cmeta_layout = {\n"
        "    \"%s\", sizeof(%s), _Alignof(%s),\n"
        "    %s_native_cmeta_fields, %zuu\n"
        "};\n",
        record->name, record->name, record->name, record->name,
        record->name, record->field_count) < 0)
      failed = 1;
    /* Bind DLL-imported scalar CMeta descriptors at call time, not in
     * file-scope initializers (which MSVC rejects). Caller owns the array. */
    if (!failed && fprintf(out,
        "static inline int %s_native_cmeta_data_fields(\n"
        "    cmeta_data_field_desc *out, size_t capacity) {\n"
        "    if (out == NULL || capacity < %zuu) return -1;\n",
        record->name, record->field_count) < 0)
      failed = 1;
    for (j = 0u; j < record->field_count && !failed; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      const char *scalar = NULL;
      size_t k;
      for (k = 0u; k < sizeof(NATIVE_SCALARS)/sizeof(NATIVE_SCALARS[0]); ++k)
        if (strcmp(field->c_type, NATIVE_SCALARS[k].c) == 0) {
          scalar = NATIVE_SCALARS[k].idl;
          break;
        }
      if (scalar == NULL || fprintf(out,
          "    out[%zuu] = (cmeta_data_field_desc){"
          "\"native.%s.%s\", \"%s\", offsetof(%s, %s), "
          "&cmeta_data_%s};\n",
          j, record->name, field->name, field->name,
          record->name, field->name, scalar) < 0)
        failed = 1;
    }
    if (!failed && fprintf(out, "    return 0;\n}\n") < 0)
      failed = 1;
    if (fprintf(out,
        "static const cmeta_type_desc %s_native_cmeta_type = {\n"
        "    .name = \"%s\",\n"
        "    .size = sizeof(%s),\n"
        "    .align = _Alignof(%s),\n"
        "    .kind = CMETA_T_OBJECT\n"
        "};\n",
        record->name, record->name, record->name, record->name) < 0)
      failed = 1;
  }
  if (!failed && fprintf(out, "#endif /* DATABIND_NATIVE_ENABLE_CMETA */\n") < 0)
    failed = 1;
  if (ferror(out)) failed = 1;
  if (fclose(out) != 0) failed = 1;
  if (failed) {
    (void)remove(path);
    return -1;
  }
  return 0;
}
