#ifndef JINJA_CMETA_CELLS_H
#define JINJA_CMETA_CELLS_H

#include "jinja_cmeta_value.h"
#include "jinja_cmeta_memory.h"

typedef struct JINJA_CMETA_ACTIVATION JINJA_CMETA_ACTIVATION;

typedef struct JINJA_CMETA_CELL_VALUE {
  int bound;
  int assigned;
  int exported;
  JINJA_CMETA_VALUE value;
} JINJA_CMETA_CELL_VALUE;

/* Private single-threaded owner. Do not copy/move while initialized.
 * Template and all VALUE payloads are borrowed until destroy; cells own no payload.
 * Activation/cell addresses survive clear and new, but not destroy.
 * Initialize from zero; destroy only when no activation/cell users remain. */
typedef struct JINJA_CMETA_CELL_STORE {
  const JINJA_CMETA_TEMPLATE *templ;
  JINJA_CMETA_ACTIVATION *first;
  size_t activation_count;
  size_t cell_count;
  size_t max_activations;
  size_t max_cells;
  JINJA_CMETA_MEMORY local_memory;
  JINJA_CMETA_MEMORY *memory;
} JINJA_CMETA_CELL_STORE;

/* Zero quotas are valid. Overflow returns CAPACITY; invalid input returns
 * INVALID_ARGUMENT. Failure leaves the zero store unchanged. Initialization
 * uses local_memory with no byte cap. Before creating any activation, a render
 * may bind memory to its shared ledger, which must outlive store destruction.
 * Never rebind memory while activations exist. */
JINJA_CMETA_STATUS jinja_cmeta_cells_init(JINJA_CMETA_CELL_STORE *store,
    const JINJA_CMETA_TEMPLATE *templ, size_t max_activations, size_t max_cells);
void jinja_cmeta_cells_destroy(JINJA_CMETA_CELL_STORE *store);

/* parent is a live defining activation from this store, never a dynamic caller.
 * owner is a compiled activation-owning scope. On admission/OOM/argument failure,
 * counters and *out remain unchanged. Empty activations still consume quota. */
JINJA_CMETA_STATUS jinja_cmeta_activation_new(JINJA_CMETA_CELL_STORE *store,
    JINJA_CMETA_ACTIVATION *parent, size_t owner, JINJA_CMETA_ACTIVATION **out);

/* Explicit defining template; all templates share the store's cumulative quotas.
 * A parent must belong to both this store and this template. The template is
 * borrowed until store destruction, even after its execution has returned. */
JINJA_CMETA_STATUS jinja_cmeta_activation_new_for_template(JINJA_CMETA_CELL_STORE *store,
    const JINJA_CMETA_TEMPLATE *templ, JINJA_CMETA_ACTIVATION *parent,
    size_t owner, JINJA_CMETA_ACTIVATION **out);

/* Resolve a compiled cell through the defining activation chain. An unrelated
 * owner returns METADATA; invalid index/arguments return INVALID_ARGUMENT.
 * *out is unchanged on failure. A successful view remains valid until destroy. */
JINJA_CMETA_STATUS jinja_cmeta_activation_cell(JINJA_CMETA_ACTIVATION *activation,
    size_t cell, JINJA_CMETA_CELL_VALUE **out);

/* Clear only scope-local bindings, never their ALIAS source. Requires a scope
 * owned by activation; wrong owner returns METADATA without changing values. */
JINJA_CMETA_STATUS jinja_cmeta_activation_clear(JINJA_CMETA_ACTIVATION *activation,
    size_t scope);

#endif
