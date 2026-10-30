#include "search_literal.h"
#include <tinytest.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The algorithm is the subject of this test: reference is deliberately naive.
 * Randomized checks are deterministic and do not exercise any FS code. */
static uint32_t search_test_seed = UINT32_C(0x71AC2026);
static uint32_t search_test_random(void) {
  uint32_t x = search_test_seed;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  search_test_seed = x;
  return x;
}

static size_t reference_find(const unsigned char *data, size_t data_size,
                             const unsigned char *needle, size_t needle_size,
                             size_t start_at) {
  size_t i;
  if (start_at > data_size || needle_size > data_size - start_at)
    return SIZE_MAX;
  if (needle_size == 0)
    return start_at;
  for (i = start_at; i <= data_size - needle_size; ++i) {
    if (memcmp(data + i, needle, needle_size) == 0)
      return i;
  }
  return SIZE_MAX;
}

static int compare_with_reference(const unsigned char *data, size_t data_size,
                                  const unsigned char *needle,
                                  size_t needle_size, size_t start_at) {
  salts_search_literal_pattern compiled = {0};
  salts_search_literal_span got = {SIZE_MAX, SIZE_MAX};
  const size_t expected = reference_find(data, data_size, needle,
                                         needle_size, start_at);
  int equal;
  if (salts_search_literal_compile(&compiled, needle, needle_size,
                                   256u) != SALTS_SEARCH_LITERAL_OK)
    return 0;
  const salts_search_literal_status rc = salts_search_literal_find(
      &compiled, data, data_size, start_at, &got);
  equal = expected == SIZE_MAX
              ? rc == SALTS_SEARCH_LITERAL_NOT_FOUND && got.start == 0u &&
                    got.end == 0u
              : rc == SALTS_SEARCH_LITERAL_OK && got.start == expected &&
                    got.end == expected + needle_size;
  salts_search_literal_destroy(&compiled);
  return equal;
}

spec("search A0 literal Two-Way (pure C11, no files)") {
  it("matches owned pattern bytes, raw NUL and overlapping spans") {
    unsigned char needle[] = {0x00, 'x', 0x00};
    const unsigned char text[] = {'a', 0, 'x', 0, 0, 'x', 0, 'z'};
    salts_search_literal_pattern compiled = {0};
    salts_search_literal_span span = {0};

    check_equal(salts_search_literal_compile(&compiled, needle, 3u, 16u),
                SALTS_SEARCH_LITERAL_OK);
    needle[1] = 'q'; /* the matcher owns a copy */
    check_equal(salts_search_literal_find(&compiled, text, sizeof(text),
                                          0u, &span), SALTS_SEARCH_LITERAL_OK);
    check_equal(span.start, 1u);
    check_equal(span.end, 4u);
    check_equal(salts_search_literal_find(&compiled, text, sizeof(text),
                                          2u, &span), SALTS_SEARCH_LITERAL_OK);
    check_equal(span.start, 4u);
    check_equal(span.end, 7u);
    check_equal(salts_search_literal_find(&compiled, text, sizeof(text),
                                          5u, &span), SALTS_SEARCH_LITERAL_NOT_FOUND);
    check_equal(span.start, 0u);
    check_equal(span.end, 0u);
    salts_search_literal_destroy(&compiled);
    check_true(!compiled.compiled);
    check_null(compiled.bytes);
  }

  it("defines empty-pattern, empty-input and invalid admission") {
    salts_search_literal_pattern p = {0};
    salts_search_literal_span span = {19u, 19u};
    const unsigned char input[] = {'a', 'b'};
    check_equal(salts_search_literal_compile(&p, NULL, 0u, 4u),
                SALTS_SEARCH_LITERAL_OK);
    check_equal(salts_search_literal_find(&p, input, sizeof(input),
                                          2u, &span), SALTS_SEARCH_LITERAL_OK);
    check_equal(span.start, 2u);
    check_equal(span.end, 2u);
    check_equal(salts_search_literal_find(&p, input, sizeof(input),
                                          3u, &span), SALTS_SEARCH_LITERAL_NOT_FOUND);
    check_equal(salts_search_literal_find(&p, NULL, 0u,
                                          0u, &span), SALTS_SEARCH_LITERAL_OK);
    check_equal(salts_search_literal_find(&p, NULL, 1u,
                                          0u, &span), SALTS_SEARCH_LITERAL_INVALID);
    check_equal(salts_search_literal_compile(&p, "x", 1u, 4u),
                SALTS_SEARCH_LITERAL_INVALID);
    salts_search_literal_destroy(&p);
    check_equal(salts_search_literal_compile(&p, NULL, 1u, 4u),
                SALTS_SEARCH_LITERAL_INVALID);
    check_equal(salts_search_literal_compile(&p, input, 2u, 1u),
                SALTS_SEARCH_LITERAL_LIMIT);
    check_equal(salts_search_literal_compile(&p, input, 2u, 0u),
                SALTS_SEARCH_LITERAL_INVALID);
    check_equal(salts_search_literal_find(&p, input, 2u, 0u, &span),
                SALTS_SEARCH_LITERAL_INVALID);
    check_equal(salts_search_literal_compile(&p, input, 2u, 2u),
                SALTS_SEARCH_LITERAL_OK);
    check_equal(salts_search_literal_find(&p, input, 2u, 0u, NULL),
                SALTS_SEARCH_LITERAL_INVALID);
    salts_search_literal_destroy(&p);
    salts_search_literal_destroy(&p);
  }

  it("correctly finds periodic and nonperiodic worst-case patterns") {
    unsigned char haystack[65536];
    unsigned char needle[128];
    memset(haystack, 'a', sizeof(haystack));
    memset(needle, 'a', sizeof(needle));
    needle[sizeof(needle)-1u] = 'b';
    check_true(compare_with_reference(haystack, sizeof(haystack),
                                      needle, sizeof(needle), 0u));
    haystack[sizeof(haystack)-1u] = 'b';
    check_true(compare_with_reference(haystack, sizeof(haystack),
                                      needle, sizeof(needle), 0u));
    check_true(compare_with_reference(haystack, sizeof(haystack),
                                      needle, sizeof(needle), 65000u));
    check_true(compare_with_reference((const unsigned char *)"ababababab",
                                      10u, (const unsigned char *)"abab", 4u, 1u));
    check_true(compare_with_reference((const unsigned char *)"ababababab",
                                      10u, (const unsigned char *)"abba", 4u, 0u));
    check_true(compare_with_reference((const unsigned char *)"aaaaaaa",
                                      7u, (const unsigned char *)"aa", 2u, 1u));
  }

  it("is byte-exact against deterministic randomized reference cases") {
    unsigned char text[192];
    unsigned char needle[96];
    size_t trial;
    int all_matched = 1;
    for (trial = 0u; trial < 24000u; ++trial) {
      const size_t text_length = search_test_random() % (sizeof(text) + 1u);
      const size_t needle_length = search_test_random() % (sizeof(needle) + 1u);
      const size_t start_at = search_test_random() % (text_length + 2u);
      const unsigned alphabet = (trial % 4u == 0u) ? 1u :
                                (trial % 4u == 1u) ? 2u :
                                (trial % 4u == 2u) ? 4u : 256u;
      size_t i;
      for (i = 0u; i < text_length; ++i)
        text[i] = (unsigned char)(search_test_random() % alphabet);
      for (i = 0u; i < needle_length; ++i)
        needle[i] = (unsigned char)(search_test_random() % alphabet);
      if (!compare_with_reference(text, text_length, needle,
                                  needle_length, start_at)) {
        all_matched = 0;
        break;
      }
    }
    check_true(all_matched);
    check_equal(trial, 24000u);
  }
}
