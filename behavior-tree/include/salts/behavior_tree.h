#ifndef SALTS_BEHAVIOR_TREE_H
#define SALTS_BEHAVIOR_TREE_H

#include <salts/error_codes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A zero-initialized owning handle. It must not be copied while initialized. */
typedef struct salts_bt_tree {
  void *impl;
} salts_bt_tree;

typedef uint32_t salts_bt_node_id;

#define SALTS_BT_INVALID_NODE_ID ((salts_bt_node_id)0u)
#define SALTS_BT_MAX_DEPTH 256u

typedef enum salts_bt_node_kind {
  SALTS_BT_NODE_LEAF = 0,
  SALTS_BT_NODE_SEQUENCE = 1,
  SALTS_BT_NODE_FALLBACK = 2,
  SALTS_BT_NODE_INVERTER = 3,
  SALTS_BT_NODE_FORCE_SUCCESS = 4,
  SALTS_BT_NODE_FORCE_FAILURE = 5
} salts_bt_node_kind;

typedef enum salts_bt_node_status {
  SALTS_BT_SUCCESS = 0,
  SALTS_BT_FAILURE = 1,
  SALTS_BT_RUNNING = 2
} salts_bt_node_status;

/*
 * max_nodes and max_depth are hard construction limits. max_depth includes
 * the root. max_depth must be in [1, SALTS_BT_MAX_DEPTH].
 */
typedef struct salts_bt_tree_config {
  size_t max_nodes;
  size_t max_depth;
} salts_bt_tree_config;

/*
 * Leaf handlers run synchronously on the thread calling salts_bt_tree_tick().
 * They may return RUNNING to be ticked again on a later call. user is borrowed
 * until tree destruction; the tree never frees it. A handler must not destroy,
 * reset, mutate, or recursively tick the same tree.
 */
typedef salts_bt_node_status (*salts_bt_leaf_fn)(void *user);

/* Set a useful bounded default before changing individual limits. */
void salts_bt_tree_config_init(salts_bt_tree_config *config);

/*
 * Initialize a zero-initialized tree. The implementation allocates exactly
 * max_nodes node slots during initialization and does not grow them later.
 * Returns SALTS_OK, SALTS_EINVAL for an invalid contract or SALTS_ENOMEM.
 */
int salts_bt_tree_init(salts_bt_tree *tree,
                       const salts_bt_tree_config *config);

/*
 * Add a node. parent_id == SALTS_BT_INVALID_NODE_ID declares the single root;
 * otherwise the node is appended to its parent's child list. Parents must be
 * created before children. Sequence/Fallback may have many children; Inverter,
 * ForceSuccess and ForceFailure require exactly one child. Leaves cannot have
 * children. Leaves require a handler; control-flow nodes require a NULL handler.
 * IDs are stable for the lifetime of the tree and are not reused after reset.
 *
 * Capacity exhaustion returns SALTS_ENOBUFS. Invalid topology/contracts return
 * SALTS_EINVAL. A tree cannot be mutated while it is ticking (SALTS_EBUSY).
 */
int salts_bt_tree_add_node(salts_bt_tree *tree,
                           salts_bt_node_id parent_id,
                           salts_bt_node_kind kind,
                           salts_bt_leaf_fn handler,
                           void *user,
                           salts_bt_node_id *out_node_id);

/*
 * Tick the root once. Sequence stops at FAILURE or RUNNING and resumes the
 * active child on the next tick. Fallback advances after FAILURE and stops at
 * SUCCESS or RUNNING. An empty Sequence succeeds; an empty Fallback fails.
 *
 * Terminal control-flow outcomes reset that subtree, so a later re-entry starts
 * from the first child. This initial runtime is single-driver and synchronous;
 * RUNNING is cooperative re-tick, not hidden background work. A tree is not
 * internally synchronized; all operations on one tree must be serialized on
 * a single driver thread.
 *
 * On API error, the function returns a Salts status code. On success it stores
 * one of SALTS_BT_SUCCESS, SALTS_BT_FAILURE, SALTS_BT_RUNNING in out_status.
 */
int salts_bt_tree_tick(salts_bt_tree *tree,
                       salts_bt_node_status *out_status);

/* Validate complete topology before any leaf is allowed to run. */
int salts_bt_tree_validate(const salts_bt_tree *tree);

/* Reset all per-node execution state without changing tree topology. */
int salts_bt_tree_reset(salts_bt_tree *tree);

/* Copy the current root ID / node count. Returns SALTS_EINVAL if no root exists. */
int salts_bt_tree_get_root(const salts_bt_tree *tree,
                           salts_bt_node_id *out_root);
int salts_bt_tree_get_node_count(const salts_bt_tree *tree,
                                 size_t *out_count);

/*
 * Destroy an initialized tree. It returns SALTS_EBUSY if called reentrantly
 * from a leaf handler; otherwise it frees only tree-owned memory and clears
 * the handle. User pointers remain caller-owned.
 */
int salts_bt_tree_destroy(salts_bt_tree *tree);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_BEHAVIOR_TREE_H */
