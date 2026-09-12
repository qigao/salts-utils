#ifndef JINJA_CMETA_VALUES_H
#define JINJA_CMETA_VALUES_H

#include "jinja_cmeta_value.h"
#include "jinja_cmeta_memory.h"
#include <cstl/vec_alloc.h>

/* Private single-threaded render owner. Initialize from zero and bind memory and limit
 * before use. Payloads inside VALUEs are borrowed, not independently destroyed.
 * The caller's committed slot count is authoritative; chunk ranges index it.
 * Snapshot spans remain contiguous and address-stable until destroy. Only an
 * unpublished context under construction may grow; refresh its pointer after
 * each successful append and never extend it after publishing the context.
 * No reset/reuse is permitted while any closure, iterator, or context borrows
 * a span. Destroy after execution stops, before releasing borrowed templates.
 * Counts bound slots and directory entries, not total retained payload bytes.
 */
typedef struct JINJA_CMETA_VALUE_STORE {
  vec_alloc_t *chunks;
  JINJA_CMETA_MEMORY *memory;
  size_t limit;
} JINJA_CMETA_VALUE_STORE;

typedef struct JINJA_CMETA_VALUE_CHUNK {
  vec_alloc_t *values;
  size_t first;
  size_t end;
} JINJA_CMETA_VALUE_CHUNK;

enum { JINJA_CMETA_VALUE_PAIR_WIDTH = 2 };

static JINJA_CMETA_STATUS jinja_cmeta_values_status(stl_status status) {
  switch (status) {
    case STL_OK: return JINJA_CMETA_OK;
    case STL_OUT_OF_MEMORY: return JINJA_CMETA_ERR_OUT_OF_MEMORY;
    case STL_CAPACITY_EXCEEDED: return JINJA_CMETA_ERR_CAPACITY;
    default: return JINJA_CMETA_ERR_METADATA;
  }
}

/* CSTL byte vectors deliberately copy only storage descriptors / borrowed
 * VALUE records. This owner explicitly destroys each nested vector once;
 * copying a directory entry must not duplicate or release its allocation. */
static JINJA_CMETA_STATUS jinja_cmeta_values_add(JINJA_CMETA_VALUE_STORE *store,
    size_t index, size_t count, size_t span_limit) {
  if (index > store->limit || count > store->limit - index ||
      span_limit < count || span_limit > store->limit - index ||
      store->limit > SIZE_MAX / sizeof(JINJA_CMETA_VALUE_CHUNK) ||
      store->limit > SIZE_MAX / sizeof(JINJA_CMETA_VALUE))
    return JINJA_CMETA_ERR_CAPACITY;
  if (store->memory == NULL) return JINJA_CMETA_ERR_METADATA;
  const stl_allocator allocator = jinja_cmeta_memory_allocator(store->memory);
  vec_alloc_t *directory = store->chunks;
  const size_t length = vec_size(vec_alloc_view(directory));
  JINJA_CMETA_VALUE_CHUNK *previous = length != 0u
      ? (JINJA_CMETA_VALUE_CHUNK *)vec_alloc_at(directory, length - 1u) : NULL;
  if (previous != NULL && index <= previous->first)
    return JINJA_CMETA_ERR_METADATA;
  stl_status status;
  if (directory == NULL) {
    status = vec_alloc_new_bytes(sizeof(JINJA_CMETA_VALUE_CHUNK),
        _Alignof(JINJA_CMETA_VALUE_CHUNK), store->limit, &allocator, &directory);
    if (status != STL_OK) return jinja_cmeta_values_status(status);
  }
  JINJA_CMETA_VALUE_CHUNK chunk = {.first = index, .end = index + count};
  status = vec_alloc_new_bytes(sizeof(JINJA_CMETA_VALUE), _Alignof(JINJA_CMETA_VALUE),
      span_limit, &allocator, &chunk.values);
  if (status == STL_OK) status = vec_alloc_resize(chunk.values, count);
  if (status == STL_OK) status = vec_alloc_push(directory, &chunk);
  if (status != STL_OK) {
    vec_alloc_destroy(chunk.values);
    if (store->chunks == NULL) vec_alloc_destroy(directory);
    return jinja_cmeta_values_status(status);
  }
  store->chunks = directory;
  /* Reacquire descriptors after directory growth; only unused tails can
   * be superseded, never committed slots or published payload addresses. */
  if (length != 0u) {
    previous = (JINJA_CMETA_VALUE_CHUNK *)vec_alloc_at(directory, length - 1u);
    if (previous->end > index) previous->end = index;
  }
  return JINJA_CMETA_OK;
}

/* Prepare without committing slots. Callers commit before recursive evaluation.
 * A zero-length span needs no storage. Failure does not advance logical ranges;
 * any initialized empty directory remains owned and safe to destroy. */
static JINJA_CMETA_STATUS jinja_cmeta_values_prepare(JINJA_CMETA_VALUE_STORE *store,
    size_t index, size_t count) {
  if (index > store->limit || count > store->limit - index)
    return JINJA_CMETA_ERR_CAPACITY;
  if (count == 0u) return JINJA_CMETA_OK;
  const size_t length = vec_size(vec_alloc_view(store->chunks));
  if (length != 0u) {
    const JINJA_CMETA_VALUE_CHUNK *last =
        (const JINJA_CMETA_VALUE_CHUNK *)vec_at_const(vec_alloc_view(store->chunks), length - 1u);
    if (index < last->first) return JINJA_CMETA_ERR_METADATA;
    if (index <= last->end && count <= last->end - index)
      return JINJA_CMETA_OK;
  }
  return jinja_cmeta_values_add(store, index, count, count);
}

/* Append one key/value pair to the newest, unpublished context. retained is
 * the number of slots already committed to that context, not its entry count.
 * Ordinary snapshots never call this path and therefore never relocate. */
static JINJA_CMETA_STATUS jinja_cmeta_values_prepare_context(
    JINJA_CMETA_VALUE_STORE *store, size_t index, size_t retained) {
  if (index > store->limit || JINJA_CMETA_VALUE_PAIR_WIDTH > store->limit - index)
    return JINJA_CMETA_ERR_CAPACITY;
  if (retained == 0u)
    return jinja_cmeta_values_add(store, index, JINJA_CMETA_VALUE_PAIR_WIDTH,
        store->limit - index);
  const size_t length = vec_size(vec_alloc_view(store->chunks));
  if (length == 0u || retained > index) return JINJA_CMETA_ERR_METADATA;
  JINJA_CMETA_VALUE_CHUNK *last =
      (JINJA_CMETA_VALUE_CHUNK *)vec_alloc_at(store->chunks, length - 1u);
  if (last->first != index - retained || last->end != index)
    return JINJA_CMETA_ERR_METADATA;
  stl_status status = vec_alloc_resize(last->values, retained + JINJA_CMETA_VALUE_PAIR_WIDTH);
  if (status != STL_OK) return jinja_cmeta_values_status(status);
  last->end = index + JINJA_CMETA_VALUE_PAIR_WIDTH;
  return JINJA_CMETA_OK;
}

/* O(log chunks), O(1) for the current span. Valid indices were prepared by the
 * caller. NULL denotes an internal range invariant violation, never a fallback. */
static JINJA_CMETA_VALUE *jinja_cmeta_values_at(JINJA_CMETA_VALUE_STORE *store,
    size_t index) {
  size_t low = 0u, high = vec_size(vec_alloc_view(store->chunks));
  if (high == 0u) return NULL;
  JINJA_CMETA_VALUE_CHUNK *chunk =
      (JINJA_CMETA_VALUE_CHUNK *)vec_alloc_at(store->chunks, high - 1u);
  if (index >= chunk->first && index < chunk->end)
    return (JINJA_CMETA_VALUE *)vec_alloc_at(chunk->values, index - chunk->first);
  while (low < high) {
    const size_t middle = low + (high - low) / 2u;
    chunk = (JINJA_CMETA_VALUE_CHUNK *)vec_alloc_at(store->chunks, middle);
    if (index < chunk->first) high = middle;
    else if (index >= chunk->end) low = middle + 1u;
    else return (JINJA_CMETA_VALUE *)vec_alloc_at(chunk->values, index - chunk->first);
  }
  return NULL;
}

static void jinja_cmeta_values_destroy(JINJA_CMETA_VALUE_STORE *store) {
  const size_t length = vec_size(vec_alloc_view(store->chunks));
  for (size_t i = 0u; i < length; ++i) {
    JINJA_CMETA_VALUE_CHUNK *chunk = (JINJA_CMETA_VALUE_CHUNK *)vec_alloc_at(store->chunks, i);
    vec_alloc_destroy(chunk->values);
  }
  vec_alloc_destroy(store->chunks);
  *store = (JINJA_CMETA_VALUE_STORE){0};
}

#endif
