/* Internal C11 query-selected matcher. Not published before A4.
 * Literal uses one Two-Way algorithm; Regex uses exactly TRE 0.9.0.
 * A kind is chosen at compile time, never inferred after a failed match.
 */
#ifndef SALTS_SEARCH_MATCHER_INTERNAL_H
#define SALTS_SEARCH_MATCHER_INTERNAL_H

#include "search_literal.h"
#include "search_regex.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum salts_search_matcher_kind {
  SALTS_SEARCH_MATCHER_LITERAL = 1,
  SALTS_SEARCH_MATCHER_REGEX = 2
} salts_search_matcher_kind;

typedef enum salts_search_matcher_status {
  SALTS_SEARCH_MATCHER_OK = 0,
  SALTS_SEARCH_MATCHER_NOT_FOUND,
  SALTS_SEARCH_MATCHER_INVALID,
  SALTS_SEARCH_MATCHER_LIMIT,
  SALTS_SEARCH_MATCHER_NO_MEMORY,
  SALTS_SEARCH_MATCHER_BAD_PATTERN,
  SALTS_SEARCH_MATCHER_UNSUPPORTED,
  SALTS_SEARCH_MATCHER_ENGINE_ERROR,
  SALTS_SEARCH_MATCHER_STOPPED /* callback requested bounded early stop */
} salts_search_matcher_status;

typedef struct salts_search_matcher_span {
  uint64_t byte_start;
  uint64_t byte_end; /* exclusive, relative to one complete line */
} salts_search_matcher_span;

typedef struct salts_search_matcher {
  salts_search_matcher_kind kind;
  size_t max_subject_bytes;
  union {
    salts_search_literal_pattern literal;
    salts_search_regex_pattern regex;
  } compiled_pattern;
  bool compiled;
} salts_search_matcher;

/* Caller must zero-initialize the object. Pattern bytes are copied/compiled
 * and immutable; the borrowed input slice remains owned by the caller.
 * A single max_subject_bytes budget applies to both algorithms and must be
 * in [1, INT_MAX] to guarantee the common 64-bit-offset contract across TRE.
 */
salts_search_matcher_status salts_search_matcher_compile(
    salts_search_matcher *out, salts_search_matcher_kind kind,
    const void *pattern, size_t pattern_length, size_t max_pattern_bytes,
    size_t max_subject_bytes);

/* First match only. No suffix recopying or implicit regex fallback.
 * Error clears *out_span and never reports a truncated successful result.
 */
salts_search_matcher_status salts_search_matcher_find_first(
    const salts_search_matcher *compiled, const void *subject,
    size_t subject_length, salts_search_matcher_span *out_span);

/* Enumerate EVERY literal byte hit in ascending order; match offsets are
 * relative to the original unmodified subject. overlapping=true reports
 * intersecting occurrences; false advances after each nonempty match.
 * Empty needles match each byte boundary INCLUDING end-of-input, once each.
 * max_hits is a strict callback admission cap: if an extra hit is found,
 * return LIMIT rather than success with incomplete results.
 * The count includes a hit whose callback returns false (STOPPED).
 *
 * TRE Regex is deliberately UNSUPPORTED here. Re-running TRE on a sliced
 * suffix changes anchors/word boundaries and cannot claim rg parity.
 * A correct single-TRE multi-hit engine is separately gated by #709.
 */
typedef bool (*salts_search_matcher_hit_callback)(
    void *user, const salts_search_matcher_span *hit);

salts_search_matcher_status salts_search_matcher_enumerate_literals(
    const salts_search_matcher *compiled, const void *subject,
    size_t subject_length, bool overlapping, size_t max_hits,
    salts_search_matcher_hit_callback callback, void *user,
    size_t *out_emitted);

void salts_search_matcher_destroy(salts_search_matcher *compiled);

#endif
