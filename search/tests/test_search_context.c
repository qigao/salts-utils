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
