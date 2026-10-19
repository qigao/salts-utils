#include <salts/behavior_tree.h>
#include <salts/clock.h>

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
  size_t limit;
  size_t counter;
  uint64_t duration_ns;
  uint64_t deadline_ns;
  bool timer_started;
  bool delay_elapsed;
  bool evaluated;
  bool run_once_complete;
  salts_bt_leaf_halt_fn halt;
  salts_bt_condition_fn condition;
  salts_bt_selector_fn selector;
  salts_bt_pipeline_prepare_fn pipeline_prepare;
  salts_bt_pipeline_complete_fn pipeline_complete;
  salts_bt_pipeline_abort_fn pipeline_abort;
  salts_bt_entry_changed_fn entry_changed;
  salts_bt_queue_begin_fn queue_begin;
  salts_bt_queue_finish_fn queue_finish;
  salts_bt_queue_abort_fn queue_abort;
  salts_bt_parallel_enter_fn parallel_enter;
  salts_bt_parallel_finish_fn parallel_finish;
  salts_bt_parallel_abort_fn parallel_abort;
  bool observed;
  bool prepared;
  bool parallel_branch_prepared;
  bool running;
} salts_bt_node;

typedef struct salts_bt_impl {
  salts_bt_node *nodes; /* index zero is reserved as SALTS_BT_INVALID_NODE_ID */
  size_t max_nodes;
  size_t max_depth;
  size_t max_control_iterations;
  size_t node_count;
  salts_bt_node_id root;
  bool busy;
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
         kind == SALTS_BT_NODE_FORCE_FAILURE ||
         kind == SALTS_BT_NODE_RETRY ||
         kind == SALTS_BT_NODE_REPEAT ||
         kind == SALTS_BT_NODE_TIMEOUT ||
         kind == SALTS_BT_NODE_DELAY ||
         kind == SALTS_BT_NODE_PARALLEL ||
         kind == SALTS_BT_NODE_REACTIVE_SEQUENCE ||
         kind == SALTS_BT_NODE_RUN_ONCE ||
         kind == SALTS_BT_NODE_IF_THEN_ELSE ||
         kind == SALTS_BT_NODE_PRECONDITION ||
         kind == SALTS_BT_NODE_WHILE_DO ||
         kind == SALTS_BT_NODE_SWITCH ||
         kind == SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE ||
         kind == SALTS_BT_NODE_PIPELINE_SEQUENCE ||
         kind == SALTS_BT_NODE_ENTRY_UPDATED ||
         kind == SALTS_BT_NODE_CONSUME_QUEUE;
}

static bool node_kind_accepts_children(salts_bt_node_kind kind) {
  return kind == SALTS_BT_NODE_SEQUENCE ||
         kind == SALTS_BT_NODE_FALLBACK ||
         kind == SALTS_BT_NODE_INVERTER ||
         kind == SALTS_BT_NODE_FORCE_SUCCESS ||
         kind == SALTS_BT_NODE_FORCE_FAILURE ||
         kind == SALTS_BT_NODE_RETRY ||
         kind == SALTS_BT_NODE_REPEAT ||
         kind == SALTS_BT_NODE_TIMEOUT ||
         kind == SALTS_BT_NODE_DELAY ||
         kind == SALTS_BT_NODE_PARALLEL ||
         kind == SALTS_BT_NODE_REACTIVE_SEQUENCE ||
         kind == SALTS_BT_NODE_RUN_ONCE ||
         kind == SALTS_BT_NODE_IF_THEN_ELSE ||
         kind == SALTS_BT_NODE_PRECONDITION ||
         kind == SALTS_BT_NODE_WHILE_DO ||
         kind == SALTS_BT_NODE_SWITCH ||
         kind == SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE ||
         kind == SALTS_BT_NODE_PIPELINE_SEQUENCE ||
         kind == SALTS_BT_NODE_ENTRY_UPDATED ||
         kind == SALTS_BT_NODE_CONSUME_QUEUE;
}

static bool node_kind_is_single_child(salts_bt_node_kind kind) {
  return kind == SALTS_BT_NODE_INVERTER ||
         kind == SALTS_BT_NODE_FORCE_SUCCESS ||
         kind == SALTS_BT_NODE_FORCE_FAILURE ||
         kind == SALTS_BT_NODE_RETRY ||
         kind == SALTS_BT_NODE_REPEAT ||
         kind == SALTS_BT_NODE_TIMEOUT ||
         kind == SALTS_BT_NODE_DELAY ||
         kind == SALTS_BT_NODE_RUN_ONCE ||
         kind == SALTS_BT_NODE_PRECONDITION ||
         kind == SALTS_BT_NODE_WHILE_DO ||
         kind == SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE ||
         kind == SALTS_BT_NODE_ENTRY_UPDATED ||
         kind == SALTS_BT_NODE_CONSUME_QUEUE;
}

static void reset_node_state(salts_bt_impl *impl, salts_bt_node_id id) {
  salts_bt_node *node = get_node(impl, id);
  salts_bt_node_id child;
  size_t child_index = 0u;
  const bool abort_pipeline =
      node != NULL && node->kind == SALTS_BT_NODE_PIPELINE_SEQUENCE && node->prepared;
  const bool abort_queue =
      node != NULL && node->kind == SALTS_BT_NODE_CONSUME_QUEUE && node->prepared;
  const size_t pipeline_child = node != NULL ? node->counter : 0u;
  if (node == NULL) return;
  if (node->kind == SALTS_BT_NODE_LEAF && node->running) {
    node->running = false;
    if (node->halt != NULL) node->halt(node->user);
  }
  node->cursor = SALTS_BT_INVALID_NODE_ID;
  node->counter = 0u;
  node->deadline_ns = 0u;
  node->timer_started = false;
  node->delay_elapsed = false;
  node->evaluated = false;
  node->run_once_complete = false;
  node->observed = false;
  node->prepared = false;
  node->parallel_branch_prepared = false;
  node->last_status = SALTS_BT_FAILURE;
  child = node->first_child;
  while (child != SALTS_BT_INVALID_NODE_ID) {
    salts_bt_node *child_node = get_node(impl, child);
    salts_bt_node_id next;
    if (child_node == NULL) break;
    next = child_node->next_sibling;
    {
      const bool abort_parallel_branch =
          node->kind == SALTS_BT_NODE_PARALLEL &&
          child_node->parallel_branch_prepared;
      reset_node_state(impl, child);
      if (abort_parallel_branch && node->parallel_abort != NULL)
        node->parallel_abort(node->user, child_index);
    }
    child = next;
    ++child_index;
  }
  /* Clean adapter-owned context only after an active child has been halted. */
  if (abort_pipeline && node->pipeline_abort != NULL)
    node->pipeline_abort(node->user, pipeline_child);
  if (abort_queue && node->queue_abort != NULL)
    node->queue_abort(node->user);
}

static int tick_node(salts_bt_impl *impl, salts_bt_node_id id,
                     size_t depth, salts_bt_node_status *out_status);

static int tick_node_impl(salts_bt_impl *impl, salts_bt_node_id id,
                          size_t depth, salts_bt_node_status *out_status) {
  salts_bt_node *node = get_node(impl, id);
  salts_bt_node_id child_id;
  if (node == NULL || out_status == NULL || depth > impl->max_depth)
    return SALTS_EINVAL;
  node->evaluated = false;

  if (node->kind == SALTS_BT_NODE_LEAF) {
    salts_bt_node_status status = node->handler(node->user);
    if (status != SALTS_BT_SUCCESS &&
        status != SALTS_BT_FAILURE &&
        status != SALTS_BT_RUNNING)
      return SALTS_EINVAL;
    node->running = status == SALTS_BT_RUNNING;
    node->last_status = status;
    *out_status = status;
    return SALTS_OK;
  }

  child_id = node->cursor != SALTS_BT_INVALID_NODE_ID
                 ? node->cursor
                 : node->first_child;

  if (node->kind == SALTS_BT_NODE_PIPELINE_SEQUENCE) {
    for (;;) {
      salts_bt_node_id current = SALTS_BT_INVALID_NODE_ID;
      salts_bt_node_id scan = node->first_child;
      size_t index = 0u;
      salts_bt_node_status child_status = SALTS_BT_FAILURE;
      int status;
      while (scan != SALTS_BT_INVALID_NODE_ID && index < node->counter) {
        const salts_bt_node *scan_node = get_node(impl, scan);
        if (scan_node == NULL) return SALTS_EINVAL;
        scan = scan_node->next_sibling;
        ++index;
      }
      current = node->cursor != SALTS_BT_INVALID_NODE_ID ? node->cursor : scan;
      if (current == SALTS_BT_INVALID_NODE_ID) {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_SUCCESS;
        *out_status = SALTS_BT_SUCCESS;
        return SALTS_OK;
      }
      if (!node->prepared) {
        if (node->pipeline_prepare == NULL || node->pipeline_complete == NULL)
          return SALTS_EINVAL;
        /* Begin ownership before the callback so abort can clean partial setup. */
        node->prepared = true;
        status = node->pipeline_prepare(node->user, node->counter);
        if (status != SALTS_OK) {
          reset_node_state(impl, id);
          return status;
        }
      }
      status = tick_node(impl, current, depth + 1u, &child_status);
      if (status != SALTS_OK) {
        reset_node_state(impl, id);
        return status;
      }
      if (child_status == SALTS_BT_RUNNING) {
        node->cursor = current;
        node->last_status = SALTS_BT_RUNNING;
        *out_status = SALTS_BT_RUNNING;
        return SALTS_OK;
      }
      status = node->pipeline_complete(node->user, node->counter, child_status);
      if (status != SALTS_OK) {
        reset_node_state(impl, id);
        return status;
      }
      node->cursor = SALTS_BT_INVALID_NODE_ID;
      node->prepared = false;
      if (child_status == SALTS_BT_FAILURE) {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_FAILURE;
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }
      ++node->counter;
    }
  }

  if (node->kind == SALTS_BT_NODE_ENTRY_UPDATED) {
    bool changed = false;
    salts_bt_node_id child = node->first_child;
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    int status;
    if (node->entry_changed == NULL || child == SALTS_BT_INVALID_NODE_ID)
      return SALTS_EINVAL;
    status = node->entry_changed(node->user, &changed);
    if (status != SALTS_OK) {
      reset_node_state(impl, id);
      return status;
    }
    if (!node->observed) changed = true;
    if (!changed && node->cursor == SALTS_BT_INVALID_NODE_ID && node->observed) {
      *out_status = node->last_status;
      return SALTS_OK;
    }
    if (changed && node->cursor != SALTS_BT_INVALID_NODE_ID) {
      reset_node_state(impl, node->cursor);
      node->cursor = SALTS_BT_INVALID_NODE_ID;
    } else if (changed) {
      reset_node_state(impl, child);
    }
    node->observed = true;
    status = tick_node(impl, child, depth + 1u, &child_status);
    if (status != SALTS_OK) {
      reset_node_state(impl, id);
      return status;
    }
    node->last_status = child_status;
    node->cursor = child_status == SALTS_BT_RUNNING ? child : SALTS_BT_INVALID_NODE_ID;
    *out_status = child_status;
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_CONSUME_QUEUE) {
    salts_bt_node_id child = node->first_child;
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    int status;
    if (node->queue_begin == NULL || node->queue_finish == NULL ||
        child == SALTS_BT_INVALID_NODE_ID)
      return SALTS_EINVAL;
    if (!node->prepared) {
      bool has_item = false;
      /* begin may prepare partial item context even when it returns an error. */
      node->prepared = true;
      status = node->queue_begin(node->user, &has_item);
      if (status != SALTS_OK) {
        reset_node_state(impl, id);
        return status;
      }
      if (!has_item) {
        node->prepared = false;
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_SUCCESS;
        *out_status = SALTS_BT_SUCCESS;
        return SALTS_OK;
      }
    }
    status = tick_node(impl, child, depth + 1u, &child_status);
    if (status != SALTS_OK) {
      reset_node_state(impl, id);
      return status;
    }
    if (child_status == SALTS_BT_RUNNING) {
      node->cursor = child;
      node->last_status = SALTS_BT_RUNNING;
      *out_status = SALTS_BT_RUNNING;
      return SALTS_OK;
    }
    if (child_status == SALTS_BT_FAILURE) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_FAILURE;
      *out_status = SALTS_BT_FAILURE;
      return SALTS_OK;
    }
    {
      bool has_more = false;
      status = node->queue_finish(node->user, &has_more);
      if (status != SALTS_OK) {
        reset_node_state(impl, id);
        return status;
      }
      /* finish has committed the current item; a later reset must not abort it. */
      node->prepared = false;
      if (has_more) {
        reset_node_state(impl, child);
        node->cursor = SALTS_BT_INVALID_NODE_ID;
        node->last_status = SALTS_BT_RUNNING;
        *out_status = SALTS_BT_RUNNING;
      } else {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_SUCCESS;
        *out_status = SALTS_BT_SUCCESS;
      }
    }
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_PRECONDITION ||
      node->kind == SALTS_BT_NODE_IF_THEN_ELSE) {
    bool condition = false;
    salts_bt_node_id selected = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = node->first_child;
    int status;
    if (node->condition == NULL) return SALTS_EINVAL;
    status = node->condition(node->user, &condition);
    if (status != SALTS_OK) return status;
    if (node->kind == SALTS_BT_NODE_PRECONDITION && !condition) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_FAILURE;
      *out_status = SALTS_BT_FAILURE;
      return SALTS_OK;
    }
    if (node->kind == SALTS_BT_NODE_IF_THEN_ELSE && !condition) {
      if (child != SALTS_BT_INVALID_NODE_ID) {
        salts_bt_node *then_node = get_node(impl, child);
        if (then_node == NULL) return SALTS_EINVAL;
        child = then_node->next_sibling;
      }
      if (child == SALTS_BT_INVALID_NODE_ID) {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_FAILURE;
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }
    }
    selected = child;
    if (node->cursor != SALTS_BT_INVALID_NODE_ID &&
        node->cursor != selected) {
      reset_node_state(impl, node->cursor);
      node->cursor = SALTS_BT_INVALID_NODE_ID;
    }
    status = tick_node(impl, selected, depth + 1u, out_status);
    if (status != SALTS_OK) return status;
    node->last_status = *out_status;
    if (*out_status == SALTS_BT_RUNNING) {
      node->cursor = selected;
    } else {
      reset_node_state(impl, id);
      node->last_status = *out_status;
    }
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE) {
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    int status;
    if (child_id == SALTS_BT_INVALID_NODE_ID || node->limit == 0u)
      return SALTS_EINVAL;
    status = tick_node(impl, child_id, depth + 1u, &child_status);
    if (status != SALTS_OK) return status;
    if (child_status == SALTS_BT_RUNNING) {
      node->last_status = SALTS_BT_RUNNING;
      *out_status = SALTS_BT_RUNNING;
      return SALTS_OK;
    }
    if (child_status == SALTS_BT_FAILURE) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_SUCCESS;
      *out_status = SALTS_BT_SUCCESS;
      return SALTS_OK;
    }
    ++node->counter;
    reset_node_state(impl, child_id);
    if (node->counter >= node->limit) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_FAILURE;
      *out_status = SALTS_BT_FAILURE;
    } else {
      node->last_status = SALTS_BT_RUNNING;
      *out_status = SALTS_BT_RUNNING;
    }
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_WHILE_DO) {
    bool condition = false;
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    int status;
    if (node->condition == NULL || child_id == SALTS_BT_INVALID_NODE_ID)
      return SALTS_EINVAL;
    if (node->counter >= node->limit) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_SUCCESS;
      *out_status = SALTS_BT_SUCCESS;
      return SALTS_OK;
    }
    status = node->condition(node->user, &condition);
    if (status != SALTS_OK) return status;
    if (!condition) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_SUCCESS;
      *out_status = SALTS_BT_SUCCESS;
      return SALTS_OK;
    }
    status = tick_node(impl, child_id, depth + 1u, &child_status);
    if (status != SALTS_OK) return status;
    if (child_status == SALTS_BT_RUNNING) {
      node->last_status = SALTS_BT_RUNNING;
      *out_status = SALTS_BT_RUNNING;
      return SALTS_OK;
    }
    if (child_status == SALTS_BT_FAILURE) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_FAILURE;
      *out_status = SALTS_BT_FAILURE;
      return SALTS_OK;
    }
    ++node->counter;
    reset_node_state(impl, child_id);
    if (node->counter >= node->limit) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_SUCCESS;
      *out_status = SALTS_BT_SUCCESS;
    } else {
      node->last_status = SALTS_BT_RUNNING;
      *out_status = SALTS_BT_RUNNING;
    }
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_SWITCH) {
    size_t selected_index = 0u;
    size_t index = 0u;
    salts_bt_node_id selected = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id current = node->first_child;
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    int status;
    if (node->selector == NULL) return SALTS_EINVAL;
    status = node->selector(node->user, &selected_index);
    if (status != SALTS_OK) return status;
    while (current != SALTS_BT_INVALID_NODE_ID && index < selected_index) {
      const salts_bt_node *case_node = get_node(impl, current);
      if (case_node == NULL) return SALTS_EINVAL;
      current = case_node->next_sibling;
      ++index;
    }
    if (current == SALTS_BT_INVALID_NODE_ID || index != selected_index) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_FAILURE;
      *out_status = SALTS_BT_FAILURE;
      return SALTS_OK;
    }
    selected = current;
    if (node->cursor != SALTS_BT_INVALID_NODE_ID &&
        node->cursor != selected) {
      reset_node_state(impl, node->cursor);
      node->cursor = SALTS_BT_INVALID_NODE_ID;
    }
    status = tick_node(impl, selected, depth + 1u, &child_status);
    if (status != SALTS_OK) return status;
    if (child_status == SALTS_BT_RUNNING) {
      node->cursor = selected;
      node->last_status = child_status;
      *out_status = child_status;
    } else {
      reset_node_state(impl, id);
      node->last_status = child_status;
      *out_status = child_status;
    }
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_RUN_ONCE) {
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    int status;
    if (node->run_once_complete) {
      *out_status = node->last_status;
      return SALTS_OK;
    }
    if (child_id == SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
    status = tick_node(impl, child_id, depth + 1u, &child_status);
    if (status != SALTS_OK) return status;
    node->last_status = child_status;
    if (child_status != SALTS_BT_RUNNING) node->run_once_complete = true;
    *out_status = child_status;
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_TIMEOUT) {
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    uint64_t now;
    int status;
    if (child_id == SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
    now = cmeta_hrtime();
    if (!node->timer_started) {
      node->deadline_ns = node->duration_ns > UINT64_MAX - now
                              ? UINT64_MAX
                              : now + node->duration_ns;
      node->timer_started = true;
    }
    if (now >= node->deadline_ns) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_FAILURE;
      *out_status = SALTS_BT_FAILURE;
      return SALTS_OK;
    }
    status = tick_node(impl, child_id, depth + 1u, &child_status);
    if (status != SALTS_OK) return status;
    now = cmeta_hrtime();
    if (now >= node->deadline_ns) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_FAILURE;
      *out_status = SALTS_BT_FAILURE;
      return SALTS_OK;
    }
    if (child_status == SALTS_BT_RUNNING) {
      node->last_status = SALTS_BT_RUNNING;
      *out_status = SALTS_BT_RUNNING;
      return SALTS_OK;
    }
    reset_node_state(impl, id);
    node->last_status = child_status;
    *out_status = child_status;
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_DELAY) {
    salts_bt_node_status child_status = SALTS_BT_FAILURE;
    uint64_t now;
    int status;
    if (child_id == SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
    if (!node->delay_elapsed) {
      now = cmeta_hrtime();
      if (!node->timer_started) {
        node->deadline_ns = node->duration_ns > UINT64_MAX - now
                                ? UINT64_MAX
                                : now + node->duration_ns;
        node->timer_started = true;
      }
      if (now < node->deadline_ns) {
        node->last_status = SALTS_BT_RUNNING;
        *out_status = SALTS_BT_RUNNING;
        return SALTS_OK;
      }
      node->delay_elapsed = true;
      node->timer_started = false;
      node->deadline_ns = 0u;
    }
    status = tick_node(impl, child_id, depth + 1u, &child_status);
    if (status != SALTS_OK) return status;
    if (child_status == SALTS_BT_RUNNING) {
      node->last_status = SALTS_BT_RUNNING;
      *out_status = SALTS_BT_RUNNING;
      return SALTS_OK;
    }
    reset_node_state(impl, id);
    node->last_status = child_status;
    *out_status = child_status;
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_REPEAT) {
    if (node->limit == 0u) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_SUCCESS;
      *out_status = SALTS_BT_SUCCESS;
      return SALTS_OK;
    }
    while (node->counter < node->limit) {
      salts_bt_node_status child_status = SALTS_BT_FAILURE;
      int status;
      if (child_id == SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
      status = tick_node(impl, child_id, depth + 1u, &child_status);
      if (status != SALTS_OK) return status;
      if (child_status == SALTS_BT_RUNNING) {
        node->last_status = SALTS_BT_RUNNING;
        *out_status = SALTS_BT_RUNNING;
        return SALTS_OK;
      }
      ++node->counter;
      reset_node_state(impl, child_id);
      child_id = node->first_child;
    }
    reset_node_state(impl, id);
    node->last_status = SALTS_BT_SUCCESS;
    *out_status = SALTS_BT_SUCCESS;
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_RETRY) {
    while (node->counter < node->limit) {
      salts_bt_node_status child_status = SALTS_BT_FAILURE;
      int status;
      if (child_id == SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
      status = tick_node(impl, child_id, depth + 1u, &child_status);
      if (status != SALTS_OK) return status;
      if (child_status == SALTS_BT_RUNNING) {
        node->last_status = SALTS_BT_RUNNING;
        *out_status = SALTS_BT_RUNNING;
        return SALTS_OK;
      }
      if (child_status == SALTS_BT_SUCCESS) {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_SUCCESS;
        *out_status = SALTS_BT_SUCCESS;
        return SALTS_OK;
      }
      ++node->counter;
      reset_node_state(impl, child_id);
      if (node->counter >= node->limit) {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_FAILURE;
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }
      child_id = node->first_child;
    }
  }

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

  if (node->kind == SALTS_BT_NODE_PARALLEL) {
    bool any_failure = false;
    bool any_running = false;
    salts_bt_node_id current = node->first_child;
    size_t child_index = 0u;
    if (current == SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
    while (current != SALTS_BT_INVALID_NODE_ID) {
      salts_bt_node *child = get_node(impl, current);
      salts_bt_node_id next;
      salts_bt_node_status child_status = SALTS_BT_FAILURE;
      int status;
      if (child == NULL) return SALTS_EINVAL;
      next = child->next_sibling;
      if (child->evaluated && child->last_status != SALTS_BT_RUNNING) {
        child_status = child->last_status;
        status = SALTS_OK;
      } else {
        if (node->parallel_enter != NULL) {
          const bool first_activation = !child->parallel_branch_prepared;
          if (first_activation) child->parallel_branch_prepared = true;
          status = node->parallel_enter(node->user, child_index, first_activation);
          if (status != SALTS_OK) {
            reset_node_state(impl, id);
            return status;
          }
        }
        status = tick_node(impl, current, depth + 1u, &child_status);
        if (status != SALTS_OK) {
          reset_node_state(impl, id);
          return status;
        }
        if (node->parallel_finish != NULL &&
            child->parallel_branch_prepared &&
            child_status != SALTS_BT_RUNNING) {
          status = node->parallel_finish(node->user, child_index, child_status);
          if (status != SALTS_OK) {
            reset_node_state(impl, id);
            return status;
          }
          child->parallel_branch_prepared = false;
        }
      }
      if (child_status == SALTS_BT_FAILURE) any_failure = true;
      else if (child_status == SALTS_BT_RUNNING) any_running = true;
      current = next;
      ++child_index;
    }
    if (any_failure) {
      reset_node_state(impl, id);
      node->last_status = SALTS_BT_FAILURE;
      *out_status = SALTS_BT_FAILURE;
      return SALTS_OK;
    }
    if (any_running) {
      node->last_status = SALTS_BT_RUNNING;
      *out_status = SALTS_BT_RUNNING;
      return SALTS_OK;
    }
    reset_node_state(impl, id);
    node->last_status = SALTS_BT_SUCCESS;
    *out_status = SALTS_BT_SUCCESS;
    return SALTS_OK;
  }

  if (node->kind == SALTS_BT_NODE_REACTIVE_SEQUENCE) {
    salts_bt_node_id current = node->first_child;
    if (current == SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
    while (current != SALTS_BT_INVALID_NODE_ID) {
      salts_bt_node *child = get_node(impl, current);
      salts_bt_node_id next;
      salts_bt_node_status child_status = SALTS_BT_FAILURE;
      int status;
      if (child == NULL) return SALTS_EINVAL;
      next = child->next_sibling;
      status = tick_node(impl, current, depth + 1u, &child_status);
      if (status != SALTS_OK) return status;
      if (child_status == SALTS_BT_FAILURE) {
        reset_node_state(impl, id);
        node->last_status = SALTS_BT_FAILURE;
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }
      if (child_status == SALTS_BT_RUNNING) {
        salts_bt_node_id later = next;
        while (later != SALTS_BT_INVALID_NODE_ID) {
          salts_bt_node *later_node = get_node(impl, later);
          salts_bt_node_id later_next;
          if (later_node == NULL) return SALTS_EINVAL;
          later_next = later_node->next_sibling;
          reset_node_state(impl, later);
          later = later_next;
        }
        node->cursor = current;
        node->last_status = SALTS_BT_RUNNING;
        *out_status = SALTS_BT_RUNNING;
        return SALTS_OK;
      }
      current = next;
    }
    reset_node_state(impl, id);
    node->last_status = SALTS_BT_SUCCESS;
    *out_status = SALTS_BT_SUCCESS;
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
  config->max_control_iterations = 64u;
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
      config->max_control_iterations == 0u ||
      config->max_control_iterations > SALTS_BT_MAX_CONTROL_ITERATIONS ||
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
  impl->max_control_iterations = config->max_control_iterations;
  impl->root = SALTS_BT_INVALID_NODE_ID;
  tree->impl = impl;
  return SALTS_OK;
}

int salts_bt_tree_add_node_with_limit(salts_bt_tree *tree,
                                      salts_bt_node_id parent_id,
                                      salts_bt_node_kind kind,
                                      salts_bt_leaf_fn handler,
                                      void *user,
                                      size_t limit,
                                      salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl = tree_impl(tree);
  salts_bt_node *parent = NULL;
  salts_bt_node *node;
  salts_bt_node_id id;
  size_t depth = 1u;

  if (out_node_id != NULL) *out_node_id = SALTS_BT_INVALID_NODE_ID;
  if (impl == NULL || out_node_id == NULL) return SALTS_EINVAL;
  if (impl->busy) return SALTS_EBUSY;
  if (!node_kind_valid(kind)) return SALTS_EINVAL;
  if ((kind == SALTS_BT_NODE_LEAF && handler == NULL) ||
      (kind != SALTS_BT_NODE_LEAF && handler != NULL))
    return SALTS_EINVAL;
  if (kind == SALTS_BT_NODE_RETRY || kind == SALTS_BT_NODE_WHILE_DO ||
      kind == SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE) {
    if (limit == 0u || limit > impl->max_control_iterations) return SALTS_EINVAL;
  } else if (kind == SALTS_BT_NODE_REPEAT) {
    if (limit > impl->max_control_iterations) return SALTS_EINVAL;
  } else if (limit != 0u) {
    return SALTS_EINVAL;
  }

  if (parent_id == SALTS_BT_INVALID_NODE_ID) {
    if (impl->root != SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
  } else {
    parent = get_node(impl, parent_id);
    if (parent == NULL || !node_kind_accepts_children(parent->kind) ||
        (node_kind_is_single_child(parent->kind) && parent->child_count >= 1u) ||
        (parent->kind == SALTS_BT_NODE_IF_THEN_ELSE && parent->child_count >= 2u))
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
  node->limit = limit;
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


static int tick_node(salts_bt_impl *impl, salts_bt_node_id id,
                     size_t depth, salts_bt_node_status *out_status) {
  salts_bt_node *node = get_node(impl, id);
  int status;
  if (node == NULL) return SALTS_EINVAL;
  status = tick_node_impl(impl, id, depth, out_status);
  if (status == SALTS_OK) node->evaluated = true;
  return status;
}

static int validate_impl(const salts_bt_impl *impl) {
  size_t index;
  if (impl == NULL || !node_id_valid(impl, impl->root)) return SALTS_EINVAL;
  if (impl->nodes[impl->root].parent != SALTS_BT_INVALID_NODE_ID) return SALTS_EINVAL;
  for (index = 1u; index <= impl->node_count; ++index) {
    const salts_bt_node *node = &impl->nodes[index];
    if (node->depth == 0u || node->depth > impl->max_depth ||
        !node_kind_valid(node->kind))
      return SALTS_EINVAL;
    if (node->kind == SALTS_BT_NODE_LEAF) {
      if (node->handler == NULL || node->child_count != 0u) return SALTS_EINVAL;
    } else if (node->handler != NULL) {
      return SALTS_EINVAL;
    }
    if (node_kind_is_single_child(node->kind) && node->child_count != 1u)
      return SALTS_EINVAL;
    if ((node->kind == SALTS_BT_NODE_PARALLEL ||
         node->kind == SALTS_BT_NODE_REACTIVE_SEQUENCE ||
         node->kind == SALTS_BT_NODE_SWITCH) && node->child_count == 0u)
      return SALTS_EINVAL;
    if (node->kind == SALTS_BT_NODE_IF_THEN_ELSE &&
        (node->condition == NULL || node->child_count == 0u || node->child_count > 2u))
      return SALTS_EINVAL;
    if ((node->kind == SALTS_BT_NODE_PRECONDITION ||
         node->kind == SALTS_BT_NODE_WHILE_DO) && node->condition == NULL)
      return SALTS_EINVAL;
    if (node->kind == SALTS_BT_NODE_SWITCH && node->selector == NULL)
      return SALTS_EINVAL;
    if ((node->parallel_enter == NULL) !=
        (node->parallel_finish == NULL))
      return SALTS_EINVAL;
    if (node->kind != SALTS_BT_NODE_PARALLEL &&
        (node->parallel_enter != NULL || node->parallel_finish != NULL ||
         node->parallel_abort != NULL))
      return SALTS_EINVAL;
    if (node->parallel_abort != NULL && node->parallel_enter == NULL)
      return SALTS_EINVAL;
    if (node->kind == SALTS_BT_NODE_PIPELINE_SEQUENCE &&
        (node->pipeline_prepare == NULL || node->pipeline_complete == NULL))
      return SALTS_EINVAL;
    if (node->kind == SALTS_BT_NODE_ENTRY_UPDATED && node->entry_changed == NULL)
      return SALTS_EINVAL;
    if (node->kind == SALTS_BT_NODE_CONSUME_QUEUE &&
        (node->queue_begin == NULL || node->queue_finish == NULL))
      return SALTS_EINVAL;
    if ((node->kind == SALTS_BT_NODE_RETRY &&
         (node->limit == 0u || node->limit > impl->max_control_iterations)) ||
        (node->kind == SALTS_BT_NODE_WHILE_DO &&
         (node->limit == 0u || node->limit > impl->max_control_iterations)) ||
        (node->kind == SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE &&
         (node->limit == 0u || node->limit > impl->max_control_iterations)) ||
        (node->kind == SALTS_BT_NODE_REPEAT &&
         node->limit > impl->max_control_iterations))
      return SALTS_EINVAL;
  }
  return SALTS_OK;
}

int salts_bt_tree_validate(const salts_bt_tree *tree) {
  return validate_impl(tree_const_impl(tree));
}

int salts_bt_tree_add_node(salts_bt_tree *tree,
                           salts_bt_node_id parent_id,
                           salts_bt_node_kind kind,
                           salts_bt_leaf_fn handler,
                           void *user,
                           salts_bt_node_id *out_node_id) {
  return salts_bt_tree_add_node_with_limit(tree, parent_id, kind, handler,
                                           user, 0u, out_node_id);
}

int salts_bt_tree_add_parallel_node(salts_bt_tree *tree,
                                    salts_bt_node_id parent_id,
                                    salts_bt_parallel_enter_fn enter,
                                    salts_bt_parallel_finish_fn finish,
                                    salts_bt_parallel_abort_fn abort,
                                    void *user,
                                    salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl;
  salts_bt_node *node;
  int status;
  if (enter == NULL || finish == NULL || out_node_id == NULL)
    return SALTS_EINVAL;
  status = salts_bt_tree_add_node_with_limit(tree, parent_id,
                                             SALTS_BT_NODE_PARALLEL, NULL,
                                             user, 0u, out_node_id);
  if (status != SALTS_OK) return status;
  impl = tree_impl(tree);
  node = get_node(impl, *out_node_id);
  if (node == NULL) return SALTS_EINVAL;
  node->parallel_enter = enter;
  node->parallel_finish = finish;
  node->parallel_abort = abort;
  return SALTS_OK;
}

int salts_bt_tree_add_pipeline_node(salts_bt_tree *tree,
                                    salts_bt_node_id parent_id,
                                    salts_bt_pipeline_prepare_fn prepare,
                                    salts_bt_pipeline_complete_fn complete,
                                    salts_bt_pipeline_abort_fn abort,
                                    void *user,
                                    salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl;
  salts_bt_node *node;
  int status;
  if (prepare == NULL || complete == NULL || out_node_id == NULL)
    return SALTS_EINVAL;
  status = salts_bt_tree_add_node_with_limit(tree, parent_id,
                                             SALTS_BT_NODE_PIPELINE_SEQUENCE,
                                             NULL, user, 0u, out_node_id);
  if (status != SALTS_OK) return status;
  impl = tree_impl(tree);
  node = get_node(impl, *out_node_id);
  if (node == NULL) return SALTS_EINVAL;
  node->pipeline_prepare = prepare;
  node->pipeline_complete = complete;
  node->pipeline_abort = abort;
  return SALTS_OK;
}

int salts_bt_tree_add_entry_updated_node(salts_bt_tree *tree,
                                         salts_bt_node_id parent_id,
                                         salts_bt_entry_changed_fn changed,
                                         void *user,
                                         salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl;
  salts_bt_node *node;
  int status;
  if (changed == NULL || out_node_id == NULL) return SALTS_EINVAL;
  status = salts_bt_tree_add_node_with_limit(tree, parent_id,
                                             SALTS_BT_NODE_ENTRY_UPDATED,
                                             NULL, user, 0u, out_node_id);
  if (status != SALTS_OK) return status;
  impl = tree_impl(tree);
  node = get_node(impl, *out_node_id);
  if (node == NULL) return SALTS_EINVAL;
  node->entry_changed = changed;
  return SALTS_OK;
}

int salts_bt_tree_add_consume_queue_node(salts_bt_tree *tree,
                                         salts_bt_node_id parent_id,
                                         salts_bt_queue_begin_fn begin,
                                         salts_bt_queue_finish_fn finish,
                                         salts_bt_queue_abort_fn abort,
                                         void *user,
                                         salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl;
  salts_bt_node *node;
  int status;
  if (begin == NULL || finish == NULL || out_node_id == NULL)
    return SALTS_EINVAL;
  status = salts_bt_tree_add_node_with_limit(tree, parent_id,
                                             SALTS_BT_NODE_CONSUME_QUEUE,
                                             NULL, user, 0u, out_node_id);
  if (status != SALTS_OK) return status;
  impl = tree_impl(tree);
  node = get_node(impl, *out_node_id);
  if (node == NULL) return SALTS_EINVAL;
  node->queue_begin = begin;
  node->queue_finish = finish;
  node->queue_abort = abort;
  return SALTS_OK;
}

int salts_bt_tree_add_condition_node(salts_bt_tree *tree,
                                     salts_bt_node_id parent_id,
                                     salts_bt_node_kind kind,
                                     salts_bt_condition_fn condition,
                                     void *user,
                                     size_t limit,
                                     salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl;
  salts_bt_node *node;
  int status;
  if (condition == NULL || out_node_id == NULL ||
      (kind != SALTS_BT_NODE_IF_THEN_ELSE &&
       kind != SALTS_BT_NODE_PRECONDITION &&
       kind != SALTS_BT_NODE_WHILE_DO))
    return SALTS_EINVAL;
  status = salts_bt_tree_add_node_with_limit(tree, parent_id, kind, NULL,
                                             user, limit, out_node_id);
  if (status != SALTS_OK) return status;
  impl = tree_impl(tree);
  node = get_node(impl, *out_node_id);
  if (node == NULL) return SALTS_EINVAL;
  node->condition = condition;
  return SALTS_OK;
}

int salts_bt_tree_add_switch_node(salts_bt_tree *tree,
                                  salts_bt_node_id parent_id,
                                  salts_bt_selector_fn selector,
                                  void *user,
                                  salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl;
  salts_bt_node *node;
  int status;
  if (selector == NULL || out_node_id == NULL) return SALTS_EINVAL;
  status = salts_bt_tree_add_node_with_limit(tree, parent_id,
                                             SALTS_BT_NODE_SWITCH, NULL,
                                             user, 0u, out_node_id);
  if (status != SALTS_OK) return status;
  impl = tree_impl(tree);
  node = get_node(impl, *out_node_id);
  if (node == NULL) return SALTS_EINVAL;
  node->selector = selector;
  return SALTS_OK;
}

int salts_bt_tree_add_leaf(salts_bt_tree *tree,
                           salts_bt_node_id parent_id,
                           const salts_bt_leaf_ops *ops,
                           void *user,
                           salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl;
  salts_bt_node *node;
  int status;
  if (ops == NULL || ops->tick == NULL || out_node_id == NULL)
    return SALTS_EINVAL;
  status = salts_bt_tree_add_node(tree, parent_id, SALTS_BT_NODE_LEAF,
                                  ops->tick, user, out_node_id);
  if (status != SALTS_OK) return status;
  impl = tree_impl(tree);
  node = get_node(impl, *out_node_id);
  if (node == NULL) return SALTS_EINVAL;
  node->halt = ops->halt;
  return SALTS_OK;
}

int salts_bt_tree_add_timed_node(salts_bt_tree *tree,
                                 salts_bt_node_id parent_id,
                                 salts_bt_node_kind kind,
                                 uint64_t duration_ns,
                                 salts_bt_node_id *out_node_id) {
  salts_bt_impl *impl;
  salts_bt_node *node;
  int status;
  if (out_node_id == NULL ||
      (kind != SALTS_BT_NODE_TIMEOUT && kind != SALTS_BT_NODE_DELAY))
    return SALTS_EINVAL;
  status = salts_bt_tree_add_node(tree, parent_id, kind, NULL, NULL, out_node_id);
  if (status != SALTS_OK) return status;
  impl = tree_impl(tree);
  node = get_node(impl, *out_node_id);
  if (node == NULL) return SALTS_EINVAL;
  node->duration_ns = duration_ns;
  return SALTS_OK;
}

int salts_bt_tree_tick(salts_bt_tree *tree,
                       salts_bt_node_status *out_status) {
  salts_bt_impl *impl = tree_impl(tree);
  int status;
  if (impl == NULL || out_status == NULL) return SALTS_EINVAL;
  if (impl->busy) return SALTS_EBUSY;
  status = validate_impl(impl);
  impl->busy = true;
  if (status != SALTS_OK) {
    if (node_id_valid(impl, impl->root))
      reset_node_state(impl, impl->root);
    impl->busy = false;
    return status;
  }
  status = tick_node(impl, impl->root, 1u, out_status);
  if (status != SALTS_OK && node_id_valid(impl, impl->root))
    reset_node_state(impl, impl->root);
  impl->busy = false;
  return status;
}

int salts_bt_tree_reset(salts_bt_tree *tree) {
  salts_bt_impl *impl = tree_impl(tree);
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->busy) return SALTS_EBUSY;
  impl->busy = true;
  if (impl->root != SALTS_BT_INVALID_NODE_ID)
    reset_node_state(impl, impl->root);
  impl->busy = false;
  return SALTS_OK;
}

int salts_bt_tree_cancel(salts_bt_tree *tree) {
  /* reset performs the same lifecycle cleanup while retaining tree topology. */
  return salts_bt_tree_reset(tree);
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
  if (impl->busy) return SALTS_EBUSY;
  impl->busy = true;
  if (impl->root != SALTS_BT_INVALID_NODE_ID)
    reset_node_state(impl, impl->root);
  free(impl->nodes);
  memset(impl, 0, sizeof(*impl));
  free(impl);
  tree->impl = NULL;
  return SALTS_OK;
}
