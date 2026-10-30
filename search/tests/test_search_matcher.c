#include "search_matcher.h"
#include "search_context.h"
#include "search_lines.h"
#include <tinytest.h>

#include <stdint.h>
#include <string.h>

typedef struct stream_probe {
  uint64_t line[12];
  uint64_t byte_start[12];
  salts_search_context_kind kind[12];
  size_t count;
  bool group_break[12];
  bool has_match_span[12];
  uint64_t match_start[12], match_end[12];
} stream_probe;

static bool record_event(void *user, const salts_search_context_event *event) {
  stream_probe *p = (stream_probe *)user;
  if (p->count >= 12u)
    return false;
  p->line[p->count] = event->line.line_number;
  p->byte_start[p->count] = event->line.byte_start;
  p->kind[p->count] = event->kind;
  p->group_break[p->count] = event->group_break;
  p->has_match_span[p->count] = event->has_match_span;
  p->match_start[p->count] = event->match_byte_start;
  p->match_end[p->count] = event->match_byte_end;
  ++p->count;
  return true;
}

spec("search common matcher chooses one literal or TRE byte engine") {
  it("returns identical half-open byte offsets for simple literal and regex") {
    const unsigned char data[] = {'!', 0, 'a', 'b', 'x'};
    salts_search_matcher m = {0};
    salts_search_matcher_span span = {99, 99};
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
        "ab", 2u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_find_first(&m, data, sizeof(data), &span),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(span.byte_start, UINT64_C(2));
    check_equal(span.byte_end, UINT64_C(4));
    salts_search_matcher_destroy(&m);
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
        "ab", 2u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_find_first(&m, data, sizeof(data), &span),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(span.byte_start, UINT64_C(2));
    check_equal(span.byte_end, UINT64_C(4));
    salts_search_matcher_destroy(&m);
    salts_search_matcher_destroy(&m);
  }

  it("keeps POSIX leftmost-longest for regex, with no implicit literal fallback") {
    salts_search_matcher m = {0};
    salts_search_matcher_span span = {0};
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a|ab", 4u, 32u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_find_first(&m, "xxab", 4u, &span),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(span.byte_start, UINT64_C(2));
    check_equal(span.byte_end, UINT64_C(4));
    salts_search_matcher_destroy(&m);
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "(a)\\1", 5u, 32u, 32u), SALTS_SEARCH_MATCHER_UNSUPPORTED);
    check_true(!m.compiled);
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "abc{~1}", 7u, 32u, 32u), SALTS_SEARCH_MATCHER_UNSUPPORTED);
    check_true(!m.compiled);
  }

  it("requires an explicit kind and a bounded subject on both algorithms") {
    salts_search_matcher m = {0};
    salts_search_matcher_span span = {17,17};
    check_equal(salts_search_matcher_compile(&m, (salts_search_matcher_kind)99,
                "a", 1u, 8u, 8u), SALTS_SEARCH_MATCHER_INVALID);
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                "a", 1u, 8u, 2u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, 2u), SALTS_SEARCH_MATCHER_INVALID);
    check_equal(salts_search_matcher_find_first(&m, "abc", 3u, &span),
                SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(span.byte_start, UINT64_C(0));
    check_equal(span.byte_end, UINT64_C(0));
    check_equal(salts_search_matcher_find_first(&m, "bb", 2u, &span),
                SALTS_SEARCH_MATCHER_NOT_FOUND);
    salts_search_matcher_destroy(&m);
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "[", 1u, 8u, 2u), SALTS_SEARCH_MATCHER_BAD_PATTERN);
    check_true(!m.compiled);
    check_equal(salts_search_matcher_find_first(&m, NULL, 0u, &span),
                SALTS_SEARCH_MATCHER_INVALID);
  }

  it("feeds a compiled TRE matcher through the one bounded line/context path") {
    salts_search_matcher m = {0};
    salts_search_context ctx = {0};
    salts_search_context_slot slots[1] = {0};
    unsigned char history[32];
    unsigned char line_bytes[32];
    salts_search_line_scanner scanner = {0};
    stream_probe probe = {0};
    const char text[] = "zz\nab\nnone\nab\nend\n";
    size_t pos = 0;
    size_t consumed = 0;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a|ab", 4u, 24u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_context_init_matcher(&ctx, &m, slots, 1u,
                history, sizeof(history), 32u, 1u, record_event, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_lines_init(&scanner, line_bytes,
                sizeof(line_bytes), SALTS_SEARCH_LINES_LF,
                SALTS_SEARCH_LINES_BINARY_ALLOW,
                salts_search_context_accept_line, &ctx),
                SALTS_SEARCH_LINES_OK);
    while (pos < sizeof(text)-1u) {
      const size_t size = sizeof(text)-1u-pos < 2u ?
                          sizeof(text)-1u-pos : 2u;
      check_equal(salts_search_lines_feed(&scanner, text+pos, size, &consumed),
                  SALTS_SEARCH_LINES_OK);
      check_equal(consumed, size);
      pos += consumed;
    }
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_OK);
    check_equal(ctx.match_lines, 2u);
    check_equal(probe.count, 5u);
    check_equal(probe.kind[0], SALTS_SEARCH_CONTEXT_BEFORE);
    check_equal(probe.kind[1], SALTS_SEARCH_CONTEXT_MATCH);
    check_equal(probe.kind[2], SALTS_SEARCH_CONTEXT_AFTER);
    check_equal(probe.kind[3], SALTS_SEARCH_CONTEXT_MATCH);
    check_equal(probe.kind[4], SALTS_SEARCH_CONTEXT_AFTER);
    check_equal(probe.line[0], UINT64_C(1));
    check_equal(probe.line[1], UINT64_C(2));
    check_equal(probe.line[4], UINT64_C(5));
    check_equal(probe.byte_start[1], UINT64_C(3));
    check_true(!probe.has_match_span[0]);
    check_equal(probe.match_start[0], UINT64_C(0));
    check_equal(probe.match_end[0], UINT64_C(0));
    check_true(probe.has_match_span[1]);
    check_equal(probe.match_start[1], UINT64_C(3));
    check_equal(probe.match_end[1], UINT64_C(5));
    check_true(!probe.has_match_span[2]);
    check_equal(probe.match_start[2], UINT64_C(0));
    check_equal(probe.match_end[2], UINT64_C(0));
    check_true(probe.has_match_span[3]);
    check_equal(probe.match_start[3], UINT64_C(11));
    check_equal(probe.match_end[3], UINT64_C(13));
    check_true(!probe.has_match_span[4]);
    check_true(!probe.group_break[3]);
    salts_search_matcher_destroy(&m);
  }

  it("evaluates an anchored TRE pattern on complete lines without slicing") {
    salts_search_matcher m = {0};
    salts_search_context ctx = {0};
    salts_search_context_slot slots[1] = {0};
    unsigned char history[32];
    unsigned char line_bytes[32];
    salts_search_line_scanner scanner = {0};
    stream_probe probe = {0};
    const char src[] = "xhit\nhit\nhit!\n";
    size_t consumed = 0;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "^hit$", 5u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_context_init_matcher(&ctx, &m, slots, 1u,
                history, sizeof(history), 32u, 1u, record_event, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_equal(salts_search_lines_init(&scanner, line_bytes,
                sizeof(line_bytes), SALTS_SEARCH_LINES_LF,
                SALTS_SEARCH_LINES_BINARY_ALLOW,
                salts_search_context_accept_line, &ctx),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, src, sizeof(src)-1u,
                &consumed), SALTS_SEARCH_LINES_OK);
    check_equal(consumed, sizeof(src)-1u);
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_OK);
    check_equal(ctx.match_lines, 1u);
    check_equal(probe.count, 3u);
    check_equal(probe.line[0], UINT64_C(1));
    check_equal(probe.line[1], UINT64_C(2));
    check_equal(probe.line[2], UINT64_C(3));
    check_equal(probe.kind[1], SALTS_SEARCH_CONTEXT_MATCH);
    check_true(probe.has_match_span[1]);
    check_equal(probe.match_start[1], UINT64_C(5));
    check_equal(probe.match_end[1], UINT64_C(8));
    check_true(!probe.has_match_span[0]);
    check_true(!probe.has_match_span[2]);
    salts_search_matcher_destroy(&m);
  }

  it("preserves a valid zero-width TRE match on an empty line") {
    salts_search_matcher m = {0};
    salts_search_context ctx = {0};
    stream_probe probe = {0};
    const salts_search_line empty = {NULL, 0u, UINT64_C(12),
                                     UINT64_C(12), UINT64_C(1), 1u};
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "^$", 2u, 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_context_init_matcher(&ctx, &m, NULL, 0u,
                NULL, 0u, 16u, 0u, record_event, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_true(salts_search_context_accept_line(&ctx, &empty));
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_OK);
    check_equal(probe.count, 1u);
    check_equal(probe.kind[0], SALTS_SEARCH_CONTEXT_MATCH);
    check_true(probe.has_match_span[0]);
    check_equal(probe.match_start[0], UINT64_C(12));
    check_equal(probe.match_end[0], UINT64_C(12));
    salts_search_matcher_destroy(&m);
  }

  it("returns original UTF-8 byte spans above 4 GiB and near UINT64_MAX") {
    salts_search_matcher m = {0};
    salts_search_context ctx = {0};
    stream_probe probe = {0};
    const unsigned char bytes[] = {'X', 0xe4, 0xb8, 0xad, 'Y'};
    const salts_search_line line = {bytes, sizeof(bytes),
        UINT64_C(0x100000000), UINT64_C(0x100000005), UINT64_C(1), 0u};
    const unsigned char near_bytes[] = {'x', 0, 'z'};
    const salts_search_line near_line = {near_bytes, sizeof(near_bytes),
        UINT64_MAX - UINT64_C(3), UINT64_MAX, UINT64_C(1), 0u};

    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                bytes + 1u, 3u, 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_context_init_matcher(&ctx, &m, NULL, 0u,
                NULL, 0u, 16u, 0u, record_event, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_true(salts_search_context_accept_line(&ctx, &line));
    check_equal(probe.count, 1u);
    check_true(probe.has_match_span[0]);
    check_equal(probe.match_start[0], UINT64_C(0x100000001));
    check_equal(probe.match_end[0], UINT64_C(0x100000004));
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_OK);
    salts_search_matcher_destroy(&m);

    memset(&ctx, 0, sizeof(ctx));
    memset(&probe, 0, sizeof(probe));
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                near_bytes, 1u, 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_context_init_matcher(&ctx, &m, NULL, 0u,
                NULL, 0u, 16u, 0u, record_event, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_true(salts_search_context_accept_line(&ctx, &near_line));
    check_equal(probe.count, 1u);
    check_true(probe.has_match_span[0]);
    check_equal(probe.match_start[0], UINT64_MAX - UINT64_C(3));
    check_equal(probe.match_end[0], UINT64_MAX - UINT64_C(2));
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_OK);
    salts_search_matcher_destroy(&m);
  }

  it("propagates the shared strict subject length limit through context") {
    salts_search_matcher m = {0};
    salts_search_context ctx = {0};
    stream_probe probe = {0};
    const unsigned char input[] = {'a','b','c'};
    const salts_search_line line = {input, sizeof(input), 0u, 3u, 1u, 0u};
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 16u, 2u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_context_init_matcher(&ctx, &m, NULL, 0u,
                NULL, 0u, 8u, 0u, record_event, &probe),
                SALTS_SEARCH_CONTEXT_OK);
    check_true(!salts_search_context_accept_line(&ctx, &line));
    check_equal(ctx.status, SALTS_SEARCH_CONTEXT_LIMIT);
    check_equal(probe.count, 0u);
    check_equal(salts_search_context_finish(&ctx), SALTS_SEARCH_CONTEXT_LIMIT);
    salts_search_matcher_destroy(&m);
  }
}
