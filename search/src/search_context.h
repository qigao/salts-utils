/* Internal C11 line-oriented literal context collector. Not installed/exported.
 * The incoming line's data is borrowed only for the call; a before-context
 * history line owns a copy in caller-supplied fixed storage until overwritten.
 */
#ifndef SALTS_SEARCH_CONTEXT_INTERNAL_H
#define SALTS_SEARCH_CONTEXT_INTERNAL_H
#include "search_lines.h"
#include "search_matcher.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum salts_search_context_status {
  SALTS_SEARCH_CONTEXT_OK = 0,
  SALTS_SEARCH_CONTEXT_INVALID,
  SALTS_SEARCH_CONTEXT_LIMIT,
  SALTS_SEARCH_CONTEXT_STOPPED,
  SALTS_SEARCH_CONTEXT_CLOSED,
  SALTS_SEARCH_CONTEXT_NO_MEMORY,
  SALTS_SEARCH_CONTEXT_MATCH_ERROR
} salts_search_context_status;

typedef enum salts_search_context_kind {
  SALTS_SEARCH_CONTEXT_BEFORE = 1,
  SALTS_SEARCH_CONTEXT_MATCH = 2,
  SALTS_SEARCH_CONTEXT_AFTER = 3
} salts_search_context_kind;

typedef struct salts_search_context_event {
  salts_search_line line; /* borrowed until callback returns */
  salts_search_context_kind kind;
  bool group_break; /* first emitted line after a non-contiguous prior result */
  /* A MATCH event carries the FIRST exact byte span on the original line.
   * BEFORE/AFTER events have has_match_span=false and zeroed span fields.
   * Empty matches remain valid with has_match_span=true and equal offsets.
   * No multi-match/rg leftmost-first claim is made by this structure.
   */
  bool has_match_span;
  uint64_t match_byte_start;
  uint64_t match_byte_end; /* exclusive, absolute source byte offsets */
} salts_search_context_event;

typedef bool (*salts_search_context_callback)(void *user,
                                              const salts_search_context_event *event);

typedef struct salts_search_context_slot {
  unsigned char *bytes; /* points into caller-owned storage */
  size_t length;
  uint64_t start, end, number;
  unsigned terminator_size;
} salts_search_context_slot;

typedef struct salts_search_context {
  const salts_search_literal_pattern *literal; /* legacy A2, borrowed */
  const salts_search_matcher *matcher; /* A4 dispatch: literal OR unique TRE */
  salts_search_context_slot *slots; /* borrowed, length before_count */
  unsigned char *storage; /* borrowed, size before_count*max_line_bytes */
  size_t before_count, after_count, max_line_bytes;
  size_t stored, next_slot, after_remaining;
  uint64_t latest_input_line, latest_emitted_line;
  size_t match_lines, emitted_lines;
  salts_search_context_callback callback;
  void *user;
  salts_search_context_status status;
  bool initialized, finished;
} salts_search_context;

/* slots/storage may be NULL iff before_count == 0. All buffers are borrowed.
 * max_line_bytes is the maximum retained before-context line (not the total
 * input size). Byte contents and offsets remain exact, including embedded NUL.
 * The pattern must remain compiled and alive throughout the collector lifetime.
 */
salts_search_context_status salts_search_context_init(
    salts_search_context *ctx, const salts_search_literal_pattern *literal,
    salts_search_context_slot *slots, size_t before_count,
    unsigned char *storage, size_t storage_bytes, size_t max_line_bytes,
    size_t after_count, salts_search_context_callback callback, void *user);

/* Unified matcher entry point; only one of literal/matcher is set.
 * For a regex match, the entire original line is passed unchanged to TRE,
 * so anchors, alternation and byte offsets do not drift from suffix-slicing.
 */
salts_search_context_status salts_search_context_init_matcher(
    salts_search_context *ctx, const salts_search_matcher *matcher,
    salts_search_context_slot *slots, size_t before_count,
    unsigned char *storage, size_t storage_bytes, size_t max_line_bytes,
    size_t after_count, salts_search_context_callback callback, void *user);

/* Pass this as the salts_search_line_callback of salts_search_lines_init.
 * False = STOP or error: inspect ctx->status to disambiguate. Neither the
 * line scanner nor this collector silently drops or truncates a line.
 */
bool salts_search_context_accept_line(void *ctx, const salts_search_line *line);

salts_search_context_status salts_search_context_finish(salts_search_context *ctx);

/* A4 query-owned all-hit stream: a distinct private result protocol from
 * the existing one-MATCH-event-per-line context collector. A line and its
 * bytes are borrowed only during the callback; events are per exact match
 * and have absolute half-open 64-bit offsets. No copied per-hit payload.
 */
typedef struct salts_search_hit_event {
  salts_search_line line; /* borrowed until callback returns */
  uint64_t match_byte_start, match_byte_end;
} salts_search_hit_event;

typedef bool (*salts_search_hit_callback)(
    void *user, const salts_search_hit_event *event);

typedef struct salts_search_hit_stream {
  salts_search_matcher_query_budget budget;
  salts_search_hit_callback callback;
  void *user;
  uint64_t last_line_number, next_byte_start;
  size_t matched_lines; /* unique lines with delivered hits */
  salts_search_context_status status;
  bool overlapping, initialized, finished, in_line;
  bool have_last_line, last_terminated;
} salts_search_hit_stream;

/* One query chooses one compiled Literal or exact TRE matcher at init.
 * max_hits is the whole-query callback quota; max_scans is the quota of
 * full-original-line TRE calls including final no-match probes. Literal
 * consumes no TRE scans. A terminal LIMIT/STOPPED/error cannot be resumed.
 */
salts_search_context_status salts_search_hit_stream_init(
    salts_search_hit_stream *stream, const salts_search_matcher *matcher,
    size_t max_hits, size_t max_scans, bool overlapping,
    salts_search_hit_callback callback, void *user);

/* Direct salts_search_line_callback for the existing chunked line scanner.
 * Lines must be contiguous in line number and physical byte offsets,
 * including LF/CRLF widths. On failure returns false and scanner STOPPED,
 * while stream->status retains the actual LIMIT/STOPPED/NO_MEMORY/INVALID.
 * Already-emitted hits remain counted. No callback reentry is permitted.
 */
bool salts_search_hit_stream_accept_line(
    void *stream, const salts_search_line *line);

salts_search_context_status salts_search_hit_stream_finish(
    salts_search_hit_stream *stream);

#endif
