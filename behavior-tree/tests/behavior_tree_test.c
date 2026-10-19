#include <salts/behavior_tree.h>
#include <salts/clock.h>
#include <salts/thread.h>

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


typedef struct running_leaf_state {
  salts_bt_tree *tree;
  size_t tick_calls;
  size_t halt_calls;
  int reentrant_reset_status;
} running_leaf_state;

static salts_bt_node_status always_running_leaf(void *user) {
  running_leaf_state *state = (running_leaf_state *)user;
  ++state->tick_calls;
  return SALTS_BT_RUNNING;
}

static void halt_running_leaf(void *user) {
  running_leaf_state *state = (running_leaf_state *)user;
  ++state->halt_calls;
  state->reentrant_reset_status = salts_bt_tree_reset(state->tree);
}

typedef struct condition_probe {
  const bool *steps;
  size_t step_count;
  size_t calls;
  int status;
} condition_probe;

static int probe_condition(void *user, bool *out_condition) {
  condition_probe *probe = (condition_probe *)user;
  if (probe == NULL || out_condition == NULL ||
      probe->steps == NULL || probe->step_count == 0u)
    return SALTS_EINVAL;
  *out_condition = probe->steps[probe->calls < probe->step_count
                                    ? probe->calls
                                    : probe->step_count - 1u];
  ++probe->calls;
  return probe->status;
}

typedef struct selector_probe {
  const size_t *steps;
  size_t step_count;
  size_t calls;
} selector_probe;

static int probe_selector(void *user, size_t *out_index) {
  selector_probe *probe = (selector_probe *)user;
  if (probe == NULL || out_index == NULL ||
      probe->steps == NULL || probe->step_count == 0u)
    return SALTS_EINVAL;
  *out_index = probe->steps[probe->calls < probe->step_count
                                ? probe->calls
                                : probe->step_count - 1u];
  ++probe->calls;
  return SALTS_OK;
}


typedef struct pipeline_probe {
  size_t prepare_calls;
  size_t complete_calls;
  size_t last_prepared_child;
  size_t last_completed_child;
  salts_bt_node_status last_completed_status;
  size_t abort_calls;
  size_t last_aborted_child;
  int status;
} pipeline_probe;

static int pipeline_prepare(void *user, size_t child_index) {
  pipeline_probe *probe = (pipeline_probe *)user;
  ++probe->prepare_calls;
  probe->last_prepared_child = child_index;
  return probe->status;
}

static int pipeline_complete(void *user, size_t child_index,
                             salts_bt_node_status child_status) {
  pipeline_probe *probe = (pipeline_probe *)user;
  ++probe->complete_calls;
  probe->last_completed_child = child_index;
  probe->last_completed_status = child_status;
  return probe->status;
}

static void pipeline_abort(void *user, size_t child_index) {
  pipeline_probe *probe = (pipeline_probe *)user;
  ++probe->abort_calls;
  probe->last_aborted_child = child_index;
}

typedef struct entry_changed_probe {
  const bool *steps;
  size_t step_count;
  size_t calls;
  int status;
} entry_changed_probe;

static int entry_changed(void *user, bool *out_changed) {
  entry_changed_probe *probe = (entry_changed_probe *)user;
  if (probe == NULL || out_changed == NULL ||
      probe->steps == NULL || probe->step_count == 0u)
    return SALTS_EINVAL;
  *out_changed = probe->steps[probe->calls < probe->step_count
                                  ? probe->calls
                                  : probe->step_count - 1u];
  ++probe->calls;
  return probe->status;
}

typedef struct queue_probe {
  size_t remaining;
  size_t begin_calls;
  size_t finish_calls;
  size_t abort_calls;
} queue_probe;

static int queue_begin(void *user, bool *out_has_item) {
  queue_probe *probe = (queue_probe *)user;
  if (probe == NULL || out_has_item == NULL) return SALTS_EINVAL;
  ++probe->begin_calls;
  *out_has_item = probe->remaining > 0u;
  if (*out_has_item) --probe->remaining;
  return SALTS_OK;
}

static int queue_finish(void *user, bool *out_has_more) {
  queue_probe *probe = (queue_probe *)user;
  if (probe == NULL || out_has_more == NULL) return SALTS_EINVAL;
  ++probe->finish_calls;
  *out_has_more = probe->remaining > 0u;
  return SALTS_OK;
}

static void queue_abort(void *user) {
  queue_probe *probe = (queue_probe *)user;
  ++probe->abort_calls;
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

  it("retries terminal failures up to the configured attempt bound") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_FAILURE, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("repeats a bounded number of terminal child cycles across RUNNING ticks") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_REPEAT,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 3u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("rejects unbounded or over-budget Retry and Repeat limits") {
    salts_bt_tree tree = {0};
    salts_bt_tree_config config = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;

    salts_bt_tree_config_init(&config);
    config.max_nodes = 4u;
    config.max_depth = 4u;
    config.max_control_iterations = 1u;
    check_equal(salts_bt_tree_init(&tree, &config), SALTS_OK);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 0u, &root), SALTS_EINVAL);
    check_equal(root, SALTS_BT_INVALID_NODE_ID);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_EINVAL);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_REPEAT,
                    NULL, NULL, 2u, &root), SALTS_EINVAL);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("returns FAILURE when Retry exhausts all attempts") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("treats a zero-cycle Repeat as immediate SUCCESS after structural validation") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_REPEAT,
                    NULL, NULL, 0u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("halts a RUNNING leaf once on reset and again only after it restarts") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_leaf(&tree, SALTS_BT_INVALID_NODE_ID,
                                      &ops, &state, &root), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(state.tick_calls, 1u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(state.halt_calls, 1u);
    check_equal(state.reentrant_reset_status, SALTS_EBUSY);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(state.halt_calls, 2u);
    check_equal(state.reentrant_reset_status, SALTS_EBUSY);
    check_true(tree.impl == NULL);
  }

  it("halts a RUNNING child when Timeout expires") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_timed_node(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_TIMEOUT,
                    UINT64_C(50000000), &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &state, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(state.tick_calls, 1u);
    cmeta_sleep_ms(60u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(state.halt_calls, 1u);
    check_equal(state.reentrant_reset_status, SALTS_EBUSY);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(state.halt_calls, 1u);
  }

  it("delays child execution without blocking the driver") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_timed_node(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_DELAY,
                    UINT64_C(30000000), &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(probe.calls, 0u);
    cmeta_sleep_ms(40u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("runs Parallel children cooperatively and retains completed branches") {
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

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_PARALLEL, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &first_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &second_probe, &second), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(first_probe.calls, 1u);
    check_equal(second_probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(first_probe.calls, 2u);
    check_equal(second_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("halts active Parallel branches when any branch fails") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id active = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id failed = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state state = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status fail_steps[] = {SALTS_BT_FAILURE};
    leaf_probe fail_probe = {fail_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_RUNNING;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_PARALLEL, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &state, &active), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &fail_probe, &failed), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(state.halt_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(state.halt_calls, 1u);
  }

  it("rechecks ReactiveSequence conditions and halts later active children") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id condition = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id work = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status condition_steps[] = {
        SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
    leaf_probe condition_probe = {condition_steps, 2u, 0u};
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state work_state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_REACTIVE_SEQUENCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &condition_probe, &condition), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &work_state, &work), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(condition_probe.calls, 1u);
    check_equal(work_state.tick_calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(condition_probe.calls, 2u);
    check_equal(work_state.halt_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(work_state.halt_calls, 1u);
  }

  it("caches RunOnce's terminal result until reset") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_RUN_ONCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 3u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }


  it("switches IfThenElse branches and halts a displaced RUNNING branch") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id then_node = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id else_node = SALTS_BT_INVALID_NODE_ID;
    const bool conditions[] = {true, false};
    condition_probe condition = {conditions, 2u, 0u, SALTS_OK};
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state running = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status else_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe else_probe = {else_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_IF_THEN_ELSE,
                                                probe_condition, &condition, 0u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &running, &then_node),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &else_probe, &else_node), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(running.tick_calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(running.halt_calls, 1u);
    check_equal(else_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(running.halt_calls, 1u);
  }

  it("enforces Precondition before running its child and yields from WhileDo") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const bool precondition_steps[] = {false, true};
    condition_probe precondition = {precondition_steps, 2u, 0u, SALTS_OK};
    const salts_bt_node_status child_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe child_probe = {child_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_RUNNING;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_PRECONDITION,
                                                probe_condition, &precondition, 0u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &child_probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(child_probe.calls, 0u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(child_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);

    {
      salts_bt_tree while_tree = {0};
      salts_bt_node_id while_root = SALTS_BT_INVALID_NODE_ID;
      salts_bt_node_id while_child = SALTS_BT_INVALID_NODE_ID;
      const bool while_conditions[] = {true, true, false};
      condition_probe while_condition = {while_conditions, 3u, 0u, SALTS_OK};
      const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
      leaf_probe while_probe = {success_steps, 1u, 0u};
      init_tree(&while_tree, 8u, 8u);
      check_equal(salts_bt_tree_add_condition_node(&while_tree, SALTS_BT_INVALID_NODE_ID,
                                                  SALTS_BT_NODE_WHILE_DO,
                                                  probe_condition, &while_condition, 4u,
                                                  &while_root), SALTS_OK);
      check_equal(salts_bt_tree_add_node(&while_tree, while_root, SALTS_BT_NODE_LEAF,
                                        probe_leaf, &while_probe, &while_child), SALTS_OK);
      check_equal(salts_bt_tree_tick(&while_tree, &result), SALTS_OK);
      check_equal(result, SALTS_BT_RUNNING);
      check_equal(while_probe.calls, 1u);
      check_equal(salts_bt_tree_tick(&while_tree, &result), SALTS_OK);
      check_equal(result, SALTS_BT_RUNNING);
      check_equal(while_probe.calls, 2u);
      check_equal(salts_bt_tree_tick(&while_tree, &result), SALTS_OK);
      check_equal(result, SALTS_BT_SUCCESS);
      check_equal(while_probe.calls, 2u);
      check_equal(salts_bt_tree_destroy(&while_tree), SALTS_OK);
    }
  }

  it("routes Switch to the selected case and halts the previous running case") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    const size_t selections[] = {0u, 1u};
    selector_probe selector = {selections, 2u, 0u};
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state running = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe success_probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_switch_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                              probe_selector, &selector, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &running, &first),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &success_probe, &second), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(running.tick_calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(running.halt_calls, 1u);
    check_equal(success_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("keeps running after successful child ticks and fails on its bounded budget") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_SUCCESS, SALTS_BT_SUCCESS, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 3u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID,
                    SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE, NULL, NULL,
                    2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("propagates predicate errors before executing any branch side effects") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const bool conditions[] = {true};
    condition_probe condition = {conditions, 1u, 0u, SALTS_EINVAL};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe child_probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_PRECONDITION,
                                                probe_condition, &condition, 0u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &child_probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(condition.calls, 1u);
    check_equal(child_probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("fails Switch cleanly for an out-of-range case index") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    const size_t selections[] = {3u};
    selector_probe selector = {selections, 1u, 0u};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_switch_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                              probe_selector, &selector, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("PipelineSequence runs preparation/completion once per child across RUNNING") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    pipeline_probe pipeline = {0u, 0u, 0u, 0u, SALTS_BT_FAILURE, 0u, 0u, SALTS_OK};
    const salts_bt_node_status first_steps[] = {SALTS_BT_SUCCESS};
    const salts_bt_node_status second_steps[] = {SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe first_probe = {first_steps, 1u, 0u};
    leaf_probe second_probe = {second_steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_pipeline_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                pipeline_prepare, pipeline_complete,
                                                pipeline_abort, &pipeline, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &first_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &second_probe, &second), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(pipeline.prepare_calls, 2u);
    check_equal(pipeline.complete_calls, 1u);
    check_equal(pipeline.last_completed_child, 0u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(pipeline.prepare_calls, 2u);
    check_equal(pipeline.complete_calls, 2u);
    check_equal(pipeline.last_completed_child, 1u);
    check_equal(pipeline.last_completed_status, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("EntryUpdated continues live work, caches terminal results, and reruns on change") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const bool change_steps[] = {false, false, false, true};
    entry_changed_probe changed = {change_steps, 4u, 0u, SALTS_OK};
    const salts_bt_node_status child_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
    leaf_probe probe = {child_steps, 3u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_entry_updated_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                     entry_changed, &changed, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(probe.calls, 3u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("ConsumeQueue advances one item per successful child iteration") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    queue_probe queue = {2u, 0u, 0u, 0u};
    const salts_bt_node_status child_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {child_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_consume_queue_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                     queue_begin, queue_finish,
                                                     queue_abort, &queue, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(queue.begin_calls, 1u);
    check_equal(queue.finish_calls, 1u);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(queue.begin_calls, 2u);
    check_equal(queue.finish_calls, 2u);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("aborts an active PipelineSequence context after halting the child on reset") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    pipeline_probe pipeline = {0u, 0u, 0u, 0u, SALTS_BT_FAILURE, 0u, 0u, SALTS_OK};
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state leaf = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_pipeline_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                pipeline_prepare, pipeline_complete,
                                                pipeline_abort, &pipeline, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(pipeline.prepare_calls, 1u);
    check_equal(pipeline.abort_calls, 0u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(leaf.halt_calls, 1u);
    check_equal(pipeline.abort_calls, 1u);
    check_equal(pipeline.last_aborted_child, 0u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(pipeline.abort_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("aborts a live ConsumeQueue item exactly once when reset interrupts its child") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    queue_probe queue = {2u, 0u, 0u, 0u};
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state leaf = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_consume_queue_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                     queue_begin, queue_finish,
                                                     queue_abort, &queue, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(queue.begin_calls, 1u);
    check_equal(queue.finish_calls, 0u);
    check_equal(queue.abort_calls, 0u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(leaf.halt_calls, 1u);
    check_equal(queue.abort_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(queue.abort_calls, 1u);
  }



  it("treats an empty ConsumeQueue as successful exhaustion") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    queue_probe queue = {0u, 0u, 0u, 0u};
    const salts_bt_node_status child_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {child_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_consume_queue_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                     queue_begin, queue_finish,
                                                     queue_abort, &queue, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(queue.begin_calls, 1u);
    check_equal(queue.finish_calls, 0u);
    check_equal(queue.abort_calls, 0u);
    check_equal(probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }


}
