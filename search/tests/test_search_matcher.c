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

typedef struct literal_hit_probe {
  salts_search_matcher_span hits[96];
  size_t count;
  size_t stop_at; /* stop after this many delivered callbacks; zero=never */
  bool overflow;
} literal_hit_probe;

static bool record_literal_hit(void *user, const salts_search_matcher_span *hit) {
  literal_hit_probe *p = (literal_hit_probe *)user;
  if (p->count >= sizeof(p->hits) / sizeof(p->hits[0])) {
    p->overflow = true;
    return false;
  }
  p->hits[p->count++] = *hit;
  return p->stop_at == 0 || p->count < p->stop_at;
}

static uint32_t literal_rand_state = UINT32_C(0x591CC011);
static uint32_t literal_rand_next(void) {
  uint32_t x = literal_rand_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return literal_rand_state = x;
}

spec("search common matcher chooses one literal or TRE byte engine") {
  it("enumerates overlapping exact TRE matches with POSIX longest spans") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t n = 44u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "aba", 3u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex(&m, "ababa", 5u,
                true, 12u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 2u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(3));
    check_equal(p.hits[1].byte_start, UINT64_C(2));
    check_equal(p.hits[1].byte_end, UINT64_C(5));
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_regex(&m, "ababa", 5u,
                false, 12u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 1u);
    salts_search_matcher_destroy(&m);
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a|ab", 4u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_regex(&m, "abab", 4u,
                false, 12u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 2u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(2));
    check_equal(p.hits[1].byte_start, UINT64_C(2));
    check_equal(p.hits[1].byte_end, UINT64_C(4));
    salts_search_matcher_destroy(&m);
  }

  it("propagates bounded first-match TRE memory rejection through common matcher") {
    enum { GROUPS = 1024 };
    char expression[GROUPS * 3u];
    salts_search_matcher matcher = {0};
    salts_search_matcher_span span = {77u, 88u};
    size_t i;
    for (i = 0u; i < GROUPS; ++i) {
      expression[3u * i] = '(';
      expression[3u * i + 1u] = 'a';
      expression[3u * i + 2u] = ')';
    }
    check_equal(salts_search_matcher_compile(&matcher,
                SALTS_SEARCH_MATCHER_REGEX, expression,
                sizeof(expression), sizeof(expression), 1u),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_find_first(&matcher, "z", 1u, &span),
                SALTS_SEARCH_MATCHER_NO_MEMORY);
    check_equal(span.byte_start, UINT64_C(0));
    check_equal(span.byte_end, UINT64_C(0));
    salts_search_matcher_destroy(&matcher);
  }

  it("bounds original-subject TRE calls without reporting incomplete success") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t n = 99u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "aba", 3u, 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "ababa", 5u,
                true, 8u, 0u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 0u);
    check_equal(p.count, 0u);
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "ababa", 5u,
                true, 8u, 1u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 1u);
    check_equal(p.count, 1u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "ababa", 5u,
                true, 8u, 2u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 2u); /* both hits emitted, but final no-match is unproven */
    check_equal(p.count, 2u);
    check_equal(p.hits[1].byte_start, UINT64_C(2));
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "ababa", 5u,
                true, 8u, 3u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 2u);
    check_equal(p.count, 2u);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "ababa", 5u,
                false, 8u, 2u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_OK); /* one hit, then proven no-match */
    check_equal(n, 1u);
    check_equal(p.count, 1u);
    salts_search_matcher_destroy(&m);
  }

  it("keeps scan budget conservative on no-match, hit-cap, and cancel") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t n = 99u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "zzz", 3u,
                false, 8u, 0u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 0u);
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "zzz", 3u,
                false, 8u, 1u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 0u);
    check_equal(p.count, 0u);
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "aaa", 3u,
                true, 0u, 1u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_LIMIT); /* one observed hit, no callback */
    check_equal(n, 0u);
    p.stop_at = 1u;
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "aaa", 3u,
                true, 8u, 1u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_STOPPED);
    check_equal(n, 1u);
    check_equal(p.count, 1u);
    salts_search_matcher_destroy(&m);
  }

  it("bounds zero-width enumeration through the original EOF boundary") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t n = 0;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a*", 2u, 8u, 8u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "ba", 2u,
                false, 8u, 2u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 2u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(0));
    check_equal(p.hits[1].byte_start, UINT64_C(1));
    check_equal(p.hits[1].byte_end, UINT64_C(2));
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_regex_bounded(&m, "ba", 2u,
                false, 8u, 3u, record_literal_hit, &p, &n),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 3u);
    check_equal(p.hits[2].byte_start, UINT64_C(2));
    check_equal(p.hits[2].byte_end, UINT64_C(2));
    salts_search_matcher_destroy(&m);
  }

  it("owns cumulative TRE scan and hit budgets across original lines") {
    salts_search_matcher m = {0};
    salts_search_matcher_query_budget owner = {0};
    literal_hit_probe p = {0};
    size_t n = 99u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "aba", 3u, 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_budget_init(&owner, 4u, 4u),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "ababa", 5u, true,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 2u);
    check_equal(owner.total_emitted, 2u);
    check_equal(owner.total_scans, 3u); /* two hits + final no-match */
    check_equal(owner.remaining_hits, 2u);
    check_equal(owner.remaining_scans, 1u);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "x", 1u, true,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 0u);
    check_equal(owner.total_scans, 4u);
    check_equal(owner.remaining_scans, 0u);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "x", 1u, true,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 0u);
    check_equal(owner.total_scans, 4u);
    check_true(owner.terminal);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "x", 1u, true,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_INVALID);
    salts_search_matcher_destroy(&m);
  }

  it("never hides query-wide hit exhaustion or early callback stop") {
    salts_search_matcher m = {0};
    salts_search_matcher_query_budget owner = {0};
    literal_hit_probe p = {0};
    size_t n = 17u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, 8u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_budget_init(&owner, 1u, 5u),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "a", 1u, false,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 1u);
    check_equal(owner.remaining_hits, 0u);
    check_equal(owner.total_scans, 2u);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "aaa", 3u, false,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 0u);
    check_equal(p.count, 1u);
    check_equal(owner.total_scans, 3u);
    check_true(owner.terminal);
    salts_search_matcher_destroy(&m);
    owner = (salts_search_matcher_query_budget){0};
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, 8u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_budget_init(&owner, 4u, 1u),
                SALTS_SEARCH_MATCHER_OK);
    p.stop_at = 1u;
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "aaa", 3u, true,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_STOPPED);
    check_equal(n, 1u);
    check_equal(owner.total_emitted, 1u);
    check_equal(owner.remaining_hits, 3u);
    check_equal(owner.remaining_scans, 0u);
    check_true(owner.terminal);
    salts_search_matcher_destroy(&m);
  }

  it("keeps literal query budgets separate from TRE invocation quota") {
    salts_search_matcher m = {0};
    salts_search_matcher_query_budget owner = {0};
    literal_hit_probe p = {0};
    size_t n = 9u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                "a", 1u, 8u, 8u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_budget_init(&owner, 2u, 0u),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "a", 1u, false,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 1u);
    check_equal(owner.total_scans, 0u);
    check_equal(owner.remaining_hits, 1u);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "a", 1u, false,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 1u);
    check_equal(owner.total_emitted, 2u);
    check_equal(owner.remaining_hits, 0u);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "b", 1u, false,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 0u); /* no more hits, even with quota == 0 */
    check_equal(owner.total_emitted, 2u);
    check_equal(owner.total_scans, 0u);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "a", 1u, false,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 0u);
    check_true(owner.terminal);
    salts_search_matcher_destroy(&m);
  }

  it("never treats terminal regex budget zero or invalid owner as complete") {
    salts_search_matcher m = {0};
    salts_search_matcher_query_budget owner = {0};
    literal_hit_probe p = {0};
    size_t n = 18u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "z", 1u, 8u, 8u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "xxx", 3u, false,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_INVALID);
    check_equal(n, 0u);
    check_equal(salts_search_matcher_query_budget_init(&owner, 2u, 0u),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_query_budget_init(&owner, 2u, 0u),
                SALTS_SEARCH_MATCHER_INVALID);
    check_equal(salts_search_matcher_query_enumerate(
                &owner, &m, "xxx", 3u, false,
                record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(owner.total_scans, 0u);
    check_equal(owner.total_emitted, 0u);
    check_true(owner.terminal);
    salts_search_matcher_destroy(&m);
  }

  it("does not turn an anchored or word-boundary TRE match into a suffix match") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t n = 0;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "^a", 2u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex(&m, "aa", 2u,
                true, 8u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 1u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(1));
    salts_search_matcher_destroy(&m);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "\\bfoo", 5u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex(&m, "xfoo foo", 8u,
                false, 8u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 1u); /* no false match on "foo" inside "xfoo" */
    check_equal(p.hits[0].byte_start, UINT64_C(5));
    check_equal(p.hits[0].byte_end, UINT64_C(8));
    salts_search_matcher_destroy(&m);
  }

  it("advances zero-width TRE results once per original byte boundary") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t n = 0;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a*", 2u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex(&m, "ba", 2u,
                false, 8u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 3u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(0));
    check_equal(p.hits[1].byte_start, UINT64_C(1));
    check_equal(p.hits[1].byte_end, UINT64_C(2));
    check_equal(p.hits[2].byte_start, UINT64_C(2));
    check_equal(p.hits[2].byte_end, UINT64_C(2));
    salts_search_matcher_destroy(&m);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "^$", 2u, 16u, 32u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex(&m, NULL, 0u,
                false, 8u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 1u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(0));
    salts_search_matcher_destroy(&m);
  }

  it("keeps binary NUL intact and reports strict TRE result budgets") {
    const unsigned char bytes[] = {'a', 0, 'a'};
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t n = 99u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "a", 1u, 8u, 3u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_regex(&m, bytes, sizeof(bytes),
                false, 1u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 1u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    memset(&p, 0, sizeof(p));
    p.stop_at = 1u;
    check_equal(salts_search_matcher_enumerate_regex(&m, bytes, sizeof(bytes),
                false, 8u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_STOPPED);
    check_equal(n, 1u);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_regex(&m, bytes, sizeof(bytes),
                false, 8u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_OK);
    check_equal(n, 2u);
    check_equal(p.hits[1].byte_start, UINT64_C(2));
    check_equal(p.hits[1].byte_end, UINT64_C(3));
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_regex(&m, bytes, sizeof(bytes),
                true, 0u, record_literal_hit, &p, &n), SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(n, 0u);
    salts_search_matcher_destroy(&m);
  }

  it("enumerates overlapping and nonoverlapping Literal hits in order") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t count = 999u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                "aba", 3u, 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_literals(&m, "ababa", 5u,
                true, 10u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(count, 2u);
    check_equal(p.count, 2u);
    check_true(!p.overflow);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(3));
    check_equal(p.hits[1].byte_start, UINT64_C(2));
    check_equal(p.hits[1].byte_end, UINT64_C(5));

    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_literals(&m, "ababa", 5u,
                false, 10u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(count, 1u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(3));
    salts_search_matcher_destroy(&m);
  }

  it("enumerates binary NUL matches and every empty-pattern byte boundary") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t count = 0;
    const unsigned char raw[] = {'a', 0, 'a', 0, 'a'};
    const unsigned char needle[] = {0, 'a'};
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                needle, sizeof(needle), 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_literals(&m, raw, sizeof(raw),
                true, 8u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(count, 2u);
    check_equal(p.hits[0].byte_start, UINT64_C(1));
    check_equal(p.hits[0].byte_end, UINT64_C(3));
    check_equal(p.hits[1].byte_start, UINT64_C(3));
    check_equal(p.hits[1].byte_end, UINT64_C(5));
    salts_search_matcher_destroy(&m);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                NULL, 0u, 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_literals(&m, "ab", 2u,
                false, 8u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(count, 3u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[1].byte_start, UINT64_C(1));
    check_equal(p.hits[2].byte_start, UINT64_C(2));
    check_equal(p.hits[2].byte_end, UINT64_C(2));
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_literals(&m, NULL, 0u,
                true, 8u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(count, 1u);
    check_equal(p.hits[0].byte_start, UINT64_C(0));
    check_equal(p.hits[0].byte_end, UINT64_C(0));
    salts_search_matcher_destroy(&m);
  }

  it("never reports success after hit-budget exhaustion or callback cancellation") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t count = 99u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                "a", 1u, 8u, 4u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_literals(&m, "aaa", 3u,
                true, 2u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(count, 2u);
    check_equal(p.count, 2u);
    memset(&p, 0, sizeof(p));
    p.stop_at = 1u;
    check_equal(salts_search_matcher_enumerate_literals(&m, "aaa", 3u,
                false, 9u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_STOPPED);
    check_equal(count, 1u);
    check_equal(p.count, 1u);
    memset(&p, 0, sizeof(p));
    check_equal(salts_search_matcher_enumerate_literals(&m, "aaa", 3u,
                true, 0u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_LIMIT);
    check_equal(count, 0u);
    check_equal(p.count, 0u);
    check_equal(salts_search_matcher_enumerate_literals(&m, "bbb", 3u,
                true, 0u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_OK);
    check_equal(count, 0u);
    check_equal(salts_search_matcher_enumerate_literals(&m, "aaaaa", 5u,
                true, 9u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_LIMIT); /* subject exceeds common bound */
    check_equal(count, 0u);
    salts_search_matcher_destroy(&m);
  }

  it("refuses regex all-hit enumeration instead of slicing anchored TRE input") {
    salts_search_matcher m = {0};
    literal_hit_probe p = {0};
    size_t count = 123u;
    check_equal(salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_REGEX,
                "^a", 2u, 16u, 16u), SALTS_SEARCH_MATCHER_OK);
    check_equal(salts_search_matcher_enumerate_literals(&m, "aa", 2u,
                false, 8u, record_literal_hit, &p, &count),
                SALTS_SEARCH_MATCHER_UNSUPPORTED);
    check_equal(count, 0u);
    check_equal(p.count, 0u);
    salts_search_matcher_destroy(&m);
  }

  it("agrees with an independent brute-force literal-hit reference") {
    unsigned char input[40], needle[12];
    size_t trial;
    bool all_equal = true;
    for (trial = 0u; trial < 3500u && all_equal; ++trial) {
      salts_search_matcher m = {0};
      literal_hit_probe p = {0};
      salts_search_matcher_span expected[41] = {{0}};
      const size_t length = literal_rand_next() % 41u;
      const size_t pat_length = literal_rand_next() % 13u;
      const unsigned alphabet = trial % 3u == 0u ? 1u :
                                trial % 3u == 1u ? 3u : 256u;
      const bool overlapping = (literal_rand_next() & 1u) != 0u;
      size_t i, count = 0, emitted = 0, next_nonoverlap = 0;
      for (i = 0u; i < length; ++i)
        input[i] = (unsigned char)(literal_rand_next() % alphabet);
      for (i = 0u; i < pat_length; ++i)
        needle[i] = (unsigned char)(literal_rand_next() % alphabet);
      for (i = 0u; i <= length; ++i) {
        if (i < next_nonoverlap)
          continue;
        if (pat_length != 0u &&
            (pat_length > length - i ||
             memcmp(input + i, needle, pat_length) != 0))
          continue;
        expected[count].byte_start = (uint64_t)i;
        expected[count].byte_end = (uint64_t)(i + pat_length);
        ++count;
        if (!overlapping && pat_length)
          next_nonoverlap = i + pat_length;
      }
      if (salts_search_matcher_compile(&m, SALTS_SEARCH_MATCHER_LITERAL,
                    needle, pat_length, 16u, 40u) != SALTS_SEARCH_MATCHER_OK) {
        all_equal = false;
        break;
      }
      if (salts_search_matcher_enumerate_literals(&m, input, length,
                 overlapping, 41u, record_literal_hit, &p, &emitted) !=
                    SALTS_SEARCH_MATCHER_OK ||
          emitted != count || p.count != count || p.overflow)
        all_equal = false;
      for (i = 0u; i < count && all_equal; ++i)
        if (p.hits[i].byte_start != expected[i].byte_start ||
            p.hits[i].byte_end != expected[i].byte_end)
          all_equal = false;
      salts_search_matcher_destroy(&m);
    }
    check_true(all_equal);
    check_equal(trial, 3500u);
  }

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
