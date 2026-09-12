#ifndef JINJA_CMETA_TEXT_H
#define JINJA_CMETA_TEXT_H

#include "jinja_cmeta_memory.h"
#include <cstl/vec_alloc.h>
#include <string.h>

/* Single-owner output bytes, including the terminating NUL. The Vec owns
 * capacity and growth; its allocator charges owners, slack and growth overlap.
 * Views expire on append or destroy. Input to append must not borrow this Vec.
 * The render ledger must outlive the buffer, including failed render cleanup. */
typedef struct JINJA_CMETA_TEXT {
  vec_alloc_t *bytes;
} JINJA_CMETA_TEXT;

static inline size_t jinja_cmeta_text_length(const JINJA_CMETA_TEXT *text) {
  const size_t size = vec_size(vec_alloc_view(text->bytes));
  return size == 0u ? 0u : size - 1u;
}

static inline const char *jinja_cmeta_text_data(const JINJA_CMETA_TEXT *text) {
  return (const char *)vec_alloc_at(text->bytes, 0u);
}

/* Only an unpublished, uniquely owned buffer may be written through here. */
static inline char *jinja_cmeta_text_mutable(JINJA_CMETA_TEXT *text) {
  return (char *)vec_alloc_at(text->bytes, 0u);
}

static inline void jinja_cmeta_text_destroy(JINJA_CMETA_TEXT *text) {
  vec_alloc_destroy(text->bytes);
  text->bytes = NULL;
}

/* Called once on an empty owner; publish only after both allocations succeed. */
static inline JINJA_CMETA_STATUS jinja_cmeta_text_init(JINJA_CMETA_TEXT *text,
    JINJA_CMETA_MEMORY *memory) {
  const stl_allocator allocator = jinja_cmeta_memory_allocator(memory);
  vec_alloc_t *bytes = NULL;
  stl_status status = vec_alloc_new_bytes(sizeof(char), _Alignof(char),
      JINJA_CMETA_MAX_TSTR_BYTES + 1u, &allocator, &bytes);
  if (status == STL_OK) status = vec_alloc_resize(bytes, 1u);
  if (status != STL_OK) {
    vec_alloc_destroy(bytes);
    return jinja_cmeta_memory_status(status);
  }
  text->bytes = bytes;
  return JINJA_CMETA_OK;
}

/* Measured-format scratch: checked storage for size bytes plus its terminator.
 * The caller initializes first and publishes only after formatting succeeds. */
static inline JINJA_CMETA_STATUS jinja_cmeta_text_resize(JINJA_CMETA_TEXT *text,
    size_t size) {
  if (!jinja_cmeta_string_append_fits(0u, size, JINJA_CMETA_MAX_TSTR_BYTES))
    return JINJA_CMETA_ERR_CAPACITY;
  const stl_status status = vec_alloc_resize(text->bytes, size + 1u);
  if (status != STL_OK) return jinja_cmeta_memory_status(status);
  jinja_cmeta_text_mutable(text)[size] = '\0';
  return JINJA_CMETA_OK;
}

/* Admission failure preserves content and its terminating NUL. Copying is
 * O(size); capacity growth and its peak accounting are delegated to CSTL. */
static inline JINJA_CMETA_STATUS jinja_cmeta_text_append(JINJA_CMETA_TEXT *text,
    const char *data, size_t size) {
  const size_t used = jinja_cmeta_text_length(text);
  if (!jinja_cmeta_string_append_fits(used, size, JINJA_CMETA_MAX_TSTR_BYTES))
    return JINJA_CMETA_ERR_CAPACITY;
  if (size == 0u) return JINJA_CMETA_OK;
  if (data == NULL) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  const stl_status status = vec_alloc_resize(text->bytes, used + size + 1u);
  if (status != STL_OK) return jinja_cmeta_memory_status(status);
  char *bytes = (char *)vec_alloc_at(text->bytes, 0u);
  memcpy(bytes + used, data, size);
  bytes[used + size] = '\0';
  return JINJA_CMETA_OK;
}

#endif
