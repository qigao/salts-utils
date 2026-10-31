#include "search_context.h"
#include <tinytest.h>
#include <stdint.h>
#include <string.h>

enum { MAX_EVENTS = 64, MAX_LINE_BYTES = 32 };
typedef struct event_copy {
  salts_search_context_kind kind;
  bool group_break;
  unsigned char bytes[MAX_LINE_BYTES];
  size_t length;
  uint64_t line, start, end;
  bool has_match_span;
  uint64_t match_start, match_end;
} event_copy;
typedef struct context_probe {
  event_copy events[MAX_EVENTS];
  size_t count, stop_at;
  bool overflow;
} context_probe;

static bool context_collect(void *user, const salts_search_context_event *ev) {
  context_probe *p = (context_probe *)user;
  event_copy *dst;
  if (p->stop_at && p->count == p->stop_at)
    return false;
  if (p->count == MAX_EVENTS || ev->line.length > MAX_LINE_BYTES) {
    p->overflow = true;
    return false;
  }
  dst = &p->events[p->count++];
  dst->length = ev->line.length;
  dst->kind = ev->kind;
  dst->group_break = ev->group_break;
  dst->line = ev->line.line_number;
  dst->start = ev->line.byte_start;
  dst->end = ev->line.byte_end;
  dst->has_match_span = ev->has_match_span;
  dst->match_start = ev->match_byte_start;
  dst->match_end = ev->match_byte_end;
  if (dst->length)
    memcpy(dst->bytes, ev->line.bytes, dst->length);
  return true;
}

static bool accept_line(salts_search_context *ctx, const void *text,
                        size_t size, uint64_t line_number, uint64_t start) {
  salts_search_line line = {(const unsigned char *)text, size,
                            start, start + size, line_number, 1};
  return salts_search_context_accept_line(ctx, &line);
}

spec("search A2 bounded literal match context") {
  it("emits before/matched/after lines in order and never duplicates overlap") {
    salts_search_context ctx = {0};
    salts_search_context_slot slots[2] = {0};
    unsigned char arena[64];
    context_probe p = {0};
    unsigned char needle[] = "hit";
    salts_search_literal_pattern pattern = {0};
    const char *source[] = {"one", "two", "hit", "four", "hit", "six", "seven", "eight"};
    size_t i;
    check_equal(salts_search_literal_compile(&pattern, needle, 3u, 8u), SALTS_SEARCH_LITERAL_OK);
    check_equal(salts_search_context_init(&ctx, &pattern, slots, 2u,
       arena, sizeof(arena), 32u, 2u, context_collect, &p), SALTS_SEARCH_CONTEXT_OK);
    needle[0] = 'x'; /* compiled literal remains independent of source bytes */
    for (i = 0; i < sizeof(source)/sizeof(source[0]); ++i)
      check_true(accept_line(&ctx, source[i], strlen(source[i]), i+1u, 500u+i*16u));
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_OK);
    check_equal(p.count, 7u);
    check_equal(ctx.match_lines, 2u);
    for (i = 0; i < p.count; ++i) {
      check_equal(p.events[i].line, i+1u);
      check_true(!p.events[i].group_break);
      check_true(p.events[i].end - p.events[i].start == p.events[i].length);
    }
    check_equal(p.events[0].kind, SALTS_SEARCH_CONTEXT_BEFORE);
    check_equal(p.events[1].kind, SALTS_SEARCH_CONTEXT_BEFORE);
    check_equal(p.events[2].kind, SALTS_SEARCH_CONTEXT_MATCH);
    check_equal(p.events[3].kind, SALTS_SEARCH_CONTEXT_AFTER);
    check_equal(p.events[4].kind, SALTS_SEARCH_CONTEXT_MATCH);
    check_equal(p.events[5].kind, SALTS_SEARCH_CONTEXT_AFTER);
    check_equal(p.events[6].kind, SALTS_SEARCH_CONTEXT_AFTER);
    check_true(!p.events[0].has_match_span);
    check_true(p.events[2].has_match_span);
    check_equal(p.events[2].match_start, UINT64_C(532));
    check_equal(p.events[2].match_end, UINT64_C(535));
    check_true(!p.events[3].has_match_span);
    check_true(p.events[4].has_match_span);
    check_equal(p.events[4].match_start, UINT64_C(564));
    check_equal(p.events[4].match_end, UINT64_C(567));
    check_true(!p.events[5].has_match_span);
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_CLOSED);
    salts_search_literal_destroy(&pattern);
  }

  it("emits a group break after noncontiguous matches") {
    salts_search_context ctx = {0};
    salts_search_context_slot slots[2] = {0};
    unsigned char arena[2*32];
    context_probe p = {0};
    salts_search_literal_pattern pattern = {0};
    size_t i;
    check_equal(salts_search_literal_compile(&pattern, "hit", 3u, 8u), SALTS_SEARCH_LITERAL_OK);
    check_equal(salts_search_context_init(&ctx, &pattern, slots, 2u,
        arena, sizeof(arena), 32u, 1u, context_collect, &p), SALTS_SEARCH_CONTEXT_OK);
    for (i=1; i<=12u; ++i) {
      const char *line = (i==3u || i==11u) ? "hit" : "x";
      check_true(accept_line(&ctx, line, strlen(line), i, UINT64_C(0x100000000)+i*10u));
    }
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_OK);
    check_equal(p.count, 8u);
    check_equal(p.events[0].line, 1u);
    check_equal(p.events[3].line, 4u);
    check_equal(p.events[4].line, 9u);
    check_true(p.events[4].group_break);
    check_equal(p.events[6].line, 11u);
    check_equal(p.events[6].kind, SALTS_SEARCH_CONTEXT_MATCH);
    check_true(!p.events[7].group_break);
    salts_search_literal_destroy(&pattern);
  }

  it("accepts zero before context, literal NUL and last line without terminator") {
    salts_search_context ctx = {0};
    context_probe p = {0};
    salts_search_literal_pattern pattern = {0};
    const unsigned char raw[] = {0,'h','i','t',0};
    salts_search_line last = {raw,sizeof(raw),10u,15u,1u,0u};
    check_equal(salts_search_literal_compile(&pattern, raw+1, 3u, 8u), SALTS_SEARCH_LITERAL_OK);
    check_equal(salts_search_context_init(&ctx, &pattern, NULL, 0u,
        NULL, 0u, 16u, 1u, context_collect, &p), SALTS_SEARCH_CONTEXT_OK);
    check_true(salts_search_context_accept_line(&ctx,&last));
    check_equal(p.count, 1u);
    check_equal(p.events[0].length, sizeof(raw));
    check_equal(p.events[0].kind, SALTS_SEARCH_CONTEXT_MATCH);
    check_equal(p.events[0].bytes[0], 0);
    check_equal(p.events[0].bytes[4], 0);
    check_true(p.events[0].has_match_span);
    check_equal(p.events[0].match_start, UINT64_C(11));
    check_equal(p.events[0].match_end, UINT64_C(14));
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_OK);
    salts_search_literal_destroy(&pattern);
  }

  it("signals exact context capacity exhaustion without emitting partial bytes") {
    salts_search_context ctx = {0};
    salts_search_context_slot slot = {0};
    unsigned char store[3] = {99, 88, 77};
    context_probe p = {0};
    salts_search_literal_pattern pattern = {0};
    check_equal(salts_search_literal_compile(&pattern, "hit", 3u, 8u), SALTS_SEARCH_LITERAL_OK);
    check_equal(salts_search_context_init(&ctx,&pattern,&slot,1u,
         store,2u,2u,1u,context_collect,&p), SALTS_SEARCH_CONTEXT_OK);
    check_true(!accept_line(&ctx,"abc",3u,1u,0u));
    check_equal(ctx.status,SALTS_SEARCH_CONTEXT_LIMIT);
    check_equal(salts_search_context_finish(&ctx),SALTS_SEARCH_CONTEXT_LIMIT);
    check_equal(p.count,0u);
    check_equal(store[2],77u);
    salts_search_literal_destroy(&pattern);
  }

  it("propagates callback stop and rejects invalid sequence and source ranges") {
    salts_search_context ctx = {0};
    salts_search_context_slot slot = {0};
    unsigned char store[16];
    context_probe p = {0};
    salts_search_literal_pattern pattern = {0};
    check_equal(salts_search_literal_compile(&pattern, "hit", 3u, 8u), SALTS_SEARCH_LITERAL_OK);
    p.stop_at = 1;
    check_equal(salts_search_context_init(&ctx,&pattern,&slot,1u,
        store,sizeof(store),16u,0u,context_collect,&p), SALTS_SEARCH_CONTEXT_OK);
    check_true(accept_line(&ctx,"prev",4u,20u,500u));
    check_true(!accept_line(&ctx,"hit",3u,21u,510u));
    check_equal(ctx.status,SALTS_SEARCH_CONTEXT_STOPPED);
    check_equal(p.count,1u); /* previous context was displayed */
    check_true(!accept_line(&ctx,"hit",3u,22u,530u));
    salts_search_literal_destroy(&pattern);

    memset(&ctx,0,sizeof(ctx));
    memset(&p,0,sizeof(p));
    check_equal(salts_search_literal_compile(&pattern, "hit", 3u, 8u), SALTS_SEARCH_LITERAL_OK);
    check_equal(salts_search_context_init(&ctx,&pattern,NULL,0u,
        NULL,0u,16u,0u,context_collect,&p), SALTS_SEARCH_CONTEXT_OK);
    check_true(accept_line(&ctx,"x",1u,200u,1000u));
    check_true(!accept_line(&ctx,"x",1u,202u,1005u));
    check_equal(ctx.status,SALTS_SEARCH_CONTEXT_INVALID);
    salts_search_literal_destroy(&pattern);
  }
}


/* A4 query-owned all-hit stream, distinct from A2's one context event/line.
 * Test captures are fixed-size and copy input bytes before the callback ends. */
enum { MAX_HIT_EVENTS = 32 };
typedef struct hit_event_copy {
  uint64_t line_number, line_start, line_end, match_start, match_end;
  unsigned terminator;
  unsigned char bytes[MAX_LINE_BYTES];
  size_t length;
} hit_event_copy;

typedef struct hit_stream_probe {
  hit_event_copy hits[MAX_HIT_EVENTS];
  size_t count, stop_after;
  bool overflow, reenter_finish;
  salts_search_hit_stream *stream;
  salts_search_context_status reenter_result;
} hit_stream_probe;

static bool collect_hit(void *user, const salts_search_hit_event *event) {
  hit_stream_probe *probe = (hit_stream_probe *)user;
  hit_event_copy *dst;
  if (probe->count >= MAX_HIT_EVENTS ||
      event->line.length > MAX_LINE_BYTES) {
    probe->overflow = true;
    return false;
  }
  dst = &probe->hits[probe->count++];
  dst->line_number = event->line.line_number;
  dst->line_start = event->line.byte_start;
  dst->line_end = event->line.byte_end;
  dst->terminator = event->line.terminator_size;
  dst->match_start = event->match_byte_start;
  dst->match_end = event->match_byte_end;
  dst->length = event->line.length;
  if (dst->length)
    memcpy(dst->bytes, event->line.bytes, dst->length);
  if (probe->reenter_finish)
    probe->reenter_result = salts_search_hit_stream_finish(probe->stream);
  return probe->stop_after == 0u || probe->count < probe->stop_after;
}

spec("search A4 query-budgeted original-line multi-hit stream") {
  it("streams overlapping TRE hits through split CRLF, LF and EOF") {
    salts_search_matcher matcher = {0};
    salts_search_hit_stream stream = {0};
    salts_search_line_scanner scanner = {0};
    hit_stream_probe probe = {0};
    unsigned char buffer[16];
    const char raw[] = "ababa\r\nxaba\naba";
    size_t pos = 0u, consumed = 0u;
    check_equal(salts_search_matcher_compile(&matcher, SALTS_SEARCH_MATCHER_REGEX,
                "aba", 3u, 16u, sizeof(buffer)), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_hit_stream_init(
                &stream, &matcher, 12u, 10u, true, collect_hit, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_lines_init(
                &scanner, buffer, sizeof(buffer), SALTS_SEARCH_LINES_LF,
                SALTS_SEARCH_LINES_BINARY_ALLOW,
                salts_search_hit_stream_accept_line, &stream),
                SALTS_SEARCH_LINES_OK);
    while (pos < sizeof(raw) - 1u) {
      const size_t remaining = sizeof(raw) - 1u - pos;
      const size_t count = remaining < 2u ? remaining : 2u;
      check_equal(salts_search_lines_feed(&scanner, raw + pos, count,
                  &consumed), SALTS_SEARCH_LINES_OK);
      check_equal(consumed, count);
      pos += count;
    }
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_CLOSED);
    check_true(!probe.overflow);
    check_equal(probe.count, 4u);
    check_equal(stream.matched_lines, 3u);
    check_equal(stream.budget.total_emitted, 4u);
    check_equal(stream.budget.total_scans, 7u);
    check_equal(probe.hits[0].line_number, UINT64_C(1));
    check_equal(probe.hits[0].terminator, 2u);
    check_equal(probe.hits[0].match_start, UINT64_C(0));
    check_equal(probe.hits[0].match_end, UINT64_C(3));
    check_equal(probe.hits[1].match_start, UINT64_C(2));
    check_equal(probe.hits[1].match_end, UINT64_C(5));
    check_equal(probe.hits[2].line_number, UINT64_C(2));
    check_equal(probe.hits[2].terminator, 1u);
    check_equal(probe.hits[2].match_start, UINT64_C(8));
    check_equal(probe.hits[2].match_end, UINT64_C(11));
    check_equal(probe.hits[3].line_number, UINT64_C(3));
    check_equal(probe.hits[3].terminator, 0u);
    check_equal(probe.hits[3].match_start, UINT64_C(12));
    check_equal(probe.hits[3].match_end, UINT64_C(15));
    check_equal(probe.hits[0].length, 5u);
    check_equal(probe.hits[1].bytes[4], (unsigned char)'a');
    salts_search_matcher_destroy(&matcher);
  }

  it("fails closed on a global hit limit after earlier line delivery") {
    salts_search_matcher matcher = {0};
    salts_search_hit_stream stream = {0};
    salts_search_line_scanner scanner = {0};
    hit_stream_probe probe = {0};
    unsigned char buffer[8];
    size_t consumed = 0u;
    check_equal(salts_search_matcher_compile(&matcher, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, sizeof(buffer)), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_hit_stream_init(
                &stream, &matcher, 1u, 5u, false, collect_hit, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_lines_init(
                &scanner, buffer, sizeof(buffer), SALTS_SEARCH_LINES_LF,
                SALTS_SEARCH_LINES_BINARY_ALLOW,
                salts_search_hit_stream_accept_line, &stream),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "a\na\n", 4u, &consumed),
                SALTS_SEARCH_LINES_STOPPED);
    check_equal(consumed, 4u);
    check_equal(probe.count, 1u);
    check_equal(probe.hits[0].match_start, UINT64_C(0));
    check_equal(stream.matched_lines, 1u);
    check_equal(stream.budget.total_emitted, 1u);
    check_equal(stream.budget.total_scans, 3u);
    check_equal(stream.status, SALTS_SEARCH_CONTEXT_LIMIT);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_LIMIT);
    check_equal(salts_search_lines_feed(&scanner, "a", 1u, &consumed),
                SALTS_SEARCH_LINES_CLOSED);
    check_equal(consumed, 0u);
    salts_search_matcher_destroy(&matcher);
  }

  it("debits final negative TRE lookups across separate source lines") {
    salts_search_matcher matcher = {0};
    salts_search_hit_stream stream = {0};
    salts_search_line_scanner scanner = {0};
    hit_stream_probe probe = {0};
    unsigned char buffer[8];
    size_t consumed = 0u;
    check_equal(salts_search_matcher_compile(&matcher, SALTS_SEARCH_MATCHER_REGEX,
                "aba", 3u, 8u, sizeof(buffer)), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_hit_stream_init(
                &stream, &matcher, 8u, 2u, false, collect_hit, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_lines_init(
                &scanner, buffer, sizeof(buffer), SALTS_SEARCH_LINES_LF,
                SALTS_SEARCH_LINES_BINARY_ALLOW,
                salts_search_hit_stream_accept_line, &stream),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "aba\nx\n", 6u, &consumed),
                SALTS_SEARCH_LINES_STOPPED);
    check_equal(consumed, 6u);
    check_equal(probe.count, 1u);
    check_equal(stream.matched_lines, 1u);
    check_equal(stream.budget.remaining_scans, 0u);
    check_equal(stream.budget.total_scans, 2u);
    check_equal(stream.status, SALTS_SEARCH_CONTEXT_LIMIT);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_LIMIT);
    salts_search_matcher_destroy(&matcher);
  }

  it("propagates early hit-callback cancellation through scanner STOPPED") {
    salts_search_matcher matcher = {0};
    salts_search_hit_stream stream = {0};
    salts_search_line_scanner scanner = {0};
    hit_stream_probe probe = {0};
    unsigned char buffer[8];
    size_t consumed = 0u;
    probe.stop_after = 1u;
    check_equal(salts_search_matcher_compile(&matcher, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, sizeof(buffer)), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_hit_stream_init(
                &stream, &matcher, 8u, 5u, true, collect_hit, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_lines_init(
                &scanner, buffer, sizeof(buffer), SALTS_SEARCH_LINES_LF,
                SALTS_SEARCH_LINES_BINARY_ALLOW,
                salts_search_hit_stream_accept_line, &stream),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "aaa\n", 4u, &consumed),
                SALTS_SEARCH_LINES_STOPPED);
    check_equal(consumed, 4u);
    check_equal(probe.count, 1u);
    check_equal(stream.status, SALTS_SEARCH_CONTEXT_STOPPED);
    check_true(stream.budget.terminal);
    check_equal(stream.budget.total_scans, 1u);
    check_equal(stream.budget.total_emitted, 1u);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_STOPPED);
    salts_search_matcher_destroy(&matcher);
  }

  it("preserves absolute offsets near UINT64_MAX and rejects discontinuity") {
    salts_search_matcher matcher = {0};
    salts_search_hit_stream stream = {0};
    hit_stream_probe probe = {0};
    const unsigned char raw[] = {'x',0,'a'};
    const salts_search_line near = {raw, sizeof(raw),
        UINT64_MAX - UINT64_C(4), UINT64_MAX - UINT64_C(1), 42u, 1u};
    const salts_search_line empty = {NULL, 0u,
        UINT64_MAX, UINT64_MAX, 43u, 0u};
    const salts_search_line broken = {raw, sizeof(raw),
        UINT64_MAX - UINT64_C(3), UINT64_MAX, 43u, 0u};
    check_equal(salts_search_matcher_compile(&matcher, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, 8u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_hit_stream_init(
                &stream, &matcher, 8u, 8u, false, collect_hit, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_true(salts_search_hit_stream_accept_line(&stream, &near));
    check_true(salts_search_hit_stream_accept_line(&stream, &empty));
    check_equal(probe.count, 1u);
    check_equal(probe.hits[0].match_start, UINT64_MAX - UINT64_C(2));
    check_equal(probe.hits[0].match_end, UINT64_MAX - UINT64_C(1));
    check_equal(stream.budget.total_scans, 3u);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_OK);
    salts_search_matcher_destroy(&matcher);

    memset(&stream, 0, sizeof(stream));
    memset(&probe, 0, sizeof(probe));
    check_equal(salts_search_matcher_compile(&matcher, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, 8u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_hit_stream_init(
                &stream, &matcher, 8u, 8u, false, collect_hit, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_true(salts_search_hit_stream_accept_line(&stream, &near));
    check_true(!salts_search_hit_stream_accept_line(&stream, &broken));
    check_equal(stream.status, SALTS_SEARCH_CONTEXT_INVALID);
    check_equal(probe.count, 1u);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_INVALID);
    salts_search_matcher_destroy(&matcher);
  }

  it("streams NUL-containing Literal hits with zero TRE scan consumption") {
    salts_search_matcher matcher = {0};
    salts_search_hit_stream stream = {0};
    salts_search_line_scanner scanner = {0};
    hit_stream_probe probe = {0};
    unsigned char buffer[8];
    const unsigned char raw[] = {'a',0,'a',0,'\n','z'};
    const unsigned char pattern[] = {'a',0};
    size_t consumed = 0u;
    check_equal(salts_search_matcher_compile(&matcher, SALTS_SEARCH_MATCHER_LITERAL,
                pattern, sizeof(pattern), 8u, sizeof(buffer)),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_hit_stream_init(
                &stream, &matcher, 2u, 0u, false, collect_hit, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_lines_init(
                &scanner, buffer, sizeof(buffer), SALTS_SEARCH_LINES_LF,
                SALTS_SEARCH_LINES_BINARY_ALLOW,
                salts_search_hit_stream_accept_line, &stream),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, raw, sizeof(raw), &consumed),
                SALTS_SEARCH_LINES_OK);
    check_equal(consumed, sizeof(raw));
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_OK);
    check_equal(probe.count, 2u);
    check_equal(probe.hits[0].match_start, UINT64_C(0));
    check_equal(probe.hits[0].match_end, UINT64_C(2));
    check_equal(probe.hits[1].match_start, UINT64_C(2));
    check_equal(probe.hits[1].match_end, UINT64_C(4));
    check_equal(probe.hits[0].bytes[1], 0u);
    check_equal(stream.matched_lines, 1u);
    check_equal(stream.budget.total_scans, 0u);
    check_equal(stream.budget.total_emitted, 2u);
    salts_search_matcher_destroy(&matcher);
  }

  it("rejects callback reentrancy even when client ignores its error") {
    salts_search_matcher matcher = {0};
    salts_search_hit_stream stream = {0};
    salts_search_line_scanner scanner = {0};
    hit_stream_probe probe = {0};
    unsigned char buffer[8];
    size_t consumed = 0u;
    check_equal(salts_search_matcher_compile(&matcher, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, sizeof(buffer)), SALTS_SEARCH_MATCHER_OK);
    probe.reenter_finish = true;
    probe.stream = &stream;
    check_equal(salts_search_hit_stream_init(
                &stream, &matcher, 8u, 5u, false, collect_hit, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_lines_init(
                &scanner, buffer, sizeof(buffer), SALTS_SEARCH_LINES_LF,
                SALTS_SEARCH_LINES_BINARY_ALLOW,
                salts_search_hit_stream_accept_line, &stream),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "a\n", 2u, &consumed),
                SALTS_SEARCH_LINES_STOPPED);
    check_equal(probe.reenter_result, SALTS_SEARCH_CONTEXT_INVALID);
    check_equal(probe.count, 1u);
    check_equal(stream.status, SALTS_SEARCH_CONTEXT_INVALID);
    check_equal(salts_search_hit_stream_finish(&stream), SALTS_SEARCH_CONTEXT_INVALID);
    salts_search_matcher_destroy(&matcher);
  }
}
