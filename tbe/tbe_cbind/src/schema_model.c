#include "tbe_cbind_internal.h"

#include "node_tree.h"
#include "schema_parser_dsl.h"
#include "tbe_cbind_capability.h"
#include "tbe_error.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct tbe_cbind_schema_counts {
  size_t record_count;
  size_t enum_count;
  size_t field_count;
} tbe_cbind_schema_counts;

static Node *tbe_cbind_node_child(const Node *parent, const char *name) {
  Node *const *items;
  size_t count;
  size_t index;

  if (parent == NULL || name == NULL) return NULL;
  if (parent->type == NODE_MAP) {
    items = parent->data.map.items;
    count = parent->data.map.count;
  } else if (parent->type == NODE_LIST) {
    items = parent->data.list.items;
    count = parent->data.list.count;
  } else {
    return NULL;
  }
  for (index = 0u; index < count; ++index) {
    Node *child = items[index];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static const char *tbe_cbind_node_string(const Node *parent,
                                         const char *name) {
  Node *child = tbe_cbind_node_child(parent, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

static int tbe_cbind_node_has(const Node *parent, const char *name) {
  return tbe_cbind_node_child(parent, name) != NULL;
}

static uint64_t tbe_cbind_name_hash(const char *text) {
  uint64_t hash = UINT64_C(1469598103934665603);
  while (*text != '\0') {
    hash ^= (unsigned char)*text++;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static int tbe_cbind_hash_capacity(size_t item_count, size_t *out) {
  size_t requested;
  size_t capacity = 8u;
  if (!tbe_cbind_size_mul(item_count, 2u, &requested)) return 0;
  if (requested < capacity) requested = capacity;
  while (capacity < requested) {
    if (capacity > SIZE_MAX / 2u) return 0;
    capacity *= 2u;
  }
  *out = capacity;
  return 1;
}

static int tbe_cbind_portable_semantic_name(const char *name) {
  size_t index;
  if (name == NULL ||
      !((name[0] >= 'A' && name[0] <= 'Z') ||
        (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
    return 0;
  for (index = 1u; name[index] != '\0'; ++index) {
    const char ch = name[index];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
      return 0;
  }
  return 1;
}

int tbe_cbind_c_identifier_valid(const char *name) {
  static const char *const keywords[] = {
      "auto", "break", "case", "char", "const", "continue", "default",
      "do", "double", "else", "enum", "extern", "float", "for",
      "goto", "if", "inline", "int", "long", "register", "restrict",
      "return", "short", "signed", "sizeof", "static", "struct",
      "switch", "typedef", "union", "unsigned", "void", "volatile",
      "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex",
      "_Generic", "_Imaginary", "_Noreturn", "_Static_assert",
      "_Thread_local"};
  size_t index;
  if (name == NULL ||
      !((name[0] >= 'A' && name[0] <= 'Z') ||
        (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
    return 0;
  for (index = 1u; name[index] != '\0'; ++index) {
    const char ch = name[index];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') || ch == '_'))
      return 0;
  }
  for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); ++index)
    if (strcmp(name, keywords[index]) == 0) return 0;
  return 1;
}

static tbe_cbind_status tbe_cbind_name_limit(
    tbe_cbind_build_context *context, const char *name, const char *path) {
  if (name == NULL || strlen(name) > context->options->max_name_bytes) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, path, "schema name exceeds max_name_bytes");
  }
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_count_schema(
    tbe_cbind_build_context *context, const Node *root,
    tbe_cbind_schema_counts *counts) {
  static const char *const record_lists[] = {"composites", "groups", "messages"};
  size_t list_index;
  *counts = (tbe_cbind_schema_counts){0};
  for (list_index = 0u;
       list_index < sizeof(record_lists) / sizeof(record_lists[0]);
       ++list_index) {
    Node *records = tbe_cbind_node_child(root, record_lists[list_index]);
    size_t record_index;
    if (records == NULL || records->type != NODE_LIST) continue;
    if (!tbe_cbind_size_add(counts->record_count, records->data.list.count,
                            &counts->record_count)) {
      return tbe_cbind_set_error(
          context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, 0u,
          CMETA_OK, record_lists[list_index], "schema exceeds max_types");
    }
    for (record_index = 0u; record_index < records->data.list.count;
         ++record_index) {
      Node *fields = tbe_cbind_node_child(records->data.list.items[record_index],
                                          "fields");
      size_t count = fields != NULL && fields->type == NODE_LIST
                         ? fields->data.list.count
                         : 0u;
      if (!tbe_cbind_size_add(counts->field_count, count,
                              &counts->field_count) ||
          counts->field_count > context->options->max_fields) {
        return tbe_cbind_set_error(
            context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA,
            record_index, CMETA_OK, record_lists[list_index],
            "schema exceeds max_fields");
      }
    }
  }
  {
    Node *enums = tbe_cbind_node_child(root, "enums");
    size_t enum_index;
    if (enums != NULL && enums->type == NODE_LIST) {
      counts->enum_count = enums->data.list.count;
      for (enum_index = 0u; enum_index < counts->enum_count; ++enum_index) {
        Node *items = tbe_cbind_node_child(enums->data.list.items[enum_index],
                                          "items");
        size_t count = items != NULL && items->type == NODE_LIST
                           ? items->data.list.count
                           : 0u;
        if (!tbe_cbind_size_add(counts->field_count, count,
                                &counts->field_count) ||
            counts->field_count > context->options->max_fields) {
          return tbe_cbind_set_error(
              context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA,
              enum_index, CMETA_OK, "enums", "schema exceeds max_fields");
        }
      }
    }
  }
  {
    size_t declaration_count;
    if (!tbe_cbind_size_add(counts->record_count, counts->enum_count,
                            &declaration_count) ||
        declaration_count > context->options->max_types) {
      return tbe_cbind_set_error(
          context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, 0u,
          CMETA_OK, "(declaration)", "schema exceeds max_types");
    }
  }
  return TBE_CBIND_OK;
}

static int tbe_cbind_list_nonempty(const Node *root, const char *name) {
  Node *list = tbe_cbind_node_child(root, name);
  return list != NULL && list->type == NODE_LIST && list->data.list.count != 0u;
}

static tbe_cbind_status tbe_cbind_reject_global_unsupported(
    tbe_cbind_build_context *context, const Node *root) {
  Node *schema = tbe_cbind_node_child(root, "schema");
  Node *attributes = tbe_cbind_node_child(schema, "attributes");
  if (tbe_cbind_list_nonempty(root, "unions")) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK,
        "(declaration)", "union declarations are unsupported");
  }
  if (attributes != NULL && attributes->type == NODE_LIST &&
      attributes->data.list.count != 0u) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK,
        "schema", "schema attributes are unsupported in v1");
  }
  return TBE_CBIND_OK;
}

static tbe_cbind_semantic_enum *tbe_cbind_find_enum(
    const tbe_cbind_schema_model *model, const char *name) {
  size_t slot;
  if (model == NULL || name == NULL || model->enum_slot_count == 0u)
    return NULL;
  slot = (size_t)tbe_cbind_name_hash(name) & (model->enum_slot_count - 1u);
  while (model->enum_slots[slot] != NULL) {
    if (strcmp(model->enum_slots[slot]->name, name) == 0)
      return model->enum_slots[slot];
    slot = (slot + 1u) & (model->enum_slot_count - 1u);
  }
  return NULL;
}

static tbe_cbind_status tbe_cbind_insert_type(
    tbe_cbind_build_context *context, tbe_cbind_schema_model *model,
    tbe_cbind_semantic_type *type) {
  size_t slot;
  if (tbe_cbind_capability_find(type->name) != NULL ||
      tbe_cbind_find_enum(model, type->name) != NULL) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, type->name, "schema type name collides with another type");
  }
  slot = (size_t)tbe_cbind_name_hash(type->name) &
                (model->type_slot_count - 1u);
  while (model->type_slots[slot] != NULL) {
    if (strcmp(model->type_slots[slot]->name, type->name) == 0) {
      return tbe_cbind_set_error(
          context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
          CMETA_OK, type->name, "duplicate schema type name");
    }
    slot = (slot + 1u) & (model->type_slot_count - 1u);
  }
  model->type_slots[slot] = type;
  return TBE_CBIND_OK;
}

static tbe_cbind_semantic_type *tbe_cbind_find_type(
    const tbe_cbind_schema_model *model, const char *name) {
  size_t slot;
  if (model == NULL || name == NULL || model->type_slot_count == 0u) return NULL;
  slot = (size_t)tbe_cbind_name_hash(name) & (model->type_slot_count - 1u);
  while (model->type_slots[slot] != NULL) {
    if (strcmp(model->type_slots[slot]->name, name) == 0)
      return model->type_slots[slot];
    slot = (slot + 1u) & (model->type_slot_count - 1u);
  }
  return NULL;
}

static tbe_cbind_status tbe_cbind_insert_enum(
    tbe_cbind_build_context *context, tbe_cbind_schema_model *model,
    tbe_cbind_semantic_enum *enum_type) {
  size_t slot;
  if (tbe_cbind_capability_find(enum_type->name) != NULL ||
      tbe_cbind_find_type(model, enum_type->name) != NULL) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, enum_type->name,
        "enum name collides with another schema type");
  }
  slot = (size_t)tbe_cbind_name_hash(enum_type->name) &
         (model->enum_slot_count - 1u);
  while (model->enum_slots[slot] != NULL) {
    if (strcmp(model->enum_slots[slot]->name, enum_type->name) == 0) {
      return tbe_cbind_set_error(
          context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
          CMETA_OK, enum_type->name, "duplicate schema enum name");
    }
    slot = (slot + 1u) & (model->enum_slot_count - 1u);
  }
  model->enum_slots[slot] = enum_type;
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_insert_field_name(
    tbe_cbind_build_context *context, const char **slots, size_t slot_count,
    const char *name, const char *path, const char *message);

typedef struct tbe_cbind_enum_value_slot {
  int64_t value;
  int occupied;
} tbe_cbind_enum_value_slot;

static size_t tbe_cbind_enum_value_hash(int64_t value) {
  uint64_t bits = (uint64_t)value;
  bits ^= bits >> 33u;
  bits *= UINT64_C(0xff51afd7ed558ccd);
  bits ^= bits >> 33u;
  return (size_t)bits;
}

static tbe_cbind_status tbe_cbind_enum_parse_value(
    tbe_cbind_build_context *context, const char *text,
    const tbe_cbind_capability *underlying, const char *path, int64_t *out) {
  unsigned long long parsed;
  unsigned long long maximum;
  char *end = NULL;
  if (text == NULL || underlying == NULL || out == NULL ||
      underlying->kind != TBE_CBIND_SCALAR_INTEGER || underlying->bits == 0u ||
      underlying->bits > 64u) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, path, "enum value or underlying integer type is invalid");
  }
  errno = 0;
  parsed = strtoull(text, &end, 0);
  if (errno == ERANGE || end == text || end == NULL || *end != '\0' ||
      parsed > (unsigned long long)INT64_MAX) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, path, "enum value is outside the supported int64 domain");
  }
  if (underlying->is_signed) {
    maximum = underlying->bits == 64u
                  ? (unsigned long long)INT64_MAX
                  : (UINT64_C(1) << (underlying->bits - 1u)) - 1u;
  } else {
    maximum = underlying->bits == 64u
                  ? (unsigned long long)INT64_MAX
                  : (UINT64_C(1) << underlying->bits) - 1u;
  }
  if (parsed > maximum) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, path, "enum value does not fit its underlying integer type");
  }
  *out = (int64_t)parsed;
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_enum_symbol_copy(
    tbe_cbind_build_context *context, const char *enum_name,
    const char *item_name, const char *path, char **out) {
  size_t enum_size;
  size_t item_size;
  size_t symbol_chars;
  size_t allocation_size;
  char *symbol;
  *out = NULL;
  enum_size = strlen(enum_name);
  item_size = strlen(item_name);
  if (!tbe_cbind_size_add(enum_size, 1u, &symbol_chars) ||
      !tbe_cbind_size_add(symbol_chars, item_size, &symbol_chars) ||
      symbol_chars > context->options->max_name_bytes ||
      !tbe_cbind_size_add(symbol_chars, 1u, &allocation_size)) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, path, "derived enum symbol exceeds max_name_bytes");
  }
  symbol = tbe_cbind_alloc_array(context, allocation_size, sizeof(*symbol));
  if (symbol == NULL) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OUT_OF_MEMORY, path, "enum symbol allocation failed");
  }
  memcpy(symbol, enum_name, enum_size);
  symbol[enum_size] = '_';
  memcpy(symbol + enum_size + 1u, item_name, item_size + 1u);
  *out = symbol;
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_extract_enum(
    tbe_cbind_build_context *context, const Node *enum_node,
    tbe_cbind_schema_model *model, tbe_cbind_semantic_enum *enum_type) {
  const char *name = tbe_cbind_node_string(enum_node, "enum_name");
  const char *underlying_name =
      tbe_cbind_node_string(enum_node, "underlying_type");
  Node *attributes = tbe_cbind_node_child(enum_node, "attributes");
  Node *items = tbe_cbind_node_child(enum_node, "items");
  const char **name_slots = NULL;
  tbe_cbind_enum_value_slot *value_slots = NULL;
  size_t slot_count = 0u;
  size_t index;
  char declaration_path[256];
  tbe_cbind_status status;
  (void)snprintf(declaration_path, sizeof(declaration_path), "%s.(declaration)",
                 name != NULL ? name : "?");
  if (name == NULL) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, declaration_path, "enum metadata is incomplete");
  }
  if (tbe_cbind_node_has(enum_node, "is_flags")) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, declaration_path, "flags declarations are unsupported");
  }
  if (attributes != NULL && attributes->type == NODE_LIST &&
      attributes->data.list.count != 0u) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, declaration_path, "enum attributes are unsupported");
  }
  status = tbe_cbind_name_limit(context, name, declaration_path);
  if (status != TBE_CBIND_OK) return status;
  if (underlying_name == NULL) underlying_name = "int32";
  status = tbe_cbind_name_limit(context, underlying_name, declaration_path);
  if (status != TBE_CBIND_OK) return status;
  enum_type->underlying = tbe_cbind_capability_find(underlying_name);
  if (enum_type->underlying == NULL ||
      enum_type->underlying->kind != TBE_CBIND_SCALAR_INTEGER) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, declaration_path,
        "enum underlying type must be a supported fixed integer");
  }
  enum_type->name = tbe_cbind_strdup(context, name);
  if (enum_type->name == NULL) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OUT_OF_MEMORY, declaration_path, "enum name allocation failed");
  }
  status = tbe_cbind_insert_enum(context, model, enum_type);
  if (status != TBE_CBIND_OK) return status;
  enum_type->item_count = items != NULL && items->type == NODE_LIST
                              ? items->data.list.count
                              : 0u;
  if (enum_type->item_count == 0u) return TBE_CBIND_OK;
  enum_type->items = tbe_cbind_alloc_array(
      context, enum_type->item_count, sizeof(*enum_type->items));
  if (enum_type->items == NULL) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OUT_OF_MEMORY, declaration_path, "enum item allocation failed");
  }
  if (!tbe_cbind_hash_capacity(enum_type->item_count, &slot_count)) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, declaration_path, "enum item index size overflow");
  }
  name_slots = tbe_cbind_alloc_array(context, slot_count,
                                     sizeof(*name_slots));
  value_slots = tbe_cbind_alloc_array(context, slot_count,
                                      sizeof(*value_slots));
  if (name_slots == NULL || value_slots == NULL) {
    status = tbe_cbind_set_error(
        context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OUT_OF_MEMORY, declaration_path,
        "enum item index allocation failed");
    goto cleanup;
  }
  for (index = 0u; index < enum_type->item_count; ++index) {
    const Node *item_node = items->data.list.items[index];
    const char *item_name = tbe_cbind_node_string(item_node, "name");
    const char *value_text = tbe_cbind_node_string(item_node, "value");
    tbe_cbind_semantic_enum_item *item = &enum_type->items[index];
    size_t value_slot;
    char path[256];
    (void)snprintf(path, sizeof(path), "%s.%s", name,
                   item_name != NULL ? item_name : "?");
    if (item_name == NULL || value_text == NULL) {
      status = tbe_cbind_set_error(
          context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, index,
          CMETA_OK, path, "enum item metadata is incomplete");
      goto cleanup;
    }
    status = tbe_cbind_name_limit(context, item_name, path);
    if (status != TBE_CBIND_OK) goto cleanup;
    status = tbe_cbind_insert_field_name(
        context, name_slots, slot_count, item_name, path,
        "enum item names are duplicated");
    if (status != TBE_CBIND_OK) goto cleanup;
    status = tbe_cbind_enum_parse_value(context, value_text,
                                        enum_type->underlying, path,
                                        &item->value);
    if (status != TBE_CBIND_OK) goto cleanup;
    value_slot = tbe_cbind_enum_value_hash(item->value) & (slot_count - 1u);
    while (value_slots[value_slot].occupied) {
      if (value_slots[value_slot].value == item->value) {
        status = tbe_cbind_set_error(
            context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, index,
            CMETA_OK, path, "enum item values are duplicated");
        goto cleanup;
      }
      value_slot = (value_slot + 1u) & (slot_count - 1u);
    }
    value_slots[value_slot].occupied = 1;
    value_slots[value_slot].value = item->value;
    item->text = tbe_cbind_strdup(context, item_name);
    if (item->text == NULL) {
      status = tbe_cbind_set_error(
          context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_SCHEMA, index,
          CMETA_OUT_OF_MEMORY, path, "enum item text allocation failed");
      goto cleanup;
    }
    status = tbe_cbind_enum_symbol_copy(context, name, item_name, path,
                                         &item->symbol);
    if (status != TBE_CBIND_OK) goto cleanup;
  }
  status = TBE_CBIND_OK;
cleanup:
  tbe_cbind_free(context, value_slots);
  tbe_cbind_free(context, name_slots);
  return status;
}

static tbe_cbind_status tbe_cbind_extract_enums(
    tbe_cbind_build_context *context, const Node *root,
    tbe_cbind_schema_model *model) {
  Node *enums = tbe_cbind_node_child(root, "enums");
  size_t index;
  if (enums == NULL || enums->type != NODE_LIST) return TBE_CBIND_OK;
  for (index = 0u; index < enums->data.list.count; ++index) {
    tbe_cbind_status status = tbe_cbind_extract_enum(
        context, enums->data.list.items[index], model, &model->enums[index]);
    if (status != TBE_CBIND_OK) return status;
  }
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_extract_attributes(
    tbe_cbind_build_context *context, const Node *field,
    const char **semantic_name, const char **native_name, const char *path) {
  Node *attributes = tbe_cbind_node_child(field, "attributes");
  size_t semantic_count = 0u;
  size_t native_count = 0u;
  size_t index;
  if (attributes == NULL || attributes->type != NODE_LIST) return TBE_CBIND_OK;
  for (index = 0u; index < attributes->data.list.count; ++index) {
    Node *attribute = attributes->data.list.items[index];
    const char *name = tbe_cbind_node_string(attribute, "name");
    const char *value = tbe_cbind_node_string(attribute, "value");
    if (name != NULL && strcmp(name, "name") == 0) {
      ++semantic_count;
      *semantic_name = value;
    } else if (name != NULL && strcmp(name, "c") == 0) {
      ++native_count;
      *native_name = value;
    } else {
      return tbe_cbind_set_error(
          context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, index,
          CMETA_OK, path, "field attribute is unsupported in v1");
    }
  }
  if (semantic_count > 1u || !tbe_cbind_portable_semantic_name(*semantic_name)) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, path,
        "name must occur once and match [A-Za-z_][A-Za-z0-9_-]*");
  }
  if (native_count > 1u || !tbe_cbind_c_identifier_valid(*native_name)) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, path, "c must occur once and name a valid C identifier");
  }
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_insert_field_name(
    tbe_cbind_build_context *context, const char **slots, size_t slot_count,
    const char *name, const char *path, const char *message) {
  size_t slot = (size_t)tbe_cbind_name_hash(name) & (slot_count - 1u);
  while (slots[slot] != NULL) {
    if (strcmp(slots[slot], name) == 0) {
      return tbe_cbind_set_error(
          context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, 0u,
          CMETA_OK, path, message);
    }
    slot = (slot + 1u) & (slot_count - 1u);
  }
  slots[slot] = name;
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_extract_field(
    tbe_cbind_build_context *context, const Node *field_node,
    tbe_cbind_schema_model *model, tbe_cbind_semantic_type *owner,
    size_t field_index,
    const char **semantic_slots, const char **native_slots,
    size_t slot_count) {
  tbe_cbind_semantic_field *field = &owner->fields[field_index];
  const char *name = tbe_cbind_node_string(field_node, "name");
  const char *type_name = tbe_cbind_node_string(field_node, "type");
  const tbe_cbind_capability *capability;
  const char *semantic_name = name;
  const char *native_name = name;
  char path[256];
  tbe_cbind_status status;
  (void)snprintf(path, sizeof(path), "%s.%s", owner->name != NULL ? owner->name : "?",
                 name != NULL ? name : "?");
  if (name == NULL || type_name == NULL) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_SCHEMA, field_index,
        CMETA_OK, path, "field metadata is incomplete");
  }
  if (tbe_cbind_node_has(field_node, "is_optional") ||
      tbe_cbind_node_has(field_node, "has_default") ||
      tbe_cbind_node_has(field_node, "is_group_field") ||
      tbe_cbind_node_has(field_node, "is_collection") ||
      tbe_cbind_node_has(field_node, "is_bytes")) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, field_index,
        CMETA_OK, path,
        "optional, default, bytes, group collection, and container fields are unsupported");
  }
  status = tbe_cbind_extract_attributes(context, field_node, &semantic_name,
                                        &native_name, path);
  if (status != TBE_CBIND_OK) return status;
  if ((status = tbe_cbind_name_limit(context, name, path)) != TBE_CBIND_OK ||
      (status = tbe_cbind_name_limit(context, type_name, path)) != TBE_CBIND_OK ||
      (status = tbe_cbind_name_limit(context, semantic_name, path)) != TBE_CBIND_OK ||
      (status = tbe_cbind_name_limit(context, native_name, path)) != TBE_CBIND_OK)
    return status;
  status = tbe_cbind_insert_field_name(
      context, semantic_slots, slot_count, semantic_name, path,
      "effective semantic field names collide");
  if (status != TBE_CBIND_OK) return status;
  status = tbe_cbind_insert_field_name(
      context, native_slots, slot_count, native_name, path,
      "effective native member names collide");
  if (status != TBE_CBIND_OK) return status;
  field->name = tbe_cbind_strdup(context, name);
  field->semantic_name = tbe_cbind_strdup(context, semantic_name);
  field->native_name = tbe_cbind_strdup(context, native_name);
  field->type_name = tbe_cbind_strdup(context, type_name);
  if (field->name == NULL || field->semantic_name == NULL ||
      field->native_name == NULL || field->type_name == NULL) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_SCHEMA, field_index,
        CMETA_OUT_OF_MEMORY, path, "field model allocation failed");
  }
  capability = tbe_cbind_capability_find(type_name);
  field->capability = capability;
  field->enum_type = capability == NULL ? tbe_cbind_find_enum(model, type_name)
                                        : NULL;
  field->kind = capability != NULL
                    ? TBE_CBIND_SEMANTIC_SCALAR
                    : field->enum_type != NULL ? TBE_CBIND_SEMANTIC_ENUM
                                               : TBE_CBIND_SEMANTIC_RECORD;
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_extract_record(
    tbe_cbind_build_context *context, const Node *record_node,
    tbe_cbind_schema_model *model, tbe_cbind_semantic_type *type) {
  Node *fields = tbe_cbind_node_child(record_node, "fields");
  Node *attributes = tbe_cbind_node_child(record_node, "attributes");
  const char **semantic_slots = NULL;
  const char **native_slots = NULL;
  size_t slot_count = 0u;
  size_t index;
  tbe_cbind_status status = TBE_CBIND_OK;
  if (attributes != NULL && attributes->type == NODE_LIST &&
      attributes->data.list.count != 0u) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK,
        type->name, "record attributes are unsupported in v1");
  }
  type->field_count = fields != NULL && fields->type == NODE_LIST
                          ? fields->data.list.count
                          : 0u;
  if (type->field_count == 0u) return TBE_CBIND_OK;
  type->fields = tbe_cbind_alloc_array(context, type->field_count,
                                       sizeof(*type->fields));
  if (type->fields == NULL)
    return tbe_cbind_set_error(context, TBE_CBIND_OUT_OF_MEMORY,
                               TBE_CBIND_PHASE_SCHEMA, 0u,
                               CMETA_OUT_OF_MEMORY, type->name,
                               "field model allocation failed");
  if (!tbe_cbind_hash_capacity(type->field_count, &slot_count))
    return tbe_cbind_set_error(context, TBE_CBIND_LIMIT_EXCEEDED,
                               TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK,
                               type->name, "field index size overflow");
  semantic_slots = tbe_cbind_alloc_array(context, slot_count,
                                         sizeof(*semantic_slots));
  native_slots = tbe_cbind_alloc_array(context, slot_count,
                                       sizeof(*native_slots));
  if (semantic_slots == NULL || native_slots == NULL) {
    status = tbe_cbind_set_error(context, TBE_CBIND_OUT_OF_MEMORY,
                                 TBE_CBIND_PHASE_SCHEMA, 0u,
                                 CMETA_OUT_OF_MEMORY, type->name,
                                 "field index allocation failed");
    goto cleanup;
  }
  for (index = 0u; index < type->field_count; ++index) {
    status = tbe_cbind_extract_field(
        context, fields->data.list.items[index], model, type, index,
        semantic_slots, native_slots, slot_count);
    if (status != TBE_CBIND_OK) goto cleanup;
  }
cleanup:
  tbe_cbind_free(context, native_slots);
  tbe_cbind_free(context, semantic_slots);
  return status;
}

static tbe_cbind_status tbe_cbind_extract_types(
    tbe_cbind_build_context *context, const Node *root,
    tbe_cbind_schema_model *model) {
  static const char *const record_lists[] = {"composites", "groups", "messages"};
  size_t output_index = 0u;
  size_t list_index;
  for (list_index = 0u;
       list_index < sizeof(record_lists) / sizeof(record_lists[0]);
       ++list_index) {
    Node *records = tbe_cbind_node_child(root, record_lists[list_index]);
    size_t record_index;
    if (records == NULL || records->type != NODE_LIST) continue;
    for (record_index = 0u; record_index < records->data.list.count;
         ++record_index, ++output_index) {
      tbe_cbind_semantic_type *type = &model->types[output_index];
      const char *name = tbe_cbind_node_string(records->data.list.items[record_index],
                                               "name");
      tbe_cbind_status status = tbe_cbind_name_limit(context, name, name);
      if (status != TBE_CBIND_OK) return status;
      type->name = tbe_cbind_strdup(context, name);
      if (type->name == NULL)
        return tbe_cbind_set_error(context, TBE_CBIND_OUT_OF_MEMORY,
                                   TBE_CBIND_PHASE_SCHEMA, 0u,
                                   CMETA_OUT_OF_MEMORY, name,
                                   "type name allocation failed");
      status = tbe_cbind_insert_type(context, model, type);
      if (status != TBE_CBIND_OK) return status;
      status = tbe_cbind_extract_record(context,
                                        records->data.list.items[record_index],
                                        model, type);
      if (status != TBE_CBIND_OK) return status;
    }
  }
  return TBE_CBIND_OK;
}

static tbe_cbind_status tbe_cbind_resolve_type(
    tbe_cbind_build_context *context, tbe_cbind_schema_model *model,
    tbe_cbind_semantic_type *type, size_t depth) {
  size_t remaining_depth;
  size_t index;
  if (depth == 0u || depth > context->options->max_depth ||
      !tbe_cbind_size_add(context->options->max_depth - depth, 1u,
                          &remaining_depth)) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_CAPACITY_EXCEEDED, type->name,
        "schema nesting exceeds max_depth");
  }
  if (type->visit_state == 2u) {
    if (type->height > remaining_depth)
      return tbe_cbind_set_error(
          context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, 0u,
          CMETA_CAPACITY_EXCEEDED, type->name,
          "schema nesting exceeds max_depth");
    return TBE_CBIND_OK;
  }
  if (type->visit_state == 1u) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_OK, type->name, "recursive by-value record graph is unsupported");
  }
  type->visit_state = 1u;
  type->height = 1u;
  for (index = 0u; index < type->field_count; ++index) {
    tbe_cbind_semantic_field *field = &type->fields[index];
    if (field->kind == TBE_CBIND_SEMANTIC_RECORD) {
      char path[256];
      size_t candidate_height;
      size_t child_depth;
      tbe_cbind_status status;
      field->record_type = tbe_cbind_find_type(model, field->type_name);
      (void)snprintf(path, sizeof(path), "%s.%s", type->name, field->name);
      if (field->record_type == NULL) {
        return tbe_cbind_set_error(
            context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_SCHEMA, index,
            CMETA_OK, path, "field type is not in the v1 support matrix");
      }
      if (!tbe_cbind_size_add(depth, 1u, &child_depth) ||
          child_depth > context->options->max_depth) {
        return tbe_cbind_set_error(
            context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, index,
            CMETA_CAPACITY_EXCEEDED, path,
            "schema nesting exceeds max_depth");
      }
      status = tbe_cbind_resolve_type(context, model, field->record_type,
                                      child_depth);
      if (status != TBE_CBIND_OK) return status;
      if (!tbe_cbind_size_add(field->record_type->height, 1u,
                              &candidate_height)) {
        return tbe_cbind_set_error(
            context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, index,
            CMETA_CAPACITY_EXCEEDED, path, "schema nesting height overflow");
      }
      if (candidate_height > type->height) type->height = candidate_height;
    }
  }
  if (type->height > remaining_depth) {
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_SCHEMA, 0u,
        CMETA_CAPACITY_EXCEEDED, type->name,
        "schema nesting exceeds max_depth");
  }
  type->visit_state = 2u;
  return TBE_CBIND_OK;
}

tbe_cbind_status tbe_cbind_schema_model_build(
    tbe_cbind_build_context *context, const char *schema_text,
    size_t schema_size, const char *type_name,
    tbe_cbind_schema_model **out_model) {
  tbe_cbind_schema_model *model = NULL;
  tbe_cbind_schema_counts counts;
  tbe_error_t parse_error;
  Node *root = NULL;
  size_t index;
  tbe_cbind_status status;
  *out_model = NULL;
  root = create_node_map("root");
  if (root == NULL)
    return tbe_cbind_set_error(context, TBE_CBIND_OUT_OF_MEMORY,
                               TBE_CBIND_PHASE_PARSE, 0u,
                               CMETA_OUT_OF_MEMORY, NULL,
                               "schema root allocation failed");
  tbe_error_init(&parse_error);
  if (parse_schema(schema_text, schema_size, root, &parse_error) != 0) {
    status = tbe_cbind_set_error(
        context,
        parse_error.code == TBE_ERR_OUT_OF_MEMORY ? TBE_CBIND_OUT_OF_MEMORY
                                                  : TBE_CBIND_SCHEMA_ERROR,
        TBE_CBIND_PHASE_PARSE, 0u,
        parse_error.code == TBE_ERR_OUT_OF_MEMORY ? CMETA_OUT_OF_MEMORY
                                                  : CMETA_OK,
        NULL, parse_error.message);
    if (context->error != NULL) {
      context->error->line = parse_error.line;
      context->error->column = parse_error.column;
    }
    node_free(root);
    return status;
  }
  status = tbe_cbind_reject_global_unsupported(context, root);
  if (status != TBE_CBIND_OK) goto cleanup;
  status = tbe_cbind_count_schema(context, root, &counts);
  if (status != TBE_CBIND_OK) goto cleanup;
  model = tbe_cbind_alloc_array(context, 1u, sizeof(*model));
  if (model == NULL) {
    status = tbe_cbind_set_error(context, TBE_CBIND_OUT_OF_MEMORY,
                                 TBE_CBIND_PHASE_SCHEMA, 0u,
                                 CMETA_OUT_OF_MEMORY, NULL,
                                 "schema model allocation failed");
    goto cleanup;
  }
  model->allocator = context->allocator;
  model->type_count = counts.record_count;
  model->enum_count = counts.enum_count;
  if (!tbe_cbind_hash_capacity(model->type_count, &model->type_slot_count)) {
    status = tbe_cbind_set_error(context, TBE_CBIND_LIMIT_EXCEEDED,
                                 TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK,
                                 NULL, "type index size overflow");
    goto cleanup;
  }
  if (!tbe_cbind_hash_capacity(model->enum_count, &model->enum_slot_count)) {
    status = tbe_cbind_set_error(context, TBE_CBIND_LIMIT_EXCEEDED,
                                 TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK,
                                 NULL, "enum index size overflow");
    goto cleanup;
  }
  model->types = tbe_cbind_alloc_array(context, model->type_count,
                                       sizeof(*model->types));
  model->type_slots = tbe_cbind_alloc_array(context, model->type_slot_count,
                                            sizeof(*model->type_slots));
  model->enums = tbe_cbind_alloc_array(context, model->enum_count,
                                       sizeof(*model->enums));
  model->enum_slots = tbe_cbind_alloc_array(context, model->enum_slot_count,
                                            sizeof(*model->enum_slots));
  if ((model->type_count != 0u && model->types == NULL) ||
      model->type_slots == NULL ||
      (model->enum_count != 0u && model->enums == NULL) ||
      model->enum_slots == NULL) {
    status = tbe_cbind_set_error(context, TBE_CBIND_OUT_OF_MEMORY,
                                 TBE_CBIND_PHASE_SCHEMA, 0u,
                                 CMETA_OUT_OF_MEMORY, NULL,
                                 "type model allocation failed");
    goto cleanup;
  }
  status = tbe_cbind_extract_enums(context, root, model);
  if (status != TBE_CBIND_OK) goto cleanup;
  status = tbe_cbind_extract_types(context, root, model);
  if (status != TBE_CBIND_OK) goto cleanup;
  for (index = 0u; index < model->type_count; ++index) {
    status = tbe_cbind_resolve_type(context, model, &model->types[index], 1u);
    if (status != TBE_CBIND_OK) goto cleanup;
  }
  model->root = tbe_cbind_find_type(model, type_name);
  if (model->root == NULL) {
    status = tbe_cbind_set_error(context, TBE_CBIND_TYPE_NOT_FOUND,
                                 TBE_CBIND_PHASE_SCHEMA, 0u, CMETA_OK,
                                 type_name, "requested schema type was not found");
    goto cleanup;
  }
  *out_model = model;
  model = NULL;
  status = TBE_CBIND_OK;
cleanup:
  node_free(root);
  if (model != NULL) tbe_cbind_schema_model_destroy(model);
  return status;
}

void tbe_cbind_schema_model_destroy(tbe_cbind_schema_model *model) {
  size_t enum_index;
  size_t type_index;
  tbe_cbind_allocator allocator;
  if (model == NULL) return;
  allocator = model->allocator;
  if (model->types != NULL) {
    for (type_index = 0u; type_index < model->type_count; ++type_index) {
      tbe_cbind_semantic_type *type = &model->types[type_index];
      size_t field_index;
      if (type->fields != NULL) {
        for (field_index = 0u; field_index < type->field_count; ++field_index) {
          tbe_cbind_semantic_field *field = &type->fields[field_index];
          allocator.free_fn(allocator.context, field->type_name);
          allocator.free_fn(allocator.context, field->native_name);
          allocator.free_fn(allocator.context, field->semantic_name);
          allocator.free_fn(allocator.context, field->name);
        }
      }
      allocator.free_fn(allocator.context, type->fields);
      allocator.free_fn(allocator.context, type->name);
    }
  }
  if (model->enums != NULL) {
    for (enum_index = 0u; enum_index < model->enum_count; ++enum_index) {
      tbe_cbind_semantic_enum *enum_type = &model->enums[enum_index];
      size_t item_index;
      if (enum_type->items != NULL) {
        for (item_index = 0u; item_index < enum_type->item_count;
             ++item_index) {
          allocator.free_fn(allocator.context,
                            enum_type->items[item_index].text);
          allocator.free_fn(allocator.context,
                            enum_type->items[item_index].symbol);
        }
      }
      allocator.free_fn(allocator.context, enum_type->items);
      allocator.free_fn(allocator.context, enum_type->name);
    }
  }
  allocator.free_fn(allocator.context, model->enum_slots);
  allocator.free_fn(allocator.context, model->enums);
  allocator.free_fn(allocator.context, model->type_slots);
  allocator.free_fn(allocator.context, model->types);
  allocator.free_fn(allocator.context, model);
}
