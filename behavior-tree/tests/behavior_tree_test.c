#include <salts/behavior_tree.h>

#include "tinytest.h"

typedef struct leaf_probe {
  const salts_bt_node_status *steps;
  size_t step_count;
  size_t calls;
} leaf_probe;

static salts_bt_node_status probe_leaf(void *user) {
  leaf_probe *probe = (leaf_probe *)user;
  salts_bt_node_status status;
  if (probe == NULL || probe->steps == NULL || probe->step_count == 0u)
    return SALTS_BT_FAILURE;
  status = probe->steps[probe->calls < probe->step_count
                            ? probe->calls
                            : probe->step_count - 1u];
  ++probe->calls;
  return status;
}

static void init_tree(salts_bt_tree *tree, size_t max_nodes, size_t max_depth) {
  salts_bt_tree_config config = {0};
  salts_bt_tree_config_init(&config);
  config.max_nodes = max_nodes;
  config.max_depth = max_depth;
  check_equal(salts_bt_tree_init(tree, &config), SALTS_OK);
}

spec("Salts C11 Behavior Tree") {
  it("resumes a Sequence at its RUNNING child and resets after terminal success") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status first_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    const salts_bt_node_status second_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe first_probe = {first_steps, 2u, 0u};
    leaf_probe second_probe = {second_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &first_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &second_probe, &second), SALTS_OK);
    check_true(root != first);
    check_true(first != second);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(first_probe.calls, 1u);
    check_equal(second_probe.calls, 0u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(first_probe.calls, 2u);
    check_equal(second_probe.calls, 1u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(first_probe.calls, 3u);
    check_equal(second_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_true(tree.impl == NULL);
  }

  it("advances a Fallback after FAILURE and resumes the RUNNING alternative") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status fail_steps[] = {SALTS_BT_FAILURE};
    const salts_bt_node_status next_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe fail_probe = {fail_steps, 1u, 0u};
    leaf_probe next_probe = {next_steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_FALLBACK, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &fail_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &next_probe, &second), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(fail_probe.calls, 1u);
    check_equal(next_probe.calls, 1u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(fail_probe.calls, 1u);
    check_equal(next_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("enforces node capacity and rejects invalid topology") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id extra = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {success_steps, 1u, 0u};
    size_t node_count = 0u;

    init_tree(&tree, 2u, 2u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &extra), SALTS_ENOBUFS);
    check_equal(extra, SALTS_BT_INVALID_NODE_ID);
    check_equal(salts_bt_tree_add_node(&tree, child, SALTS_BT_NODE_SEQUENCE,
                                      NULL, NULL, &extra), SALTS_EINVAL);
    check_equal(salts_bt_tree_get_node_count(&tree, &node_count), SALTS_OK);
    check_equal(node_count, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("rejects missing roots and invalid configuration") {
    salts_bt_tree tree = {0};
    salts_bt_tree_config config = {0};
    salts_bt_node_status result = SALTS_BT_FAILURE;
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;

    salts_bt_tree_config_init(&config);
    config.max_depth = 0u;
    check_equal(salts_bt_tree_init(&tree, &config), SALTS_EINVAL);
    check_equal(salts_bt_tree_init(&tree, NULL), SALTS_EINVAL);

    salts_bt_tree_config_init(&config);
    config.max_nodes = 4u;
    config.max_depth = 4u;
    check_equal(salts_bt_tree_init(&tree, &config), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(salts_bt_tree_get_root(&tree, &root), SALTS_EINVAL);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("maps terminal child states through Inverter") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_INVERTER, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("maps a terminal child failure to SUCCESS through ForceSuccess") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_FORCE_SUCCESS, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("maps a terminal child success to FAILURE through ForceFailure") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_FORCE_FAILURE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("requires exactly one child for single-child decorators") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id extra = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_INVERTER, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &extra), SALTS_EINVAL);
    check_equal(extra, SALTS_BT_INVALID_NODE_ID);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }
  it("preserves RUNNING through status decorators") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_FORCE_SUCCESS, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("validates the whole tree before any leaf side effect") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id incomplete = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_INVERTER,
                                      NULL, NULL, &incomplete), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_EINVAL);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("accepts a complete tree through explicit validation") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_INVERTER, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_OK);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

}
