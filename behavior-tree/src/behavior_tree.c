#include <salts/behavior_tree.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct salts_bt_node {
  salts_bt_node_kind kind;
  salts_bt_node_status last_status;
  salts_bt_leaf_fn handler;
  void *user;
  salts_bt_node_id parent;
  salts_bt_node_id first_child;
  salts_bt_node_id last_child;
  salts_bt_node_id next_sibling;
  salts_bt_node_id cursor;
  size_t depth;
  size_t child_count;
} salts_bt_node;

typedef struct salts_bt_impl {
  salts_bt_node *nodes; /* index zero is reserved as SALTS_BT_INVALID_NODE_ID */
  size_t max_nodes;
  size_t max_depth;
  size_t node_count;
  salts_bt_node_id root;
  bool ticking;
} salts_bt_impl;

static salts_bt_impl *tree_impl(salts_bt_tree *tree) {
  return tree != NULL ? (salts_bt_impl *)tree->impl : NULL;
}

static const salts_bt_impl *tree_const_impl(const salts_bt_tree *tree) {
  return tree != NULL ? (const salts_bt_impl *)tree->impl : NULL;
}

static bool node_id_valid(const salts_bt_impl *impl, salts_bt_node_id id) {
  return impl != NULL && id != SALTS_BT_INVALID_NODE_ID &&
         (size_t)id <= impl->node_count;
}

static salts_bt_node *get_node(salts_bt_impl *impl, salts_bt_node_id id) {
  return node_id_valid(impl, id) ? &impl->nodes[id] : NULL;
}

static bool node_kind_valid(salts_bt_node_kind kind) {
  return kind == SALTS_BT_NODE_LEAF ||
         kind == SALTS_BT_NODE_SEQUENCE ||
         kind == SALTS_BT_NODE_FALLBACK ||
         kind == SALTS_BT_NODE_INVERTER ||
         kind == SALTS_BT_NODE_FORCE_SUCCESS ||
         kind == SALTS_BT_NODE_FORCE_FAILURE;
}

static bool node_kind_accepts_children(salts_bt_node_kind kind) {
  return kind == SALTS_BT_NODE_SEQUENCE ||
         kind == SALTS_BT_NODE_FALLBACK ||
         kind == SALTS_BT_NODE_INVERTER ||
         kind == SALTS_BT_NODE_FORCE_SUCCESS ||
         kind == SALTS_BT_NODE_FORCE_FAILURE;
}

static bool node_kind_is_single_child(salts_bt_node_kind kind) {
  return kind == SALTS_BT_NODE_INVERTER ||
         kind == SALTS_BT_NODE_FORCE_SUCCESS ||
         kind == SALTS_BT_NODE_FORCE_FAILURE;
}

static void reset_node_state(salts_bt_impl *impl, salts_bt_node_id id) {
  salts_bt_node *node = get_node(impl, id);
  salts_bt_node_id child;
  if (node == NULL) return;
  node->cursor = SALTS_BT_INVALID_NODE_ID;
  node->last_status = SALTS_BT_FAILURE;
  child = node->first_child;
  while (child != SALTS_BT_INVALID_NODE_ID) {
    salts_bt_node *child_node = get_node(impl, child);
    salts_bt_node_id next;
    if (child_node == NULL) break;
    next = child_node->next_sibling;
    reset_node_state(impl, child);
    child = next;
  }
}

static int tick_node(salts_bt_impl *impl, salts_bt_node_id id,
                     size_t depth, salts_bt_node_status *out_status) {
  salts_bt_node *node = get_node(impl, id);
  salts_bt_node_id child_id;
  if (node == NULL || out_status == NULL || depth > impl->max_depth)
    return SALTS_EINVAL;

  if (node->kind == SALTS_BT_NODE_LEAF) {
    salts_bt_node_status status = node->handler(node->user);
    if (status != SALTS_BT_SUCCESS &&
        status != SALTS_BT_FAILURE &&
        status != SALTS_BT_RUNNING)
      return SALTS_EINVAL;
    node->last_status = status;
    *out_status = status;
    return SALTS_OK;
  }

  child_id = node->cursor != SALTS_BT_INVALID_NODE_ID
                 ? node->cursor
                 : node->first_child;

  if (node_kind_is_single_child(node->kind)) {
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    int status;
    if (child_id == SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
    status = tick_node(impl, child_id, depth + 1u, &child_status);
    if (status != SALTS_OK) return status;
    if (node->kind == SALTS_BT_NODE_INVERTER) {
      if (child_status == SALTS_BT_SUCCESS) child_status = SALTS_BT_FAILURE;
      else if (child_status == SALTS_BT_FAILURE) child_status = SALTS_BT_SUCCESS;
    } else if (node->kind == SALTS_BT_NODE_FORCE_SUCCESS) {
      if (child_status != SALTS_BT_RUNNING) child_status = SALTS_BT_SUCCESS;
    } else if (node->kind == SALTS_BT_NODE_FORCE_FAILURE) {
      if (child_status != SALTS_BT_RUNNING) child_status = SALTS_BT_FAILURE;
    }
    node->last_status = child_status;
    *out_status = child_status;
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_SEQUENCE) {
    while (child_id != SALTS_BT_INVALID_NODE_ID) {
      salts_bt_node *child = get_node(impl, child_id);
      salts_bt_node_status child_status = SALTS_BT_FAILURE;
      salts_bt_node_id next;
      int status;
      if (child == NULL) return SALTS_EINVAL;
      next = child->next_sibling;
      status = tick_node(impl, child_id, depth + 1u, &child_status);
      if (status != SALTS_OK) return status;
      if (child_status == SALTS_BT_FAILURE) {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_FAILURE;
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }
      if (child_status == SALTS_BT_RUNNING) {
        node->cursor = child_id;
        node->last_status = SALTS_BT_RUNNING;
        *out_status = SALTS_BT_RUNNING;
        return SALTS_OK;
      }
      child_id = next;
      node->cursor = child_id;
    }
    reset_node_state(impl, id);
    node->last_status = SALTS_BT_SUCCESS;
    *out_status = SALTS_BT_SUCCESS;
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_FALLBACK) {
    while (child_id != SALTS_BT_INVALID_NODE_ID) {
      salts_bt_node *child = get_node(impl, child_id);
      salts_bt_node_status child_status = SALTS_BT_FAILURE;
      salts_bt_node_id next;
      int status;
      if (child == NULL) return SALTS_EINVAL;
      next = child->next_sibling;
      status = tick_node(impl, child_id, depth + 1u, &child_status);
      if (status != SALTS_OK) return status;
      if (child_status == SALTS_BT_SUCCESS) {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_SUCCESS;
        *out_status = SALTS_BT_SUCCESS;
        return SALTS_OK;
      }
      if (child_status == SALTS_BT_RUNNING) {
        node->cursor = child_id;
        node->last_status = SALTS_BT_RUNNING;
        *out_status = SALTS_BT_RUNNING;
        return SALTS_OK;
      }
      child_id = next;
      node->cursor = child_id;
    }
    reset_node_state(impl, id);
    node->last_status = SALTS_BT_FAILURE;
    *out_status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }

  return SALTS_EINVAL;
}

void salts_bt_tree_config_init(salts_bt_tree_config *config) {
  if (config == NULL) return;
  config->max_nodes = 128u;
  config->max_depth = 64u;
}

int salts_bt_tree_init(salts_bt_tree *tree,
                       const salts_bt_tree_config *config) {
  salts_bt_impl *impl;
  if (tree == NULL || config == NULL || tree->impl != NULL ||
      config->max_nodes == 0u ||
      config->max_nodes > (size_t)UINT32_MAX ||
      config->max_depth == 0u ||
      config->max_depth > SALTS_BT_MAX_DEPTH ||
      config->max_depth > config->max_nodes ||
      config->max_nodes > SIZE_MAX / sizeof(salts_bt_node) - 1u)
    return SALTS_EINVAL;

  impl = (salts_bt_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->nodes = (salts_bt_node *)calloc(config->max_nodes + 1u,
                                       sizeof(*impl->nodes));
  if (impl->nodes == NULL) {
    free(impl);
    return SALTS_ENOMEM;
  }
  impl->max_nodes = config->max_nodes;
  impl->max_depth = config->max_depth;
  impl->root = SALTS_BT_INVALID_NODE_ID;
  tree->impl = impl;
  return SALTS_OK;
}

int salts_bt_tree_add_node(salts_bt_tree *tree,
                           salts_bt_node_id parent_id,
                           salts_bt_node_kind kind,
                           salts_bt_leaf_fn handler,
                           void *user,
                           salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl = tree_impl(tree);
  salts_bt_node *parent = NULL;
  salts_bt_node *node;
  salts_bt_node_id id;
  size_t depth = 1u;

  if (out_node_id != NULL) *out_node_id = SALTS_BT_INVALID_NODE_ID;
  if (impl == NULL || out_node_id == NULL) return SALTS_EINVAL;
  if (impl->ticking) return SALTS_EBUSY;
  if (!node_kind_valid(kind)) return SALTS_EINVAL;
  if ((kind == SALTS_BT_NODE_LEAF && handler == NULL) ||
      (kind != SALTS_BT_NODE_LEAF && handler != NULL))
    return SALTS_EINVAL;

  if (parent_id == SALTS_BT_INVALID_NODE_ID) {
    if (impl->root != SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
  } else {
    parent = get_node(impl, parent_id);
    if (parent == NULL || !node_kind_accepts_children(parent->kind) ||
        (node_kind_is_single_child(parent->kind) && parent->child_count >= 1u))
      return SALTS_EINVAL;
    depth = parent->depth + 1u;
    if (depth > impl->max_depth) return SALTS_EINVAL;
  }

  if (impl->node_count >= impl->max_nodes) return SALTS_ENOBUFS;
  id = (salts_bt_node_id)(impl->node_count + 1u);
  node = &impl->nodes[id];
  memset(node, 0, sizeof(*node));
  node->kind = kind;
  node->handler = handler;
  node->user = user;
  node->parent = parent_id;
  node->depth = depth;
  node->last_status = SALTS_BT_FAILURE;
  ++impl->node_count;

  if (parent == NULL) {
    impl->root = id;
  } else if (parent->first_child == SALTS_BT_INVALID_NODE_ID) {
    parent->first_child = id;
    parent->last_child = id;
    ++parent->child_count;
  } else {
    salts_bt_node *last = get_node(impl, parent->last_child);
    if (last == NULL) {
      --impl->node_count;
      memset(node, 0, sizeof(*node));
      return SALTS_EINVAL;
    }
    last->next_sibling = id;
    parent->last_child = id;
    ++parent->child_count;
  }

  *out_node_id = id;
  return SALTS_OK;
}

int salts_bt_tree_tick(salts_bt_tree *tree,
                       salts_bt_node_status *out_status) {
  salts_bt_impl *impl = tree_impl(tree);
  int status;
  if (impl == NULL || out_status == NULL ||
      impl->root == SALTS_BT_INVALID_NODE_ID)
    return SALTS_EINVAL;
  if (impl->ticking) return SALTS_EBUSY;
  impl->ticking = true;
  status = tick_node(impl, impl->root, 1u, out_status);
  impl->ticking = false;
  return status;
}

int salts_bt_tree_reset(salts_bt_tree *tree) {
  salts_bt_impl *impl = tree_impl(tree);
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->ticking) return SALTS_EBUSY;
  if (impl->root != SALTS_BT_INVALID_NODE_ID)
    reset_node_state(impl, impl->root);
  return SALTS_OK;
}

int salts_bt_tree_get_root(const salts_bt_tree *tree,
                           salts_bt_node_id *out_root) {
  const salts_bt_impl *impl = tree_const_impl(tree);
  if (impl == NULL || out_root == NULL ||
      impl->root == SALTS_BT_INVALID_NODE_ID)
    return SALTS_EINVAL;
  *out_root = impl->root;
  return SALTS_OK;
}

int salts_bt_tree_get_node_count(const salts_bt_tree *tree,
                                 size_t *out_count) {
  const salts_bt_impl *impl = tree_const_impl(tree);
  if (impl == NULL || out_count == NULL) return SALTS_EINVAL;
  *out_count = impl->node_count;
  return SALTS_OK;
}

int salts_bt_tree_destroy(salts_bt_tree *tree) {
  salts_bt_impl *impl;
  if (tree == NULL) return SALTS_EINVAL;
  impl = tree_impl(tree);
  if (impl == NULL) return SALTS_OK;
  if (impl->ticking) return SALTS_EBUSY;
  free(impl->nodes);
  memset(impl, 0, sizeof(*impl));
  free(impl);
  tree->impl = NULL;
  return SALTS_OK;
}
