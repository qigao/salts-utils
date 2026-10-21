#include <salts/process_retainer.h>
#include <salts/error_codes.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum process_retainer_slot_state {
  PROCESS_RETAINER_EMPTY = 0,
  PROCESS_RETAINER_RESERVED = 1,
  PROCESS_RETAINER_ADOPTED = 2
} process_retainer_slot_state;

typedef struct process_retainer_slot {
  uint64_t generation;
  process_retainer_slot_state state;
  cflow_process process;
  void *payload;
  cflow_process_retainer_release_fn release;
} process_retainer_slot;

typedef struct process_retainer_impl {
  process_retainer_slot *slots;
  size_t capacity;
} process_retainer_impl;

static process_retainer_impl *retainer_impl(cflow_process_retainer *owner) {
  return owner != NULL ? (process_retainer_impl *)owner->impl : NULL;
}

static process_retainer_slot *retainer_reserved_slot(
    process_retainer_impl *impl, cflow_process_retainer_token token) {
  process_retainer_slot *slot;
  if (impl == NULL || token.index >= impl->capacity || token.generation == 0u)
    return NULL;
  slot = &impl->slots[token.index];
  if (slot->state != PROCESS_RETAINER_RESERVED ||
      slot->generation != token.generation)
    return NULL;
  return slot;
}

int cflow_process_retainer_init(cflow_process_retainer *owner, size_t capacity) {
  process_retainer_impl *impl;
  if (owner == NULL || owner->impl != NULL || capacity == 0u ||
      capacity > SIZE_MAX / sizeof(process_retainer_slot))
    return SALTS_EINVAL;
  impl = (process_retainer_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->slots = (process_retainer_slot *)calloc(capacity, sizeof(*impl->slots));
  if (impl->slots == NULL) {
    free(impl);
    return SALTS_ENOMEM;
  }
  impl->capacity = capacity;
  owner->impl = impl;
  return SALTS_OK;
}

int cflow_process_retainer_reserve(cflow_process_retainer *owner,
                                  cflow_process_retainer_token *out) {
  process_retainer_impl *impl = retainer_impl(owner);
  size_t index;
  if (out != NULL) *out = (cflow_process_retainer_token){0};
  if (impl == NULL || out == NULL) return SALTS_EINVAL;
  for (index = 0u; index < impl->capacity; ++index) {
    process_retainer_slot *slot = &impl->slots[index];
    if (slot->state != PROCESS_RETAINER_EMPTY) continue;
    ++slot->generation;
    if (slot->generation == 0u) ++slot->generation;
    slot->state = PROCESS_RETAINER_RESERVED;
    out->index = index;
    out->generation = slot->generation;
    return SALTS_OK;
  }
  return SALTS_ENOBUFS;
}

int cflow_process_retainer_release(cflow_process_retainer *owner,
                                  cflow_process_retainer_token token) {
  process_retainer_slot *slot =
      retainer_reserved_slot(retainer_impl(owner), token);
  if (slot == NULL) return SALTS_EINVAL;
  slot->state = PROCESS_RETAINER_EMPTY;
  return SALTS_OK;
}

int cflow_process_retainer_adopt(
    cflow_process_retainer *owner, cflow_process_retainer_token token,
    cflow_process *source, void *payload,
    cflow_process_retainer_release_fn release) {
  process_retainer_slot *slot =
      retainer_reserved_slot(retainer_impl(owner), token);
  cflow_process_stats stats = {0};
  if (slot == NULL || source == NULL || source->impl == NULL ||
      payload == NULL || release == NULL)
    return SALTS_EINVAL;
  /*
   * Admission must already be closed. The caller exclusively owns the driver
   * and callback lifetime at this transfer boundary. Only Process, not raw
   * native handles or a second process state machine, changes owners.
   */
  if (!cflow_process_get_stats(source, &stats) || !stats.close_requested)
    return SALTS_EBUSY;
  slot->process = *source;
  slot->payload = payload;
  slot->release = release;
  slot->state = PROCESS_RETAINER_ADOPTED;
  source->impl = NULL;
  return SALTS_OK;
}

int cflow_process_retainer_run_ready(cflow_process_retainer *owner,
                                     size_t max_steps,
                                     size_t *out_reaped,
                                     size_t *out_pending) {
  process_retainer_impl *impl = retainer_impl(owner);
  size_t index;
  int first_error = SALTS_OK;
  size_t reaped = 0u;
  size_t pending = 0u;
  if (out_reaped != NULL) *out_reaped = 0u;
  if (out_pending != NULL) *out_pending = 0u;
  if (impl == NULL || max_steps == 0u ||
      out_reaped == NULL || out_pending == NULL)
    return SALTS_EINVAL;

  for (index = 0u; index < impl->capacity; ++index) {
    process_retainer_slot *slot = &impl->slots[index];
    if (slot->state == PROCESS_RETAINER_RESERVED) {
      ++pending;
      continue;
    }
    if (slot->state != PROCESS_RETAINER_ADOPTED) continue;
    {
      size_t progressed = 0u;
      bool quiescent = false;
      int status = cflow_process_shutdown_step(
          &slot->process, max_steps, &progressed, &quiescent);
      if (status != SALTS_OK && status != SALTS_EBUSY &&
          first_error == SALTS_OK)
        first_error = status;
      if (quiescent) {
        int destroy_status = cflow_process_destroy(&slot->process);
        if (destroy_status != SALTS_OK && first_error == SALTS_OK)
          first_error = destroy_status;
        if (slot->process.impl == NULL) {
          cflow_process_retainer_release_fn release = slot->release;
          void *payload = slot->payload;
          slot->state = PROCESS_RETAINER_EMPTY;
          slot->payload = NULL;
          slot->release = NULL;
          release(payload);
          ++reaped;
          continue;
        }
      }
    }
    ++pending;
  }
  *out_reaped = reaped;
  *out_pending = pending;
  if (first_error != SALTS_OK) return first_error;
  return pending == 0u ? SALTS_OK : SALTS_EBUSY;
}

int cflow_process_retainer_destroy(cflow_process_retainer *owner) {
  process_retainer_impl *impl = retainer_impl(owner);
  size_t index;
  if (impl == NULL) return SALTS_EINVAL;
  for (index = 0u; index < impl->capacity; ++index) {
    if (impl->slots[index].state != PROCESS_RETAINER_EMPTY)
      return SALTS_EBUSY;
  }
  free(impl->slots);
  free(impl);
  owner->impl = NULL;
  return SALTS_OK;
}
