#include "semantic_ir.h"

#include <string.h>

static const Node *semantic_child(const Node *map, const char *name) {
  size_t i;
  if (map == NULL || map->type != NODE_MAP || name == NULL) return NULL;
  for (i = 0u; i < map->data.map.count; ++i) {
    const Node *child = map->data.map.items[i];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static const char *semantic_text(const Node *map, const char *name) {
  const Node *child = semantic_child(map, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

int databind_semantic_field_build(const Node *root, const Node *field,
                                  databind_semantic_field_ir *out) {
  databind_semantic_field_ir result = {0};
  const char *name;
  const char *declared_type;

  if (root == NULL || field == NULL || out == NULL) return 0;

  name = semantic_text(field, "name");
  declared_type = semantic_text(field, "type");
  if (name == NULL || name[0] == '\0' ||
      declared_type == NULL || declared_type[0] == '\0')
    return 0;

  result.struct_size = sizeof(result);
  result.abi_version = DATABIND_SEMANTIC_IR_ABI_VERSION;
  result.owner_name = semantic_text(field, "owner_name");
  result.name = name;
  result.declared_type = declared_type;

  if (!schema_cmeta_field_resolve(root, field, &result.semantic)) return 0;

  *out = result;
  return 1;
}
