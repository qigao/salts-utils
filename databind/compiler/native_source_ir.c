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

/* These spellings are the exported Salts CMeta scalar descriptors, not
 * compiler-owned substitutes. A type without a canonical descriptor stays
 * unadmitted until an explicit provider contract exists. */
static const char *native_sequence_cmeta_symbol(const char *type) {
  static const native_scalar_map descriptors[] = {
      {"int8", "cmeta_type_int8"}, {"uint8", "cmeta_type_uint8"},
      {"int16", "cmeta_type_int16"}, {"uint16", "cmeta_type_uint16"},
      {"int32", "cmeta_type_int32"}, {"uint32", "cmeta_type_uint32"},
      {"int64", "cmeta_type_int64"}, {"uint64", "cmeta_type_uint64"}
  };
  size_t i;
  if (type == NULL) return NULL;
  if (strcmp(type, "string") == 0) return "databind_native_text_cmeta_type";
  if (strcmp(type, "bytes") == 0) return "databind_native_bytes_cmeta_type";
  for (i = 0u; i < sizeof(descriptors) / sizeof(descriptors[0]); ++i)
    if (strcmp(type, descriptors[i].idl) == 0) return descriptors[i].c;
  return NULL;
}

/* Build an ownership plan even for types the current renderer cannot emit.
 * Never confuse non-trivial fields with a trivially copied scalar. */
static int native_field_ownership(
    const IdlContract *contract, const IdlField *field) {
  size_t i;
  if (field->collection_kind != IDL_COLLECTION_NONE) {
    switch (field->collection_kind) {
      case IDL_COLLECTION_ARRAY:
      case IDL_COLLECTION_LIST: return DATABIND_NATIVE_OWNED_SEQUENCE;
      case IDL_COLLECTION_SET: return DATABIND_NATIVE_OWNED_SET;
      case IDL_COLLECTION_MAP: return DATABIND_NATIVE_OWNED_MAP;
      default: return -1;
    }
  }
  if (native_scalar(field->type_name) != NULL) return DATABIND_NATIVE_TRIVIAL;
  if (field->type_name == NULL) return -1;
  if (strcmp(field->type_name, "string") == 0)
    return DATABIND_NATIVE_OWNED_TEXT;
  if (strcmp(field->type_name, "bytes") == 0)
    return DATABIND_NATIVE_OWNED_BYTES;
  for (i = 0u; i < contract->data_count; ++i) {
    const IdlDataDecl *decl = &contract->data[i];
    if (decl->name != NULL && strcmp(decl->name, field->type_name) == 0 &&
        (decl->kind == IDL_DATA_MESSAGE || decl->kind == IDL_DATA_COMPOSITE))
      return DATABIND_NATIVE_OWNED_RECORD;
  }
  return -1;
}

/* Value-embedded records require dependency-first C declaration order.
 * A visiting node reached again is a recursive value cycle, not a pointer
 * reference. Reject it before publishing any generated header. */
static int native_order_record(const databind_native_source_ir *ir,
                               size_t index, unsigned char *state,
                               size_t *order, size_t *count) {
  size_t j, k;
  const databind_native_source_record *record = &ir->records[index];
  if (state[index] == 2u) return 0;
  if (state[index] == 1u) return -1;
  state[index] = 1u;
  for (j = 0u; j < record->field_count; ++j) {
    const databind_native_source_field *field = &record->fields[j];
    if (field->ownership != DATABIND_NATIVE_OWNED_RECORD) continue;
    if (field->c_type == NULL) return -1;
    for (k = 0u; k < ir->record_count; ++k)
      if (strcmp(ir->records[k].name, field->c_type) == 0) break;
    if (k == ir->record_count ||
        native_order_record(ir, k, state, order, count) != 0)
      return -1;
  }
  state[index] = 2u;
  order[(*count)++] = index;
  return 0;
}

static int native_sort_records(databind_native_source_ir *ir) {
  size_t i, count = 0u;
  unsigned char *state;
  size_t *order;
  databind_native_source_record *sorted;
  if (ir->record_count == 0u) return 0;
  state = (unsigned char *)calloc(ir->record_count, sizeof(*state));
  order = (size_t *)malloc(ir->record_count * sizeof(*order));
  sorted = (databind_native_source_record *)malloc(
      ir->record_count * sizeof(*sorted));
  if (!state || !order || !sorted) {
    free(state); free(order); free(sorted);
    return -1;
  }
  for (i = 0u; i < ir->record_count; ++i)
    if (native_order_record(ir, i, state, order, &count) != 0) break;
  if (i == ir->record_count && count == ir->record_count) {
    for (i = 0u; i < count; ++i) sorted[i] = ir->records[order[i]];
    free(ir->records);
    ir->records = sorted;
    sorted = NULL;
  }
  free(state); free(order); free(sorted);
  return count == ir->record_count ? 0 : -1;
}

/* A record descriptor is usable as a sequence element or Map value.
 * Record keys and Sets still require an explicit canonical comparator. */
static int native_record_declared(const databind_native_source_ir *ir,
                                  const char *name) {
  size_t i;
  if (ir == NULL || name == NULL) return 0;
  for (i = 0u; i < ir->record_count; ++i)
    if (ir->records[i].name != NULL &&
        strcmp(ir->records[i].name, name) == 0) return 1;
  return 0;
}

static const char *native_collection_element_symbol(
    const databind_native_source_ir *ir, const char *provider,
    const char *logical_type, char *buffer, size_t capacity) {
  int n;
  if (provider != NULL) return provider;
  if (!native_record_declared(ir, logical_type) || !buffer || capacity == 0u)
    return NULL;
  n = snprintf(buffer, capacity, "%s_native_element_cmeta_type", logical_type);
  return n >= 0 && (size_t)n < capacity ? buffer : NULL;
}

static int native_record_needs_element_traits(
    const databind_native_source_ir *ir, const char *name) {
  size_t i, j;
  if (!ir || !name) return 0;
  for (i = 0u; i < ir->record_count; ++i) {
    const databind_native_source_record *record = &ir->records[i];
    for (j = 0u; j < record->field_count; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      if (field->ownership == DATABIND_NATIVE_OWNED_SEQUENCE &&
          field->element_type != NULL &&
          strcmp(field->element_type, name) == 0)
        return 1;
      if (field->ownership == DATABIND_NATIVE_OWNED_MAP &&
          field->value_type != NULL &&
          strcmp(field->value_type, name) == 0)
        return 1;
    }
  }
  return 0;
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
  if (contract == NULL || contract->name == NULL ||
      contract->name[0] == '\0' || out == NULL || out->records != NULL ||
      out->record_count != 0u || out->schema_name != NULL ||
      out->schema_version != NULL)
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
  plan.schema_name = contract->name;
  plan.schema_version = contract->version;
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
      const int ownership = native_field_ownership(contract, field);
      const char *type = native_scalar(field->type_name);
      if (field->name == NULL || field->name[0] == '\0' ||
          field->default_value != NULL || ownership < 0)
        goto fail;
      record->fields[j].name = field->name;
      record->fields[j].c_type = type != NULL ? type :
          (ownership == DATABIND_NATIVE_OWNED_TEXT ? "databind_native_text" :
           ownership == DATABIND_NATIVE_OWNED_BYTES ? "databind_native_bytes" :
           ownership == DATABIND_NATIVE_OWNED_RECORD ? field->type_name :
           ownership == DATABIND_NATIVE_OWNED_SEQUENCE ? "vec_t" :
           ownership == DATABIND_NATIVE_OWNED_MAP ? "map_t" :
           ownership == DATABIND_NATIVE_OWNED_SET ? "set_t" : NULL);
      record->fields[j].optional = field->optional != 0;
      record->fields[j].nullable = field->nullable != 0;
      record->fields[j].ownership = (databind_native_source_ownership)ownership;
      if (ownership == DATABIND_NATIVE_OWNED_SEQUENCE ||
          ownership == DATABIND_NATIVE_OWNED_SET) {
        const char *element = field->inner_type;
        if (element == NULL || element[0] == '\0') goto fail;
        record->fields[j].element_type = element;
        record->fields[j].element_is_trivial = native_scalar(element) != NULL;
        record->fields[j].element_cmeta_symbol =
            native_sequence_cmeta_symbol(element);
      }
      if (ownership == DATABIND_NATIVE_OWNED_MAP) {
        record->fields[j].key_type = field->key_type;
        record->fields[j].value_type = field->value_type;
        record->fields[j].key_cmeta_symbol =
            native_sequence_cmeta_symbol(field->key_type);
        record->fields[j].value_cmeta_symbol =
            native_sequence_cmeta_symbol(field->value_type);
        if (field->key_type == NULL || field->value_type == NULL)
          goto fail;
      }
    }
  }
  if (native_sort_records(&plan) != 0) goto fail;
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

/* Namespace parts are interpolated into C string literals. Reject
 * punctuation requiring escaping, rather than emitting ambiguous identities.
 * Version is an optional stable token, never a rendered C identifier. */
static int native_version_token(const char *version) {
  const unsigned char *p = (const unsigned char *)version;
  if (p == NULL || *p == '\0') return 1;
  for (; *p != '\0'; ++p)
    if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= 'a' && *p <= 'z') || *p == '.' || *p == '-'))
      return 0;
  return 1;
}

static int native_format_stable_id(
    const databind_native_source_ir *ir, const char *record,
    char *out, size_t capacity) {
  int count;
  if (ir == NULL || ir->schema_name == NULL || record == NULL ||
      out == NULL || capacity == 0u)
    return 0;
  if (ir->schema_version != NULL && ir->schema_version[0] != '\0')
    count = snprintf(out, capacity, "tbe.native.%s.v%s.%s",
                     ir->schema_name, ir->schema_version, record);
  else
    count = snprintf(out, capacity, "tbe.native.%s.%s",
                     ir->schema_name, record);
  return count >= 0 && (size_t)count < capacity;
}

static int native_source_ir_render(
    const databind_native_source_ir *ir, const char *path, FILE *stream) {
  FILE *out;
  size_t i, j;
  int failed = 0;
  int has_owning = 0;
  if (ir == NULL || (stream == NULL && (path == NULL || path[0] == '\0')) ||
      !native_identifier(ir->schema_name) ||
      !native_version_token(ir->schema_version) ||
      (ir->record_count != 0u && ir->records == NULL))
    return -1;
  for (i = 0u; i < ir->record_count; ++i) {
    const databind_native_source_record *record = &ir->records[i];
    char stable_type_id[1024];
    /* Refuse unrepresentable IDs before opening a caller-owned output. */
    if (!native_format_stable_id(ir, record->name, stable_type_id,
                                 sizeof(stable_type_id)))
      return -1;
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
          !((record->fields[j].ownership == DATABIND_NATIVE_TRIVIAL &&
              native_c_type(record->fields[j].c_type)) ||
            (record->fields[j].ownership == DATABIND_NATIVE_OWNED_TEXT &&
             record->fields[j].c_type != NULL &&
             strcmp(record->fields[j].c_type, "databind_native_text") == 0) ||
            (record->fields[j].ownership == DATABIND_NATIVE_OWNED_BYTES &&
             record->fields[j].c_type != NULL &&
             strcmp(record->fields[j].c_type, "databind_native_bytes") == 0) ||
            (record->fields[j].ownership == DATABIND_NATIVE_OWNED_SEQUENCE &&
             record->fields[j].c_type != NULL &&
             strcmp(record->fields[j].c_type, "vec_t") == 0 &&
             (record->fields[j].element_cmeta_symbol != NULL ||
              native_record_declared(ir, record->fields[j].element_type))) ||
            (record->fields[j].ownership == DATABIND_NATIVE_OWNED_RECORD &&
             record->fields[j].c_type != NULL) ||
            (record->fields[j].ownership == DATABIND_NATIVE_OWNED_MAP &&
             record->fields[j].key_cmeta_symbol != NULL &&
             (record->fields[j].value_cmeta_symbol != NULL ||
              native_record_declared(ir, record->fields[j].value_type)) &&
             record->fields[j].c_type != NULL &&
             strcmp(record->fields[j].c_type, "map_t") == 0) ||
            (record->fields[j].ownership == DATABIND_NATIVE_OWNED_SET &&
             record->fields[j].element_cmeta_symbol != NULL &&
             record->fields[j].c_type != NULL &&
             strcmp(record->fields[j].c_type, "set_t") == 0)) ||
          native_field_collides(record, j))
        return -1; /* Renderer only accepts canonical lowered C types. */
  }
  out = stream != NULL ? stream : fopen(path, "wb");
  if (out == NULL) return -1;
  if (fprintf(out,
      "#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n"
      "#include <stddef.h>\n#include <stdlib.h>\n#include <string.h>\n\n"
      "typedef struct databind_native_text { char *data; size_t size; } databind_native_text;\n"
      "typedef struct databind_native_bytes { unsigned char *data; size_t size; } databind_native_bytes;\n\n") < 0)
    failed = 1;
  for (i = 0u; i < ir->record_count; ++i) {
    for (j = 0u; j < ir->records[i].field_count; ++j)
      if (ir->records[i].fields[j].ownership == DATABIND_NATIVE_OWNED_SEQUENCE)
        break;
    if (j != ir->records[i].field_count) break;
  }
  if (i != ir->record_count &&
      fprintf(out, "#include <cstl/vec.h>\n\n") < 0) failed = 1;
  for (i = 0u; i < ir->record_count; ++i) {
    for (j = 0u; j < ir->records[i].field_count; ++j)
      if (ir->records[i].fields[j].ownership == DATABIND_NATIVE_OWNED_MAP ||
          ir->records[i].fields[j].ownership == DATABIND_NATIVE_OWNED_SET)
        break;
    if (j != ir->records[i].field_count) break;
  }
  if (i != ir->record_count &&
      fprintf(out, "#include <cstl/map.h>\n#include <cstl/set.h>\n\n") < 0)
    failed = 1;
  /* A collection uses real CSTL element traits; native scalar/header-only
   * output remains free of a Salts dependency until collections are selected. */
  {
    int text_provider = 0, bytes_provider = 0;
    for (i = 0u; i < ir->record_count; ++i) {
      for (j = 0u; j < ir->records[i].field_count; ++j) {
        const databind_native_source_field *field = &ir->records[i].fields[j];
        const char *symbols[] = {field->element_cmeta_symbol,
                                 field->key_cmeta_symbol,
                                 field->value_cmeta_symbol};
        size_t k;
        for (k = 0u; k < sizeof(symbols)/sizeof(symbols[0]); ++k) {
          if (symbols[k] == NULL) continue;
          if (strcmp(symbols[k], "databind_native_text_cmeta_type") == 0)
            text_provider = 1;
          if (strcmp(symbols[k], "databind_native_bytes_cmeta_type") == 0)
            bytes_provider = 1;
        }
      }
    }
    if (!failed && text_provider &&
        fputs(
        "/* Native text CSTL provider: copy/move/destroy are explicit ownership operations. */\n"
        "static bool databind_native_text_trait_copy(void *destination, const void *source) {\n"
        "    const databind_native_text *src = (const databind_native_text *)source;\n"
        "    databind_native_text copy = {0};\n"
        "    if (!destination || !src || (src->size && !src->data) || src->size == SIZE_MAX)\n"
        "        return false;\n"
        "    if (src->data) {\n"
        "        copy.data = (char *)malloc(src->size + 1u);\n"
        "        if (!copy.data) return false;\n"
        "        if (src->size) memcpy(copy.data, src->data, src->size);\n"
        "        copy.data[src->size] = '\\0';\n"
        "    }\n"
        "    copy.size = src->size;\n"
        "    *(databind_native_text *)destination = copy;\n"
        "    return true;\n"
        "}\n"
        "static void databind_native_text_trait_move(void *destination, void *source) {\n"
        "    if (!destination || !source || destination == source) return;\n"
        "    *(databind_native_text *)destination = *(databind_native_text *)source;\n"
        "    *(databind_native_text *)source = (databind_native_text){0};\n"
        "}\n"
        "static void databind_native_text_trait_destroy(void *value) {\n"
        "    databind_native_text *item = (databind_native_text *)value;\n"
        "    if (!item) return;\n"
        "    free(item->data);\n"
        "    *item = (databind_native_text){0};\n"
        "}\n"
        "static int databind_native_text_trait_compare(const void *left, const void *right) {\n"
        "    const databind_native_text *a = (const databind_native_text *)left;\n"
        "    const databind_native_text *b = (const databind_native_text *)right;\n"
        "    size_t n;\n"
        "    int order;\n"
        "    if (!a || !b || (a->size && !a->data) || (b->size && !b->data)) abort();\n"
        "    n = a->size < b->size ? a->size : b->size;\n"
        "    order = n ? memcmp(a->data, b->data, n) : 0;\n"
        "    return order ? order : (a->size > b->size) - (a->size < b->size);\n"
        "}\n"
        "static const cmeta_type_traits databind_native_text_cmeta_traits = {\n"
        "    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY | CMETA_TRAIT_COMPARE,\n"
        "    .compare = databind_native_text_trait_compare,\n"
        "    .copy_construct = databind_native_text_trait_copy,\n"
        "    .move_construct = databind_native_text_trait_move,\n"
        "    .destroy = databind_native_text_trait_destroy\n"
        "};\n"
        "static const cmeta_type_identity databind_native_text_cmeta_identity =\n"
        "    CMETA_TYPE_ID_ATOM_INIT(\"tbe.native.owned_text.v1\");\n"
        "static const cmeta_type_desc databind_native_text_cmeta_type = {\n"
        "    .name = \"databind_native_text\",\n"
        "    .size = sizeof(databind_native_text), .align = _Alignof(databind_native_text),\n"
        "    .kind = CMETA_T_OBJECT, .traits = &databind_native_text_cmeta_traits,\n"
        "    .identity = &databind_native_text_cmeta_identity\n"
        "};\n",
        out) == EOF) failed = 1;
    if (!failed && bytes_provider &&
        fputs(
        "/* Native bytes CSTL provider: copy/move/destroy are explicit ownership operations. */\n"
        "static bool databind_native_bytes_trait_copy(void *destination, const void *source) {\n"
        "    const databind_native_bytes *src = (const databind_native_bytes *)source;\n"
        "    databind_native_bytes copy = {0};\n"
        "    if (!destination || !src || (src->size && !src->data))\n"
        "        return false;\n"
        "    if (src->size) {\n"
        "        copy.data = (unsigned char *)malloc(src->size);\n"
        "        if (!copy.data) return false;\n"
        "        if (src->size) memcpy(copy.data, src->data, src->size);\n"
        "    }\n"
        "    copy.size = src->size;\n"
        "    *(databind_native_bytes *)destination = copy;\n"
        "    return true;\n"
        "}\n"
        "static void databind_native_bytes_trait_move(void *destination, void *source) {\n"
        "    if (!destination || !source || destination == source) return;\n"
        "    *(databind_native_bytes *)destination = *(databind_native_bytes *)source;\n"
        "    *(databind_native_bytes *)source = (databind_native_bytes){0};\n"
        "}\n"
        "static void databind_native_bytes_trait_destroy(void *value) {\n"
        "    databind_native_bytes *item = (databind_native_bytes *)value;\n"
        "    if (!item) return;\n"
        "    free(item->data);\n"
        "    *item = (databind_native_bytes){0};\n"
        "}\n"
        "static int databind_native_bytes_trait_compare(const void *left, const void *right) {\n"
        "    const databind_native_bytes *a = (const databind_native_bytes *)left;\n"
        "    const databind_native_bytes *b = (const databind_native_bytes *)right;\n"
        "    size_t n;\n"
        "    int order;\n"
        "    if (!a || !b || (a->size && !a->data) || (b->size && !b->data)) abort();\n"
        "    n = a->size < b->size ? a->size : b->size;\n"
        "    order = n ? memcmp(a->data, b->data, n) : 0;\n"
        "    return order ? order : (a->size > b->size) - (a->size < b->size);\n"
        "}\n"
        "static const cmeta_type_traits databind_native_bytes_cmeta_traits = {\n"
        "    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY | CMETA_TRAIT_COMPARE,\n"
        "    .compare = databind_native_bytes_trait_compare,\n"
        "    .copy_construct = databind_native_bytes_trait_copy,\n"
        "    .move_construct = databind_native_bytes_trait_move,\n"
        "    .destroy = databind_native_bytes_trait_destroy\n"
        "};\n"
        "static const cmeta_type_identity databind_native_bytes_cmeta_identity =\n"
        "    CMETA_TYPE_ID_ATOM_INIT(\"tbe.native.owned_bytes.v1\");\n"
        "static const cmeta_type_desc databind_native_bytes_cmeta_type = {\n"
        "    .name = \"databind_native_bytes\",\n"
        "    .size = sizeof(databind_native_bytes), .align = _Alignof(databind_native_bytes),\n"
        "    .kind = CMETA_T_OBJECT, .traits = &databind_native_bytes_cmeta_traits,\n"
        "    .identity = &databind_native_bytes_cmeta_identity\n"
        "};\n",
        out) == EOF) failed = 1;
  }
  /* Descriptors may be referenced before their record definition, and
   * collection-recursive records are legal even when inline value cycles are not. */
  for (i = 0u; i < ir->record_count && !failed; ++i)
    if (native_record_needs_element_traits(ir, ir->records[i].name) &&
        fprintf(out, "static const cmeta_type_desc %s_native_element_cmeta_type;\n",
                ir->records[i].name) < 0) failed = 1;
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
        "static inline void %s_init(%s *value) { if (value) *value = (%s){0}; }\n"
        "static inline void %s_clear(%s *value) {\n"
        "    if (!value) return;\n",
        record->name, record->name, record->name,
        record->name, record->name) < 0) failed = 1;
    for (j = 0u; j < record->field_count && !failed; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      if (field->ownership == DATABIND_NATIVE_OWNED_TEXT ||
          field->ownership == DATABIND_NATIVE_OWNED_BYTES)
        if (fprintf(out, "    free(value->%s.data);\n", field->name) < 0)
          failed = 1;
      if (field->ownership == DATABIND_NATIVE_OWNED_RECORD)
        if (fprintf(out, "    %s_clear(&value->%s);\n",
                    field->c_type, field->name) < 0) failed = 1;
      if (field->ownership == DATABIND_NATIVE_OWNED_SEQUENCE)
        if (fprintf(out, "    if (value->%s.initialized) vec_destroy(&value->%s);\n",
                    field->name, field->name) < 0) failed = 1;
      if (field->ownership == DATABIND_NATIVE_OWNED_MAP)
        if (fprintf(out, "    if (value->%s.impl) map_destroy(&value->%s);\n",
                    field->name, field->name) < 0) failed = 1;
      if (field->ownership == DATABIND_NATIVE_OWNED_SET)
        if (fprintf(out, "    if (value->%s.map.impl) set_destroy(&value->%s);\n",
                    field->name, field->name) < 0) failed = 1;
    }
    if (!failed && fprintf(out,
        "    *value = (%s){0};\n}\n"
        "static inline int %s_clone(%s *dst, const %s *src) {\n"
        "    if (!dst || !src) return -1;\n"
        "    if (dst == src) return 0;\n"
        "    %s tmp = *src;\n",
        record->name, record->name, record->name, record->name,
        record->name) < 0) failed = 1;
    for (j = 0u; j < record->field_count && !failed; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      if (field->ownership == DATABIND_NATIVE_OWNED_RECORD) {
        if (fprintf(out, "    %s_init(&tmp.%s);\n",
                    field->c_type, field->name) < 0) failed = 1;
        continue;
      }
      if (field->ownership == DATABIND_NATIVE_OWNED_SEQUENCE) {
        if (fprintf(out, "    tmp.%s = (vec_t){0};\n",
                    field->name) < 0) failed = 1;
        continue;
      }
      if (field->ownership == DATABIND_NATIVE_OWNED_MAP ||
          field->ownership == DATABIND_NATIVE_OWNED_SET) {
        if (fprintf(out, "    tmp.%s = (%s){0};\n",
                    field->name, field->c_type) < 0) failed = 1;
        continue;
      }
      if (field->ownership != DATABIND_NATIVE_OWNED_TEXT &&
          field->ownership != DATABIND_NATIVE_OWNED_BYTES) continue;
      if (fprintf(out, "    tmp.%s.data = NULL; tmp.%s.size = 0;\n",
                  field->name, field->name) < 0) failed = 1;
    }
    for (j = 0u; j < record->field_count && !failed; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      char element_buffer[256], value_buffer[256];
      const char *element_cmeta = native_collection_element_symbol(
          ir, field->element_cmeta_symbol, field->element_type,
          element_buffer, sizeof(element_buffer));
      const char *value_cmeta = native_collection_element_symbol(
          ir, field->value_cmeta_symbol, field->value_type,
          value_buffer, sizeof(value_buffer));
      if (field->ownership == DATABIND_NATIVE_OWNED_RECORD) {
        if (fprintf(out,
            "    if (%s_clone(&tmp.%s, &src->%s) != 0) goto native_clone_fail;\n",
            field->c_type, field->name, field->name) < 0) failed = 1;
        continue;
      }
      if (field->ownership == DATABIND_NATIVE_OWNED_MAP) {
        if (fprintf(out,
            "    if (src->%s.impl) {\n"
            "      if (!cmeta_type_equal(src->%s.key_type, &%s) ||\n"
            "          !cmeta_type_equal(src->%s.value_type, &%s)) goto native_clone_fail;\n"
            "      tmp.%s.key_type = &%s; tmp.%s.value_type = &%s;\n"
            "      if (map_init(&tmp.%s, map_entry_limit(&src->%s)) != STL_OK) goto native_clone_fail;\n"
            "      cmeta_range_cursor cursor_%s = {0};\n"
            "      const void *key_%s = NULL, *value_%s = NULL;\n"
            "      while (map_range_next(&src->%s, &cursor_%s, &key_%s, &value_%s)) {\n"
            "        if (map_put(&tmp.%s, key_%s, value_%s) != STL_OK) goto native_clone_fail;\n"
            "      }\n"
            "    } else if (src->%s.key_type || src->%s.value_type) {\n"
            "      if (!cmeta_type_equal(src->%s.key_type, &%s) ||\n"
            "          !cmeta_type_equal(src->%s.value_type, &%s)) goto native_clone_fail;\n"
            "      tmp.%s = src->%s;\n"
            "    }\n",
            field->name, field->name, field->key_cmeta_symbol,
            field->name, value_cmeta,
            field->name, field->key_cmeta_symbol, field->name, value_cmeta,
            field->name, field->name,
            field->name, field->name, field->name,
            field->name, field->name, field->name, field->name,
            field->name, field->name, field->name,
            field->name, field->name, field->name, field->key_cmeta_symbol,
            field->name, value_cmeta, field->name, field->name) < 0) failed = 1;
        continue;
      }
      if (field->ownership == DATABIND_NATIVE_OWNED_SET) {
        if (fprintf(out,
            "    if (src->%s.map.impl) {\n"
            "      if (!cmeta_type_equal(src->%s.element_type, &%s)) goto native_clone_fail;\n"
            "      tmp.%s.element_type = &%s;\n"
            "      if (set_init(&tmp.%s, set_element_limit(&src->%s)) != STL_OK) goto native_clone_fail;\n"
            "      cmeta_range_cursor cursor_%s = {0};\n"
            "      const void *element_%s = NULL;\n"
            "      while (set_range_next(&src->%s, &cursor_%s, &element_%s)) {\n"
            "        if (set_add(&tmp.%s, element_%s) != STL_OK) goto native_clone_fail;\n"
            "      }\n"
            "    } else if (src->%s.element_type || src->%s.map.key_type) {\n"
            "      if (!cmeta_type_equal(src->%s.element_type, &%s)) goto native_clone_fail;\n"
            "      tmp.%s = src->%s;\n"
            "    }\n",
            field->name, field->name, element_cmeta,
            field->name, element_cmeta, field->name, field->name,
            field->name, field->name,
            field->name, field->name, field->name,
            field->name, field->name,
            field->name, field->name, field->name, element_cmeta,
            field->name, field->name) < 0) failed = 1;
        continue;
      }
      if (field->ownership == DATABIND_NATIVE_OWNED_SEQUENCE) {
        if (fprintf(out,
            "    if (src->%s.initialized) {\n"
            "      if (!cmeta_type_equal(src->%s.element_type, &%s)) goto native_clone_fail;\n"
            "      if (vec_raw_init(&tmp.%s, &%s, src->%s.element_limit) != STL_OK) goto native_clone_fail;\n"
            "      for (size_t k = 0; k < vec_size(&src->%s); ++k) {\n"
            "        const void *element = vec_at_const(&src->%s, k);\n"
            "        if (!element || vec_push(&tmp.%s, element) != STL_OK) goto native_clone_fail;\n"
            "      }\n"
            "    } else if (src->%s.data || src->%s.size) goto native_clone_fail;\n",
            field->name, field->name, element_cmeta,
            field->name, element_cmeta, field->name,
            field->name, field->name, field->name, field->name,
            field->name) < 0) failed = 1;
        continue;
      }
      if (field->ownership != DATABIND_NATIVE_OWNED_TEXT &&
          field->ownership != DATABIND_NATIVE_OWNED_BYTES) continue;
      if (fprintf(out,
          "    if (src->%s.size) {\n"
          "        if (!src->%s.data) goto native_clone_fail;\n",
          field->name, field->name) < 0) failed = 1;
      if (field->ownership == DATABIND_NATIVE_OWNED_TEXT &&
          fprintf(out,
          "        if (src->%s.size == SIZE_MAX) goto native_clone_fail;\n",
          field->name) < 0) failed = 1;
      if (fprintf(out,
          "        tmp.%s.data = malloc(src->%s.size%s);\n"
          "        if (!tmp.%s.data) goto native_clone_fail;\n"
          "        memcpy(tmp.%s.data, src->%s.data, src->%s.size);\n",
          field->name, field->name,
          field->ownership == DATABIND_NATIVE_OWNED_TEXT ? " + 1u" : "",
          field->name, field->name, field->name, field->name) < 0) failed = 1;
      if (field->ownership == DATABIND_NATIVE_OWNED_TEXT &&
          fprintf(out, "        tmp.%s.data[src->%s.size] = '\\0';\n",
                  field->name, field->name) < 0) failed = 1;
      if (fprintf(out, "        tmp.%s.size = src->%s.size;\n    }\n",
                  field->name, field->name) < 0) failed = 1;
    }
    if (!failed && fprintf(out,
        "    if (0) goto native_clone_fail;\n"
        "    %s_clear(dst); *dst = tmp; return 0;\n"
        "native_clone_fail:\n"
        "    %s_clear(&tmp); return -1;\n}\n"
        "static inline int %s_move(%s *dst, %s *src) {\n"
        "    if (!dst || !src) return -1;\n"
        "    if (dst != src) { %s_clear(dst); *dst = *src; *src = (%s){0}; }\n"
        "    return 0;\n}\n\n",
        record->name, record->name,
        record->name, record->name, record->name,
        record->name, record->name) < 0) failed = 1;
    if (!failed && native_record_needs_element_traits(ir, record->name)) {
      char stable_id[1024];
      if (!native_format_stable_id(ir, record->name, stable_id,
                                   sizeof(stable_id))) {
        failed = 1;
      } else if (fprintf(out,
          "static bool %s_native_element_copy(void *dst, const void *src) {\n"
          "    if (!dst || !src) return false;\n"
          "    %s_init((%s *)dst);\n"
          "    return %s_clone((%s *)dst, (const %s *)src) == 0;\n"
          "}\n"
          "static void %s_native_element_move(void *dst, void *src) {\n"
          "    if (!dst || !src || dst == src) return;\n"
          "    %s_init((%s *)dst);\n"
          "    if (%s_move((%s *)dst, (%s *)src) != 0) abort();\n"
          "}\n"
          "static void %s_native_element_destroy(void *value) {\n"
          "    %s_clear((%s *)value);\n"
          "}\n",
          record->name, record->name, record->name,
          record->name, record->name, record->name,
          record->name, record->name, record->name,
          record->name, record->name, record->name,
          record->name, record->name, record->name) < 0)
        failed = 1;
      if (!failed && fprintf(out,
          "static const cmeta_type_traits %s_native_element_traits = {\n"
          "    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,\n"
          "    .copy_construct = %s_native_element_copy,\n"
          "    .move_construct = %s_native_element_move,\n"
          "    .destroy = %s_native_element_destroy\n"
          "};\n"
          "static const cmeta_type_identity %s_native_element_identity =\n"
          "    CMETA_TYPE_ID_ATOM_INIT(\"%s\");\n"
          "static const cmeta_type_desc %s_native_element_cmeta_type = {\n"
          "    .name = \"%s\", .size = sizeof(%s), .align = _Alignof(%s),\n"
          "    .kind = CMETA_T_OBJECT,\n"
          "    .traits = &%s_native_element_traits,\n"
          "    .identity = &%s_native_element_identity\n"
          "};\n\n",
          record->name, record->name, record->name, record->name,
          record->name, stable_id, record->name, record->name,
          record->name, record->name, record->name, record->name) < 0)
        failed = 1;
    }
  }
  /* Reuse Salts CMeta descriptors without imposing a Salts dependency on
   * the standalone C11 source-only header unless reflection is requested. */
  /* Owning CMeta field providers are not installed yet; reject their
   * reflection opt-in instead of projecting fake scalar descriptors. */
  { for (i = 0u; i < ir->record_count; ++i)
      for (j = 0u; j < ir->records[i].field_count; ++j)
        if (ir->records[i].fields[j].ownership != DATABIND_NATIVE_TRIVIAL)
          has_owning = 1;
    if (has_owning && !failed &&
        fprintf(out, "#ifdef DATABIND_NATIVE_ENABLE_CMETA\n#error Native owning CMeta reflection is not implemented\n#endif\n") < 0)
      failed = 1;
  }
  if (!has_owning) {
  if (!failed && fprintf(out,
      "#ifdef DATABIND_NATIVE_ENABLE_CMETA\n"
      "#include <cmeta_cmeta_data.h>\n") < 0)
    failed = 1;
  for (i = 0u; i < ir->record_count && !failed; ++i) {
    const databind_native_source_record *record = &ir->records[i];
    char stable_type_id[1024];
    if (!native_format_stable_id(ir, record->name, stable_type_id,
                                 sizeof(stable_type_id))) {
      failed = 1;
      break;
    }
    /* Optional/nullable are separate native flags, not fields of the CMeta
     * logical value view. Preserve their exact byte offsets explicitly. */
    if (!failed && fprintf(out,
        "typedef struct %s_native_state_desc {\n"
        "    const char *name;\n"
        "    size_t presence_offset;\n"
        "    size_t null_offset;\n"
        "} %s_native_state_desc;\n"
        "static const %s_native_state_desc %s_native_cmeta_states[] = {\n",
        record->name, record->name, record->name, record->name) < 0)
      failed = 1;
    for (j = 0u; j < record->field_count && !failed; ++j) {
      const databind_native_source_field *field = &record->fields[j];
      if (fprintf(out, "    {\"%s\", ", field->name) < 0)
        failed = 1;
      if (!failed && (field->optional
          ? fprintf(out, "offsetof(%s, has_%s), ", record->name, field->name)
          : fprintf(out, "SIZE_MAX, ")) < 0)
        failed = 1;
      if (!failed && (field->nullable
          ? fprintf(out, "offsetof(%s, is_null_%s)", record->name, field->name)
          : fprintf(out, "SIZE_MAX")) < 0)
        failed = 1;
      if (!failed && fprintf(out, "},\n") < 0)
        failed = 1;
    }
    if (!failed && fprintf(out, "};\n") < 0)
      failed = 1;
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
          "\"%s.%s\", \"%s\", offsetof(%s, %s), "
          "&cmeta_data_%s};\n",
          j, stable_type_id, field->name, field->name,
          record->name, field->name, scalar) < 0)
        failed = 1;
    }
    if (!failed && fprintf(out, "    return 0;\n}\n") < 0)
      failed = 1;
    if (!failed && fprintf(out,
        "static const cmeta_type_identity %s_native_cmeta_identity =\n"
        "    CMETA_TYPE_ID_ATOM_INIT(\"%s\");\n"
        "static const cmeta_type_desc %s_native_cmeta_type = {\n"
        "    .name = \"%s\",\n"
        "    .size = sizeof(%s),\n"
        "    .align = _Alignof(%s),\n"
        "    .kind = CMETA_T_OBJECT,\n"
        "    .identity = &%s_native_cmeta_identity\n"
        "};\n",
        record->name, stable_type_id,
        record->name, record->name, record->name, record->name,
        record->name) < 0)
      failed = 1;
  }
  /* Reflection V2 VIEW intentionally has no value lifecycle: native flags
   * for optional/nullable fields are outside the logical field projection.
   * Every pointer below borrows caller-owned binding storage. Do not move or
   * copy an initialized binding: bind again in its final location. */
  for (i = 0u; i < ir->record_count && !failed; ++i) {
    const databind_native_source_record *record = &ir->records[i];
    char stable_type_id[1024];
    if (!native_format_stable_id(ir, record->name, stable_type_id,
                                 sizeof(stable_type_id))) {
      failed = 1;
      break;
    }
    if (fprintf(out,
        "typedef struct %s_native_cmeta_binding {\n"
        "    cmeta_field_desc layout_fields[%zuu];\n"
        "    cmeta_data_field_desc data_fields[%zuu];\n"
        "    cmeta_struct_desc layout;\n"
        "    cmeta_data_reflection_shape reflection;\n"
        "    cmeta_data_desc data;\n"
        "} %s_native_cmeta_binding;\n"
        "static inline int %s_native_cmeta_bind(\n"
        "    %s_native_cmeta_binding *binding) {\n"
        "    size_t i;\n"
        "    if (binding == NULL || %zuu > CMETA_DATA_REFLECTION_MAX_FIELDS ||\n"
        "        %s_native_cmeta_data_fields(binding->data_fields, %zuu) != 0)\n"
        "        return -1;\n"
        "    for (i = 0u; i < %zuu; ++i) {\n"
        "        const cmeta_data_desc *value = binding->data_fields[i].value;\n"
        "        if (value == NULL || value->storage_type == NULL) return -1;\n"
        "        binding->layout_fields[i] = %s_native_cmeta_fields[i];\n"
        "        binding->layout_fields[i].type = value->storage_type;\n"
        "    }\n"
        "    binding->layout = %s_native_cmeta_layout;\n"
        "    binding->layout.fields = binding->layout_fields;\n"
        "    binding->reflection = (cmeta_data_reflection_shape){\n"
        "        {&binding->layout, binding->data_fields, %zuu},\n"
        "        sizeof(cmeta_data_reflection_shape), CMETA_DATA_REFLECTION_VIEW\n"
        "    };\n"
        "    binding->data = (cmeta_data_desc){\n"
        "        .struct_size = sizeof(cmeta_data_desc),\n"
        "        .abi_version = CMETA_DATA_DESC_REFLECTION_ABI_VERSION,\n"
        "        .stable_id = \"%s.data\",\n"
        "        .display_name = \"%s\",\n"
        "        .kind = CMETA_DATA_STRUCT,\n"
        "        .storage_type = &%s_native_cmeta_type,\n"
        "        .shape = &binding->reflection\n"
        "    };\n"
        "    return cmeta_data_desc_valid(&binding->data) ? 0 : -1;\n"
        "}\n",
        record->name, record->field_count, record->field_count,
        record->name, record->name, record->name,
        record->field_count, record->name, record->field_count,
        record->field_count, record->name, record->name,
        record->field_count, stable_type_id, record->name,
        record->name) < 0)
      failed = 1;
  }
  if (!failed && fprintf(out, "#endif /* DATABIND_NATIVE_ENABLE_CMETA */\n") < 0)
    failed = 1;
  }
  if (ferror(out)) failed = 1;
  if (stream != NULL) {
    if (fflush(out) != 0) failed = 1;
  } else {
    if (fclose(out) != 0) failed = 1;
    if (failed) (void)remove(path);
  }
  return failed ? -1 : 0;
}

int databind_native_source_ir_write_header(
    const databind_native_source_ir *ir, const char *path) {
  return native_source_ir_render(ir, path, NULL);
}

int databind_native_source_ir_write_stream(
    const databind_native_source_ir *ir, FILE *stream) {
  if (stream == NULL) return -1;
  return native_source_ir_render(ir, NULL, stream);
}
