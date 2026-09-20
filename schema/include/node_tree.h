#ifndef NODE_TREE_H
#define NODE_TREE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dynamic node tree for Mustache data binding.
 *
 * Provides a simple tree data structure that can represent strings, lists,
 * and maps — exactly what the Mustache template engine needs as input.
 */

/* Schema (DSL) Parser */
typedef enum {
  SALTS_NODE_ROOT,
  SALTS_NODE_STRING,
  SALTS_NODE_LIST,
  SALTS_NODE_MAP,

  /* Compatibility aliases for tbe_compiler */
  NODE_ROOT   = SALTS_NODE_ROOT,
  NODE_STRING = SALTS_NODE_STRING,
  NODE_LIST   = SALTS_NODE_LIST,
  NODE_MAP    = SALTS_NODE_MAP
} salts_node_type_t;

typedef struct salts_node_s {
  salts_node_type_t type;
  const char *name;
  union {
    char *string_val;
    struct {
      struct salts_node_s **items;
      size_t count;
      size_t cap;
    } list;
    struct {
      struct salts_node_s **items;
      size_t count;
      size_t cap;
    } map;
  } data;
} salts_node_t;

typedef salts_node_type_t NodeType;
typedef salts_node_t Node;

/** Create a string-valued node. Both @p name and @p val are duplicated.
 *  @return Node pointer on success, NULL on allocation failure. */
Node *create_node_string(const char *name, const char *val);

/** Create an empty list node.
 *  @return Node pointer on success, NULL on allocation failure. */
Node *create_node_list(const char *name);

/** Append @p item to @p list (NODE_LIST).
 *  @return 0 on success, -1 on allocation failure. */
int list_add(Node *list, Node *item);

/** Create an empty map node.
 *  @return Node pointer on success, NULL on allocation failure. */
Node *create_node_map(const char *name);

/** Append @p item to @p map  (NODE_MAP).
 *  @return 0 on success, -1 on allocation failure. */
int map_add(Node *map, Node *item);

/** Recursively free a node and all its children. */
void node_free(Node *node);

#ifdef __cplusplus
}
#endif

#endif /* NODE_TREE_H */
