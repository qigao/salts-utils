#include "node_tree.h"
#include "schema_parser_dsl.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NODE_INITIAL_CAPACITY 8
#define NODE_GROWTH_FACTOR 2

Node *create_node_string(const char *name, const char *val) {
  if (!val) return NULL;

  Node *n = (Node *)calloc(1, sizeof(Node));
  if (!n) return NULL;

  n->type = NODE_STRING;
  if (name) {
    n->name = strdup(name);
    if (!n->name) {
      free(n);
      return NULL;
    }
  }

  n->data.string_val = strdup(val);
  if (!n->data.string_val) {
    free((void *)n->name);
    free(n);
    return NULL;
  }

  return n;
}

Node *create_node_list(const char *name) {
  Node *n = (Node *)calloc(1, sizeof(Node));
  if (!n) return NULL;

  n->type = NODE_LIST;
  if (name) {
    n->name = strdup(name);
    if (!n->name) {
      free(n);
      return NULL;
    }
  }
  return n;
}

static int node_add_child(Node *parent, Node *item) {
  Node ***items_ptr;
  size_t *count, *cap;

  if (!parent || !item || (parent->type != NODE_LIST && parent->type != NODE_MAP)) return -1;
  if (parent->type == NODE_LIST) {
    items_ptr = &parent->data.list.items;
    count = &parent->data.list.count;
    cap = &parent->data.list.cap;
  } else {
    items_ptr = &parent->data.map.items;
    count = &parent->data.map.count;
    cap = &parent->data.map.cap;
  }

  if (*count > *cap) return -1;
  if (*count == *cap) {
    const size_t max_cap = SIZE_MAX / sizeof(Node *);
    size_t new_cap;
    if (*cap > max_cap || *cap == max_cap) return -1;
    if (*cap == 0) new_cap = NODE_INITIAL_CAPACITY;
    else if (*cap > max_cap / NODE_GROWTH_FACTOR) new_cap = max_cap;
    else new_cap = *cap * NODE_GROWTH_FACTOR;
    Node **new_items = (Node **)realloc(*items_ptr, new_cap * sizeof(Node *));
    if (!new_items) return -1;
    *items_ptr = new_items;
    *cap = new_cap;
  }
  (*items_ptr)[(*count)++] = item;
  return 0;
}

int list_add(Node *list, Node *item) {
  return node_add_child(list, item);
}

Node *create_node_map(const char *name) {
  Node *n = (Node *)calloc(1, sizeof(Node));
  if (!n) return NULL;

  n->type = NODE_MAP;
  if (name) {
    n->name = strdup(name);
    if (!n->name) {
      free(n);
      return NULL;
    }
  }
  return n;
}

int map_add(Node *map, Node *item) {
  return node_add_child(map, item);
}

void node_free(Node *node) {
  Node *current = node;
  if (!current) return;

  /* During destruction, name stores the parent link after its owned text is released. */
  free((void *)current->name);
  current->name = NULL;
  while (current) {
    Node **items = NULL;
    size_t *count = NULL;
    Node *parent;

    if (current->type == NODE_LIST) {
      items = current->data.list.items;
      count = &current->data.list.count;
    } else if (current->type == NODE_MAP) {
      items = current->data.map.items;
      count = &current->data.map.count;
    }

    if (count != NULL && *count != 0) {
      Node *child = items[--(*count)];
      if (child == NULL) continue;
      free((void *)child->name);
      child->name = (const char *)(void *)current;
      current = child;
      continue;
    }

    parent = (Node *)(void *)current->name;
    if (current->type == NODE_STRING) free(current->data.string_val);
    else if (current->type == NODE_LIST) free(current->data.list.items);
    else if (current->type == NODE_MAP) free(current->data.map.items);
    free(current);
    current = parent;
  }
}
