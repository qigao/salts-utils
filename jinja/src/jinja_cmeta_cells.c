#include "jinja_cmeta_cells.h"

#include <stdlib.h>
#include <string.h>

struct JINJA_CMETA_ACTIVATION {
  JINJA_CMETA_ACTIVATION *next;
  JINJA_CMETA_ACTIVATION *parent;
  JINJA_CMETA_CELL_STORE *store;
  const JINJA_CMETA_TEMPLATE *templ;
  size_t owner;
  size_t count;
  JINJA_CMETA_CELL_VALUE values[];
};

JINJA_CMETA_STATUS jinja_cmeta_cells_init(JINJA_CMETA_CELL_STORE *store,
    const JINJA_CMETA_TEMPLATE *templ, size_t max_activations, size_t max_cells) {
  if (store == NULL || store->templ != NULL || templ == NULL ||
      templ->lexical_scope_count == 0u || templ->lexical_scopes == NULL)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (max_activations > SIZE_MAX / sizeof(JINJA_CMETA_ACTIVATION) ||
      max_cells > (SIZE_MAX - max_activations * sizeof(JINJA_CMETA_ACTIVATION)) /
          sizeof(JINJA_CMETA_CELL_VALUE))
    return JINJA_CMETA_ERR_CAPACITY;
  *store = (JINJA_CMETA_CELL_STORE){.templ = templ,
      .max_activations = max_activations, .max_cells = max_cells,
      .local_memory = {.limit = SIZE_MAX}};
  store->memory = &store->local_memory;
  return JINJA_CMETA_OK;
}

void jinja_cmeta_cells_destroy(JINJA_CMETA_CELL_STORE *store) {
  if (store == NULL) return;
  JINJA_CMETA_ACTIVATION *activation = store->first;
  while (activation != NULL) {
    JINJA_CMETA_ACTIVATION *next = activation->next;
    jinja_cmeta_memory_deallocate(store->memory, activation,
        sizeof(*activation) + activation->count * sizeof(JINJA_CMETA_CELL_VALUE));
    activation = next;
  }
  *store = (JINJA_CMETA_CELL_STORE){0};
}

JINJA_CMETA_STATUS jinja_cmeta_activation_new(JINJA_CMETA_CELL_STORE *store,
    JINJA_CMETA_ACTIVATION *parent, size_t owner, JINJA_CMETA_ACTIVATION **out) {
  return jinja_cmeta_activation_new_for_template(store,
      store != NULL ? store->templ : NULL, parent, owner, out);
}

JINJA_CMETA_STATUS jinja_cmeta_activation_new_for_template(JINJA_CMETA_CELL_STORE *store,
    const JINJA_CMETA_TEMPLATE *templ, JINJA_CMETA_ACTIVATION *parent,
    size_t owner, JINJA_CMETA_ACTIVATION **out) {
  if (store == NULL || store->templ == NULL || templ == NULL || out == NULL ||
      templ->lexical_scopes == NULL || owner >= templ->lexical_scope_count ||
      (parent != NULL && (parent->store != store || parent->templ != templ)))
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  const JINJA_CMETA_LEXICAL_SCOPE *scope = &templ->lexical_scopes[owner];
  if (scope->owner != owner || (scope->parent == SIZE_MAX && parent != NULL) ||
      (scope->parent != SIZE_MAX && (parent == NULL || scope->parent >= templ->lexical_scope_count ||
          parent->owner != templ->lexical_scopes[scope->parent].owner)))
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  const size_t count = scope->cell_count;
  if (store->activation_count >= store->max_activations ||
      count > store->max_cells - store->cell_count ||
      count > (SIZE_MAX - sizeof(JINJA_CMETA_ACTIVATION)) / sizeof(JINJA_CMETA_CELL_VALUE))
    return JINJA_CMETA_ERR_CAPACITY;
  const size_t bytes = sizeof(JINJA_CMETA_ACTIVATION) + count * sizeof(JINJA_CMETA_CELL_VALUE);
  void *storage = NULL;
  const stl_status allocation = jinja_cmeta_memory_allocate(store->memory, bytes, &storage);
  if (allocation != STL_OK) return jinja_cmeta_memory_status(allocation);
  JINJA_CMETA_ACTIVATION *activation = (JINJA_CMETA_ACTIVATION *)storage;
  memset(activation, 0, bytes);
  activation->store = store;
  activation->templ = templ;
  activation->parent = parent;
  activation->owner = owner;
  activation->count = count;
  activation->next = store->first;
  store->first = activation;
  ++store->activation_count;
  store->cell_count += count;
  *out = activation;
  return JINJA_CMETA_OK;
}

JINJA_CMETA_STATUS jinja_cmeta_activation_cell(JINJA_CMETA_ACTIVATION *activation,
    size_t cell, JINJA_CMETA_CELL_VALUE **out) {
  if (activation == NULL || out == NULL || cell >= activation->templ->cell_count)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  const JINJA_CMETA_CELL *layout = &activation->templ->cells[cell];
  /* Only immutable defining-parent edges are traversed, O(lexical depth). */
  for (; activation != NULL; activation = activation->parent) {
    if (activation->owner != layout->owner) continue;
    if (layout->slot >= activation->count) return JINJA_CMETA_ERR_METADATA;
    *out = &activation->values[layout->slot];
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_METADATA;
}

JINJA_CMETA_STATUS jinja_cmeta_activation_clear(JINJA_CMETA_ACTIVATION *activation,
    size_t scope) {
  if (activation == NULL || scope >= activation->templ->lexical_scope_count)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  const JINJA_CMETA_TEMPLATE *templ = activation->templ;
  const JINJA_CMETA_LEXICAL_SCOPE *frame = &templ->lexical_scopes[scope];
  if (frame->owner != activation->owner) return JINJA_CMETA_ERR_METADATA;
  /* Compiled layout is immutable and compiler-validated. Clearing never
   * releases borrowed payload or the storage referenced by a closure. */
  for (size_t i = 0u; i < frame->binding_count; ++i) {
    size_t cell = templ->cell_bindings[frame->first_binding + i].cell;
    activation->values[templ->cells[cell].slot] = (JINJA_CMETA_CELL_VALUE){0};
  }
  return JINJA_CMETA_OK;
}
