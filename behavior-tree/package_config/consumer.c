#include <salts/behavior_tree.h>

static salts_bt_node_status succeed(void *user) {
  unsigned int *calls = (unsigned int *)user;
  if (calls != 0) ++*calls;
  return SALTS_BT_SUCCESS;
}

int main(void) {
  salts_bt_tree tree = {0};
  salts_bt_tree_config config = {0};
  salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
  salts_bt_node_id leaf = SALTS_BT_INVALID_NODE_ID;
  salts_bt_node_status status = SALTS_BT_FAILURE;
  unsigned int calls = 0u;
  int result;

  salts_bt_tree_config_init(&config);
  config.max_nodes = 4u;
  config.max_depth = 4u;

  result = salts_bt_tree_init(&tree, &config);
  if (result != SALTS_OK) return 1;

  result = salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                  SALTS_BT_NODE_SEQUENCE, 0, 0, &root);
  if (result != SALTS_OK) goto cleanup;

  result = salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                  succeed, &calls, &leaf);
  if (result != SALTS_OK) goto cleanup;

  result = salts_bt_tree_validate(&tree);
  if (result != SALTS_OK) goto cleanup;

  result = salts_bt_tree_tick(&tree, &status);
  if (result != SALTS_OK || status != SALTS_BT_SUCCESS || calls != 1u) {
    result = SALTS_EINVAL;
    goto cleanup;
  }

  result = salts_bt_tree_destroy(&tree);
  return result == SALTS_OK ? 0 : 2;

cleanup:
  (void)salts_bt_tree_destroy(&tree);
  return result == SALTS_OK ? 3 : 4;
}
