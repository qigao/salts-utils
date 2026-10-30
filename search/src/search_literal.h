/* Internal C11 literal matcher contract; intentionally not installed until A4.
 * Ownership: initialized pattern owns its immutable copy of the needle.
 * Threading: simultaneous search is safe if destroy/compile do not overlap.
 * Caller owns source bytes for the duration of each search call.
 */
#ifndef SALTS_SEARCH_LITERAL_INTERNAL_H
#define SALTS_SEARCH_LITERAL_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum salts_search_literal_status {
  SALTS_SEARCH_LITERAL_OK = 0,
  SALTS_SEARCH_LITERAL_NOT_FOUND = 1,
  SALTS_SEARCH_LITERAL_INVALID = 2,
  SALTS_SEARCH_LITERAL_LIMIT = 3,
  SALTS_SEARCH_LITERAL_NO_MEMORY = 4
} salts_search_literal_status;

typedef struct salts_search_literal_pattern {
  unsigned char *bytes;
  size_t length;
  size_t critical_position;
  size_t period;
  bool periodic;
  bool compiled;
} salts_search_literal_pattern;

typedef struct salts_search_literal_span {
  size_t start;
  size_t end; /* exclusive */
} salts_search_literal_span;

/* 'out' must be zero-initialized; caller must call destroy when done.
 * max_pattern_bytes must be nonzero, and is a strict admission bound.
 * An empty pattern is allowed and matches [start_at,start_at).
 */
salts_search_literal_status salts_search_literal_compile(
    salts_search_literal_pattern *out, const void *needle,
    size_t needle_length, size_t max_pattern_bytes);

/* Returns first byte-exact match at or after start_at. No string termination
 * or locale conversion. Failed operations clear *out_span when non-NULL.
 */
salts_search_literal_status salts_search_literal_find(
    const salts_search_literal_pattern *pattern, const void *text,
    size_t text_length, size_t start_at, salts_search_literal_span *out_span);

void salts_search_literal_destroy(salts_search_literal_pattern *pattern);

#endif
