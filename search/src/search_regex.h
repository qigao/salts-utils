/* Private C11 TRE exact-regex adapter, intentionally not installed as SDK.
 * Uses exactly the fixed laurikari/tre 0.9.0 C provider from vcpkg-cache.
 * A compiled object owns one TRE regex and may not be destroyed while in use.
 */
#ifndef SALTS_SEARCH_REGEX_INTERNAL_H
#define SALTS_SEARCH_REGEX_INTERNAL_H
/* TRE's installed 0.9.0 public header uses size_t and wchar_t without
 * unconditionally including their defining standard headers. The adapter
 * makes those declarations available before entering the single C provider. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
#include <tre/tre.h>

typedef enum salts_search_regex_status {
  SALTS_SEARCH_REGEX_OK = 0,
  SALTS_SEARCH_REGEX_NOT_FOUND,
  SALTS_SEARCH_REGEX_INVALID,
  SALTS_SEARCH_REGEX_BAD_PATTERN,
  SALTS_SEARCH_REGEX_UNSUPPORTED,
  SALTS_SEARCH_REGEX_LIMIT,
  SALTS_SEARCH_REGEX_NO_MEMORY,
  SALTS_SEARCH_REGEX_ENGINE_ERROR
} salts_search_regex_status;

typedef struct salts_search_regex_span {
  uint64_t byte_start, byte_end; /* half-open offsets in the supplied slice */
} salts_search_regex_span;

typedef struct salts_search_regex_pattern {
  regex_t tre; /* opaque compiled Tagged NFA; one unique engine */
  size_t max_subject_bytes;
  bool compiled;
} salts_search_regex_pattern;

/* Exact byte-mode POSIX ERE with leftmost-longest match priority.
 * Hard max pattern length 4096 bytes; caller may choose a stricter bound.
 * max_subject_bytes must be in [1, INT_MAX], and the caller must use
 * complete line slices (no advertised multiline regex support).
 * Backreferences and approximate/fuzzy constructs are rejected at compile
 * admission; the underlying TRE package is shared with TurboScript and still
 * contains those modules, but this API never executes them.
 */
salts_search_regex_status salts_search_regex_compile(
    salts_search_regex_pattern *out, const void *pattern_bytes,
    size_t pattern_length, size_t max_pattern_bytes,
    size_t max_subject_bytes);

/* Return first exact match only; no suffix-rescan semantics are exposed,
 * preventing incorrect anchoring/word-boundary behavior of sliced rescans.
 * Failure zeroes out_span. No fallback engine, hidden truncation or allocation
 * owned by this wrapper. First and repeated exact lookups share the same
 * 8 MiB per-execution TRE workspace budget (not compile/aggregate CPU).
 */
salts_search_regex_status salts_search_regex_find_first(
    const salts_search_regex_pattern *compiled, const void *subject,
    size_t subject_length, salts_search_regex_span *out_span);

/* Find the first exact match whose START is >= min_start in the ORIGINAL
 * unchanged subject, retaining original ^/$/word-boundary semantics.
 * Requires the additive TRE 0.9.0#1 tre_regnexecb_from() in the unique
 * pinned static provider. No C substring slicing or alternate engine.
 * A min_start past EOF yields NOT_FOUND; EOF itself can match zero width.
 */
salts_search_regex_status salts_search_regex_find_from(
    const salts_search_regex_pattern *compiled, const void *subject,
    size_t subject_length, size_t min_start, salts_search_regex_span *out_span);

void salts_search_regex_destroy(salts_search_regex_pattern *compiled);
#endif
