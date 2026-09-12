#ifndef JINJA_CMETA_MEMORY_H
#define JINJA_CMETA_MEMORY_H

#include "jinja_cmeta_internal.h"
#include <cstl/allocator.h>
#include <stddef.h>
#include <stdlib.h>

/* Private single-threaded requested-byte ledger. The render owns this object;
 * allocators and storage owners borrow it through their final deallocation.
 * used is the single source of live charges; peak includes overlapping growth
 * buffers and CSTL scratch. Zero prohibits allocation, SIZE_MAX removes only
 * this byte cap, not object quotas. No callbacks, reset, or concurrent mutation
 * may intervene between admission and publication. Allocation failure publishes
 * neither storage nor charges. Free uses the exact original requested size.
 *
 * This ledger meters storage and render objects, but not yet all string paths.
 * The public runtime configuration therefore has no total-byte option yet.
 */
typedef struct JINJA_CMETA_MEMORY {
  size_t limit;
  size_t used;
  size_t peak;
  size_t allocations;
} JINJA_CMETA_MEMORY;

static inline stl_status jinja_cmeta_memory_allocate(void *context, size_t bytes, void **out) {
  JINJA_CMETA_MEMORY *memory = (JINJA_CMETA_MEMORY *)context;
  if (memory == NULL || out == NULL || bytes == 0u) return STL_INVALID_ARGUMENT;
  *out = NULL;
  if (memory->used > memory->limit || bytes > memory->limit - memory->used ||
      memory->allocations == SIZE_MAX) return STL_CAPACITY_EXCEEDED;
  void *data = malloc(bytes);
  if (data == NULL) return STL_OUT_OF_MEMORY;
  memory->used += bytes;
  ++memory->allocations;
  if (memory->peak < memory->used) memory->peak = memory->used;
  *out = data;
  return STL_OK;
}

static inline void jinja_cmeta_memory_deallocate(void *context, void *data, size_t bytes) {
  JINJA_CMETA_MEMORY *memory = (JINJA_CMETA_MEMORY *)context;
  /* An invalid release is an ownership violation, not a recoverable quota error. */
  if (memory == NULL || data == NULL || bytes == 0u || bytes > memory->used ||
      memory->allocations == 0u) abort();
  free(data);
  memory->used -= bytes;
  --memory->allocations;
}

static inline stl_allocator jinja_cmeta_memory_allocator(JINJA_CMETA_MEMORY *memory) {
  return (stl_allocator){memory, jinja_cmeta_memory_allocate, jinja_cmeta_memory_deallocate};
}

/* Transfer a raw memory_allocate allocation to the caller without freeing it.
 * Only the successful public result boundary may consume this ownership;
 * receipts and allocator-bound container storage must use their destructors. */
static inline void jinja_cmeta_memory_disown(JINJA_CMETA_MEMORY *memory, size_t bytes) {
  if (memory == NULL || bytes == 0u || bytes > memory->used || memory->allocations == 0u)
    abort();
  memory->used -= bytes;
  --memory->allocations;
}

static inline JINJA_CMETA_STATUS jinja_cmeta_memory_status(stl_status status) {
  switch (status) {
    case STL_OK: return JINJA_CMETA_OK;
    case STL_OUT_OF_MEMORY: return JINJA_CMETA_ERR_OUT_OF_MEMORY;
    case STL_CAPACITY_EXCEEDED: return JINJA_CMETA_ERR_CAPACITY;
    default: return JINJA_CMETA_ERR_METADATA;
  }
}

/* A receipt for independently owned allocations, not a suballocator. Keeping
 * it next to the payload avoids a second registry and lets every cleanup path
 * return the original charge. Only ordinary C alignment is supported. */
typedef union JINJA_CMETA_MEMORY_BLOCK {
#if defined(_MSC_VER)
  /* MSVC's C headers omit max_align_t; its fundamental maximum is double. */
  double alignment;
#else
  max_align_t alignment;
#endif
  struct {
    JINJA_CMETA_MEMORY *memory;
    size_t bytes;
  } receipt;
} JINJA_CMETA_MEMORY_BLOCK;

/* Uninitialized array storage. Payload and receipt are admitted together.
 * Empty requests publish NULL without a charge; failure also leaves *out NULL.
 * The ledger must outlive drop, including on early render exits. */
static inline JINJA_CMETA_STATUS jinja_cmeta_memory_new(JINJA_CMETA_MEMORY *memory,
    size_t count, size_t width, void **out) {
  if (memory == NULL || out == NULL || width == 0u) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  *out = NULL;
  if (count == 0u) return JINJA_CMETA_OK;
  if (count > (SIZE_MAX - sizeof(JINJA_CMETA_MEMORY_BLOCK)) / width)
    return JINJA_CMETA_ERR_CAPACITY;
  const size_t bytes = sizeof(JINJA_CMETA_MEMORY_BLOCK) + count * width;
  void *storage = NULL;
  const stl_status status = jinja_cmeta_memory_allocate(memory, bytes, &storage);
  if (status != STL_OK) return jinja_cmeta_memory_status(status);
  JINJA_CMETA_MEMORY_BLOCK *block = (JINJA_CMETA_MEMORY_BLOCK *)storage;
  block->receipt.memory = memory;
  block->receipt.bytes = bytes;
  *out = block + 1;
  return JINJA_CMETA_OK;
}

/* NULL is accepted. Non-NULL must be a live payload returned by memory_new,
 * never CSTL storage, a cell activation, borrowed data, or a caller result. */
static inline void jinja_cmeta_memory_drop(void *data) {
  if (data != NULL) {
    JINJA_CMETA_MEMORY_BLOCK *block = (JINJA_CMETA_MEMORY_BLOCK *)data - 1;
    jinja_cmeta_memory_deallocate(block->receipt.memory, block, block->receipt.bytes);
  }
}

#endif
