#ifndef JINJA_PARSER_MEMORY_H
#define JINJA_PARSER_MEMORY_H

#include "jinja_expression_parser.h"
#include <stdlib.h>
#include <string.h>

/* Shared parser boundary, independent of executor state. NULL explicitly
 * selects the CRT allocator; a supplied allocator never falls back on failure.
 * All storage is malloc-aligned and returned with its original requested size. */
JINJA_EXPRESSION_PARSE_STATUS jinja_parser_allocate(const stl_allocator *allocator,
    size_t bytes, void **out);
void jinja_parser_deallocate(const stl_allocator *allocator, void *data, size_t bytes);

static inline stl_status jinja_parser_default_allocate(void *context, size_t bytes, void **out) {
  (void)context;
  *out = malloc(bytes);
  return *out != NULL ? STL_OK : STL_OUT_OF_MEMORY;
}

static inline void jinja_parser_default_deallocate(void *context, void *data, size_t bytes) {
  (void)context;
  (void)bytes;
  free(data);
}

/* Copy only after allocation has validated a supplied callback pair. Context
 * remains borrowed until every owner using this copy has been destroyed. */
static inline stl_allocator jinja_parser_allocator_copy(const stl_allocator *allocator) {
  return allocator != NULL ? *allocator : (stl_allocator){NULL,
      jinja_parser_default_allocate, jinja_parser_default_deallocate};
}

static inline JINJA_EXPRESSION_PARSE_STATUS jinja_parser_zero(const stl_allocator *allocator,
    size_t bytes, void **out) {
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_parser_allocate(allocator, bytes, out);
  if (status == JINJA_EXPRESSION_PARSE_OK) memset(*out, 0, bytes);
  return status;
}

#endif
