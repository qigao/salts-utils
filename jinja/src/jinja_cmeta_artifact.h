#ifndef JINJA_CMETA_ARTIFACT_H
#define JINJA_CMETA_ARTIFACT_H

#include "jinja_cmeta_memory.h"
#include <stdlib.h>
#include <string.h>

static inline stl_status jinja_cmeta_artifact_default_allocate(void *context,
    size_t bytes, void **out) {
  (void)context;
  *out = malloc(bytes);
  return *out != NULL ? STL_OK : STL_OUT_OF_MEMORY;
}

static inline void jinja_cmeta_artifact_default_deallocate(void *context,
    void *data, size_t bytes) {
  (void)context;
  (void)bytes;
  free(data);
}

/* Callback configuration is copied; its context must outlive the artifact.
 * NULL explicitly selects the default allocator, never an allocation fallback.
 * The root object includes the fixed allocation registry in its charge. */
static inline JINJA_CMETA_TEMPLATE *jinja_cmeta_artifact_create(
    const stl_allocator *allocator, JINJA_CMETA_STATUS *status) {
  const stl_allocator selected = allocator != NULL ? *allocator : (stl_allocator){NULL,
      jinja_cmeta_artifact_default_allocate, jinja_cmeta_artifact_default_deallocate};
  if (selected.allocate == NULL || selected.deallocate == NULL) {
    *status = JINJA_CMETA_ERR_INVALID_ARGUMENT;
    return NULL;
  }
  void *storage = NULL;
  *status = jinja_cmeta_memory_status(selected.allocate(selected.context,
      sizeof(JINJA_CMETA_TEMPLATE), &storage));
  if (*status != JINJA_CMETA_OK) return NULL;
  JINJA_CMETA_TEMPLATE *templ = (JINJA_CMETA_TEMPLATE *)storage;
  memset(templ, 0, sizeof(*templ));
  templ->allocator = selected;
  templ->retained_bytes = sizeof(*templ);
  return templ;
}

/* Construction-only retained allocation boundary. The registry is the owner;
 * semantic fields borrow its storage. No field is resized or released early.
 * Allocation failure leaves the registry and retained-byte count unchanged. */
static inline void *jinja_cmeta_artifact_allocate(JINJA_CMETA_TEMPLATE *templ,
    size_t count, size_t width) {
  if (templ->allocation_status != JINJA_CMETA_OK) return NULL;
  if (templ->allocation_count == JINJA_CMETA_MAX_ARTIFACT_ALLOCATIONS) {
    templ->allocation_status = JINJA_CMETA_ERR_CAPACITY;
    return NULL;
  }
  if (width == 0u || count == 0u) {
    templ->allocation_status = JINJA_CMETA_ERR_INVALID_ARGUMENT;
    return NULL;
  }
  if (count > (SIZE_MAX - templ->retained_bytes) / width) {
    templ->allocation_status = JINJA_CMETA_ERR_CAPACITY;
    return NULL;
  }
  const size_t bytes = count * width;
  void *storage = NULL;
  templ->allocation_status = jinja_cmeta_memory_status(
      templ->allocator.allocate(templ->allocator.context, bytes, &storage));
  if (templ->allocation_status != JINJA_CMETA_OK) return NULL;
  templ->allocations[templ->allocation_count++] = (JINJA_CMETA_ARTIFACT_ALLOCATION){storage, bytes};
  templ->retained_bytes += bytes;
  return storage;
}

/* Destruction-only: semantic views become invalid. Each charge comes from the
 * original allocation, not a logical count potentially reduced by deduplication. */
static inline void jinja_cmeta_artifact_clear(JINJA_CMETA_TEMPLATE *templ) {
  while (templ->allocation_count != 0u) {
    const JINJA_CMETA_ARTIFACT_ALLOCATION owned = templ->allocations[--templ->allocation_count];
    if (owned.bytes > templ->retained_bytes) abort();
    templ->allocator.deallocate(templ->allocator.context, owned.data, owned.bytes);
    templ->retained_bytes -= owned.bytes;
  }
}

static inline void jinja_cmeta_artifact_destroy(JINJA_CMETA_TEMPLATE *templ) {
  if (templ == NULL) return;
  jinja_cmeta_artifact_clear(templ);
  if (templ->retained_bytes != sizeof(*templ)) abort();
  const stl_allocator allocator = templ->allocator;
  allocator.deallocate(allocator.context, templ, sizeof(*templ));
}

static inline void *jinja_cmeta_artifact_zero(JINJA_CMETA_TEMPLATE *templ,
    size_t count, size_t width) {
  void *storage = jinja_cmeta_artifact_allocate(templ, count, width);
  if (storage != NULL) memset(storage, 0, count * width);
  return storage;
}

#endif
