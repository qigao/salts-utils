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
 * The Literal enumerator rejects REGEX kind; exact TRE enumeration below
 * preserves original-subject offsets without re-running on a sliced suffix.
 * A4 full CPU, concurrency and filesystem acceptance remain gated by #709.
 */
typedef bool (*salts_search_matcher_hit_callback)(
    void *user, const salts_search_matcher_span *hit);

salts_search_matcher_status salts_search_matcher_enumerate_literals(
    const salts_search_matcher *compiled, const void *subject,
    size_t subject_length, bool overlapping, size_t max_hits,
    salts_search_matcher_hit_callback callback, void *user,
    size_t *out_emitted);

/* Enumerate POSIX exact TRE matches on the UNCHANGED original subject.
 * The unique TRE 0.9.0#1 provider's start-position API prevents suffix-
 * induced changes to ^, $, word-boundaries and original match offsets.
 * The callback and result-budget semantics match Literal enumeration.
 * Overlap starts the next search at previous_start+1, otherwise previous_end.
 * A zero-width match always advances one byte; EOF yields at most one.
 *
 * This is a bounded correctness implementation, NOT a claim of one-pass
 * O(subject_length) all-hit performance: TRE may rescan the full prefix
 * for each hit, so the strict max_hits bound is mandatory.
 */
salts_search_matcher_status salts_search_matcher_enumerate_regex(
    const salts_search_matcher *compiled, const void *subject,
    size_t subject_length, bool overlapping, size_t max_hits,
    salts_search_matcher_hit_callback callback, void *user,
    size_t *out_emitted);

/* Explicit per-enumeration TRE invocation budget, additive private API.
 * max_scans bounds the number of FULL-original-subject Tagged-NFA searches,
 * including the final failed search needed to prove that all hits are found
 * and any lookahead needed to prove max_hits exhaustion.
 * Budget exhaustion returns LIMIT even if no more hit would exist: no
 * incomplete result may be mislabeled success. out_emitted counts callbacks
 * already invoked, including one requesting STOPPED. max_scans=0 returns
 * LIMIT for a valid search even on a no-match subject (conservative).
 * This does NOT bound work within a single TRE scan, compilation time,
 * aggregate owner concurrency, or wall-clock CPU. Old API remains unchanged.
 */
salts_search_matcher_status salts_search_matcher_enumerate_regex_bounded(
    const salts_search_matcher *compiled, const void *subject,
    size_t subject_length, bool overlapping, size_t max_hits,
    size_t max_scans,
    salts_search_matcher_hit_callback callback, void *user,
    size_t *out_emitted);

/* Query-owned cumulative resource ledger across complete source lines.
 * The same one compiled matcher is borrowed for all calls; per-line original
 * bytes remain borrowed only during enumerate(). Initialize a zeroed object
 * exactly once. Any LIMIT/STOPPED/NO_MEMORY/error makes owner terminal.
 * A subsequent call is INVALID, never a misleading OK or silent restart.
 * Callback must not reenter the query owner or destroy the compiled matcher.
 *
 * remaining_hits counts callbacks, regardless of Literal/Regex kind.
 * remaining_scans counts complete-subject TRE invocations for Regex only,
 * including terminal negative probes; Literal does not consume TRE scans.
 * total_emitted and total_scans are monotone, per-query counters.
 * This does not cap one TRE call's CPU, compile work, or other owners.
 */
typedef struct salts_search_matcher_query_budget {
  size_t remaining_hits, remaining_scans;
  size_t total_emitted, total_scans;
  bool initialized, terminal, in_call;
} salts_search_matcher_query_budget;

salts_search_matcher_status salts_search_matcher_query_budget_init(
    salts_search_matcher_query_budget *budget,
    size_t max_hits, size_t max_scans);

/* Enumerate one original, complete line under a shared query-wide ledger.
 * out_emitted is the number of callbacks on THIS line, even if STOPPED.
 * The callback spans remain relative to this original line, as in the
 * primitive enumerate_* APIs. The caller owns each line's global offset.
 * A zero remaining TRE scan budget produces LIMIT even for a no-match line:
 * no false "complete" result without the required final proof.
 */
salts_search_matcher_status salts_search_matcher_query_enumerate(
    salts_search_matcher_query_budget *budget,
    const salts_search_matcher *compiled,
    const void *subject, size_t subject_length,
    bool overlapping, salts_search_matcher_hit_callback callback,
    void *user, size_t *out_emitted);

void salts_search_matcher_destroy(salts_search_matcher *compiled);

#endif
