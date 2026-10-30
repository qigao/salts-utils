#include "search_lines.h"
#include "search_literal.h"
#include <tinytest.h>

#include <stdint.h>
#include <string.h>

#define TEST_MAX_LINES 256u
#define TEST_MAX_BYTES 256u

typedef struct line_record {
  unsigned char content[TEST_MAX_BYTES];
  size_t length;
  uint64_t start;
  uint64_t end;
  uint64_t number;
  unsigned terminator;
} line_record;

typedef struct line_probe {
  line_record records[TEST_MAX_LINES];
  size_t count;
  size_t stop_at; /* zero means never stop */
  bool overflow;
} line_probe;

static bool line_collect(void *user, const salts_search_line *line) {
  line_probe *probe = (line_probe *)user;
  line_record *dst;
  if (probe->count >= TEST_MAX_LINES || line->length > TEST_MAX_BYTES) {
    probe->overflow = true;
    return false;
  }
  dst = &probe->records[probe->count++];
  dst->length = line->length;
  if (line->length)
    memcpy(dst->content, line->bytes, line->length);
  dst->start = line->byte_start;
  dst->end = line->byte_end;
  dst->number = line->line_number;
  dst->terminator = line->terminator_size;
  return probe->stop_at == 0 || probe->count < probe->stop_at;
}

static void reference_emit(line_probe *probe, const unsigned char *data,
                           size_t start, size_t length, unsigned term) {
  line_record *dst = &probe->records[probe->count++];
  dst->length = length;
  if (length)
    memcpy(dst->content, data + start, length);
  dst->start = (uint64_t)start;
  dst->end = (uint64_t)(start + length);
  dst->number = (uint64_t)probe->count;
  dst->terminator = term;
}

static void reference_lines(line_probe *probe, const unsigned char *data,
                            size_t length, salts_search_lines_newline mode) {
  size_t start = 0;
  size_t i = 0;
  memset(probe, 0, sizeof(*probe));
  while (i < length) {
    if (data[i] == '\n') {
      const bool crlf = i > start && data[i - 1] == '\r';
      reference_emit(probe, data, start, i - start - (crlf ? 1u : 0u),
                     crlf ? 2u : 1u);
      start = ++i;
    } else if (mode == SALTS_SEARCH_LINES_UNIVERSAL && data[i] == '\r') {
      const bool crlf = i + 1u < length && data[i + 1u] == '\n';
      reference_emit(probe, data, start, i - start, crlf ? 2u : 1u);
      i += crlf ? 2u : 1u;
      start = i;
    } else {
      ++i;
    }
  }
  if (start < length)
    reference_emit(probe, data, start, length - start, 0u);
}

static int probes_equal(const line_probe *a, const line_probe *b) {
  size_t i;
  if (a->overflow || b->overflow || a->count != b->count)
    return 0;
  for (i = 0; i < a->count; ++i) {
    const line_record *x = &a->records[i], *y = &b->records[i];
    if (x->length != y->length || x->start != y->start ||
        x->end != y->end || x->number != y->number ||
        x->terminator != y->terminator ||
        (x->length && memcmp(x->content, y->content, x->length) != 0))
      return 0;
  }
  return 1;
}

static int verify_chunk_size(const unsigned char *data, size_t length,
                             salts_search_lines_newline mode, size_t chunk) {
  unsigned char arena[TEST_MAX_BYTES];
  line_probe expected = {0}, seen = {0};
  salts_search_line_scanner scanner = {0};
  size_t consumed = 0, pos = 0;
  salts_search_lines_status rc;
  reference_lines(&expected, data, length, mode);
  rc = salts_search_lines_init(&scanner, arena, sizeof(arena), mode,
                               SALTS_SEARCH_LINES_BINARY_ALLOW, line_collect,
                               &seen);
  if (rc != SALTS_SEARCH_LINES_OK)
    return 0;
  while (pos < length) {
    const size_t bytes = length - pos < chunk ? length - pos : chunk;
    rc = salts_search_lines_feed(&scanner, data + pos, bytes, &consumed);
    if (rc != SALTS_SEARCH_LINES_OK || consumed != bytes)
      return 0;
    pos += consumed;
    /* Empty chunks must not cause the last line to flush. */
    if (salts_search_lines_feed(&scanner, NULL, 0u, &consumed) !=
            SALTS_SEARCH_LINES_OK || consumed != 0)
      return 0;
  }
  rc = salts_search_lines_finish(&scanner);
  if (rc != SALTS_SEARCH_LINES_OK ||
      salts_search_lines_finish(&scanner) != SALTS_SEARCH_LINES_CLOSED)
    return 0;
  return probes_equal(&expected, &seen);
}

static uint32_t rand_state = UINT32_C(0xA207C11);
static uint32_t next_rand(void) {
  uint32_t x = rand_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return rand_state = x;
}

typedef struct match_probe {
  const salts_search_literal_pattern *pattern;
  uint64_t hits[16];
  size_t count;
  bool failed;
} match_probe;

static bool line_find_literal(void *user, const salts_search_line *line) {
  match_probe *probe = (match_probe *)user;
  salts_search_literal_span match = {0};
  salts_search_literal_status rc;
  size_t at = 0;
  do {
    rc = salts_search_literal_find(probe->pattern, line->bytes,
                                   line->length, at, &match);
    if (rc == SALTS_SEARCH_LITERAL_NOT_FOUND)
      return true;
    if (rc != SALTS_SEARCH_LITERAL_OK || probe->count == 16u) {
      probe->failed = true;
      return false;
    }
    probe->hits[probe->count++] = line->byte_start + match.start;
    at = match.start + 1u; /* enumerate overlapping literal matches */
  } while (at <= line->length);
  return true;
}

spec("search A2 line scanner: pure C11 byte chunks") {
  it("does not create a line for an empty file or after a final delimiter") {
    static const unsigned char empty[] = "";
    static const unsigned char lines[] = "\n\n";
    check_true(verify_chunk_size(empty, 0u, SALTS_SEARCH_LINES_LF, 1u));
    check_true(verify_chunk_size(lines, 2u, SALTS_SEARCH_LINES_LF, 1u));
    check_true(verify_chunk_size(lines, 2u, SALTS_SEARCH_LINES_UNIVERSAL, 2u));
  }

  it("retains CRLF across every chunk split and lone CR in default LF mode") {
    static const unsigned char bytes[] = {'a','\r','\n','\n','b','\r','c','\n',0,'d','\r','\n','z'};
    size_t step;
    for (step = 1u; step <= sizeof(bytes); ++step) {
      check_true(verify_chunk_size(bytes, sizeof(bytes), SALTS_SEARCH_LINES_LF, step));
      check_true(verify_chunk_size(bytes, sizeof(bytes), SALTS_SEARCH_LINES_UNIVERSAL, step));
    }
  }

  it("supports CR-only mode, trailing CR and CRLF at exact line capacity") {
    static const unsigned char bytes[] = {'a','b','\r','c','\r','\n','\r','z','\r'};
    unsigned char arena[2];
    line_probe probe = {0};
    salts_search_line_scanner scanner = {0};
    size_t n = 0;
    check_true(verify_chunk_size(bytes, sizeof(bytes), SALTS_SEARCH_LINES_UNIVERSAL, 1u));
    check_true(verify_chunk_size(bytes, sizeof(bytes), SALTS_SEARCH_LINES_LF, 1u));
    check_equal(salts_search_lines_init(&scanner, arena, sizeof(arena),
                  SALTS_SEARCH_LINES_UNIVERSAL, SALTS_SEARCH_LINES_BINARY_ALLOW,
                  line_collect, &probe), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "ab\r\n", 4u, &n),
                SALTS_SEARCH_LINES_OK);
    check_equal(n, 4u);
    check_equal(probe.count, 1u);
    check_equal(probe.records[0].length, 2u);
    check_equal(probe.records[0].terminator, 2u);
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_OK);
  }

  it("reports the exact 64-bit starting offset without truncating") {
    unsigned char arena[8];
    line_probe probe = {0};
    salts_search_line_scanner scanner = {0};
    size_t n = 0;
    check_equal(salts_search_lines_init(&scanner, arena, sizeof(arena),
                    SALTS_SEARCH_LINES_LF, SALTS_SEARCH_LINES_BINARY_ALLOW,
                    line_collect, &probe), SALTS_SEARCH_LINES_OK);
    scanner.byte_offset = UINT64_C(0x100000003);
    scanner.line_start = scanner.byte_offset;
    scanner.line_number = 9000000000ULL;
    check_equal(salts_search_lines_feed(&scanner, "hi\n", 3u, &n),
                SALTS_SEARCH_LINES_OK);
    check_equal(probe.count, 1u);
    check_equal(probe.records[0].start, UINT64_C(0x100000003));
    check_equal(probe.records[0].end, UINT64_C(0x100000005));
    check_equal(probe.records[0].number, UINT64_C(9000000000));
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_OK);
  }

  it("fails closed on binary rejection, line budget and offset overflow") {
    unsigned char arena[2] = {0};
    const unsigned char raw[] = {'a', 0, 'z'};
    line_probe probe = {0};
    salts_search_line_scanner scanner = {0};
    size_t n = SIZE_MAX;
    check_equal(salts_search_lines_init(&scanner, arena, sizeof(arena),
                    SALTS_SEARCH_LINES_LF,
                    SALTS_SEARCH_LINES_BINARY_REJECT_NUL,
                    line_collect, &probe), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, raw, sizeof(raw), &n),
                SALTS_SEARCH_LINES_BINARY);
    check_equal(n, 1u);
    check_equal(salts_search_lines_feed(&scanner, "x", 1u, &n),
                SALTS_SEARCH_LINES_CLOSED);
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_CLOSED);

    memset(&scanner, 0, sizeof(scanner));
    check_equal(salts_search_lines_init(&scanner, arena, sizeof(arena),
                    SALTS_SEARCH_LINES_LF, SALTS_SEARCH_LINES_BINARY_ALLOW,
                    line_collect, &probe), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "abc", 3u, &n),
                SALTS_SEARCH_LINES_LIMIT);
    check_equal(n, 3u);
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_CLOSED);

    memset(&scanner, 0, sizeof(scanner));
    check_equal(salts_search_lines_init(&scanner, arena, sizeof(arena),
                    SALTS_SEARCH_LINES_LF, SALTS_SEARCH_LINES_BINARY_ALLOW,
                    line_collect, &probe), SALTS_SEARCH_LINES_OK);
    scanner.byte_offset = UINT64_MAX;
    scanner.line_start = UINT64_MAX;
    check_equal(salts_search_lines_feed(&scanner, "x", 1u, &n),
                SALTS_SEARCH_LINES_OVERFLOW);
    check_equal(n, 0u);
  }

  it("supports stopping from a callback without consuming the next byte") {
    unsigned char arena[16];
    salts_search_line_scanner scanner = {0};
    line_probe probe = {0};
    size_t n = 0;
    probe.stop_at = 1u;
    check_equal(salts_search_lines_init(&scanner, arena, sizeof(arena),
                  SALTS_SEARCH_LINES_UNIVERSAL, SALTS_SEARCH_LINES_BINARY_ALLOW,
                  line_collect, &probe), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "a\rb", 3u, &n),
                SALTS_SEARCH_LINES_STOPPED);
    check_equal(n, 2u); /* a, CR consumed; b not consumed */
    check_equal(probe.count, 1u);
    check_equal(probe.records[0].length, 1u);
    check_equal(probe.records[0].terminator, 1u);
  }

  it("matches a literal crossing input chunk boundaries in one complete line") {
    unsigned char arena[32];
    salts_search_line_scanner scanner = {0};
    salts_search_literal_pattern pattern = {0};
    match_probe probe = {0};
    size_t n = 0;
    check_equal(salts_search_literal_compile(&pattern, "aba", 3u, 16u),
                SALTS_SEARCH_LITERAL_OK);
    probe.pattern = &pattern;
    check_equal(salts_search_lines_init(&scanner, arena, sizeof(arena),
                  SALTS_SEARCH_LINES_LF, SALTS_SEARCH_LINES_BINARY_ALLOW,
                  line_find_literal, &probe), SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "xxab", 4u, &n),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "aba\nabc", 7u, &n),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_feed(&scanner, "aba", 3u, &n),
                SALTS_SEARCH_LINES_OK);
    check_equal(salts_search_lines_finish(&scanner), SALTS_SEARCH_LINES_OK);
    check_true(!probe.failed);
    check_equal(probe.count, 3u);
    check_equal(probe.hits[0], 2u);
    check_equal(probe.hits[1], 4u);
    check_equal(probe.hits[2], 11u);
    salts_search_literal_destroy(&pattern);
  }

  it("matches independent reference on deterministic randomized byte streams") {
    unsigned char input[96];
    size_t trial;
    int all_match = 1;
    for (trial = 0; trial < 2500u && all_match; ++trial) {
      const size_t length = next_rand() % (sizeof(input) + 1u);
      const salts_search_lines_newline mode = (next_rand() & 1u)
                                            ? SALTS_SEARCH_LINES_LF
                                            : SALTS_SEARCH_LINES_UNIVERSAL;
      const size_t chunk = 1u + next_rand() % 21u;
      size_t i;
      for (i = 0; i < length; ++i) {
        const unsigned choice = next_rand() % 20u;
        input[i] = choice < 3u ? '\n' : choice < 6u ? '\r' :
                   choice == 6u ? 0 : (unsigned char)(next_rand() & 0xffu);
      }
      if (!verify_chunk_size(input, length, mode, chunk) ||
          !verify_chunk_size(input, length, mode, 1u))
        all_match = 0;
    }
    check_true(all_match);
    check_equal(trial, 2500u);
  }
}
