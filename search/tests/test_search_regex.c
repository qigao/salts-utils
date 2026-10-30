#include "search_regex.h"
#include <tinytest.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

spec("search A1 unique TRE exact Tagged NFA (C11)") {
  it("matches from the original subject without reinterpreting BOL") {
    salts_search_regex_pattern p = {0};
    salts_search_regex_span span = {99,99};
    check_equal(salts_search_regex_compile(&p, "^a", 2u, 16u, 32u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_from(&p, "aa", 2u, 0u, &span),
                SALTS_SEARCH_REGEX_OK);
    check_equal(span.byte_start, UINT64_C(0));
    check_equal(span.byte_end, UINT64_C(1));
    check_equal(salts_search_regex_find_from(&p, "aa", 2u, 1u, &span),
                SALTS_SEARCH_REGEX_NOT_FOUND);
    check_equal(span.byte_start, UINT64_C(0));
    check_equal(span.byte_end, UINT64_C(0));
    salts_search_regex_destroy(&p);
  }

  it("preserves original byte offsets and POSIX alternatives past a start") {
    salts_search_regex_pattern p = {0};
    salts_search_regex_span span = {0};
    check_equal(salts_search_regex_compile(&p, "a|ab", 4u, 16u, 32u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_from(&p, "abab", 4u, 0u, &span),
                SALTS_SEARCH_REGEX_OK);
    check_equal(span.byte_start, UINT64_C(0));
    check_equal(span.byte_end, UINT64_C(2));
    check_equal(salts_search_regex_find_from(&p, "abab", 4u, 1u, &span),
                SALTS_SEARCH_REGEX_OK);
    check_equal(span.byte_start, UINT64_C(2));
    check_equal(span.byte_end, UINT64_C(4));
    check_equal(salts_search_regex_find_from(&p, "abab", 4u, 3u, &span),
                SALTS_SEARCH_REGEX_NOT_FOUND);
    check_equal(salts_search_regex_find_from(&p, "abab", 4u, 5u, &span),
                SALTS_SEARCH_REGEX_NOT_FOUND);
    salts_search_regex_destroy(&p);
  }

  it("keeps TRE word-boundary assertions on the full original string") {
    salts_search_regex_pattern p = {0};
    salts_search_regex_span span = {0};
    check_equal(salts_search_regex_compile(&p, "\\bfoo", 5u, 16u, 32u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_from(&p, "xfoo", 4u, 1u, &span),
                SALTS_SEARCH_REGEX_NOT_FOUND); /* suffix scan would falsely match */
    check_equal(salts_search_regex_find_from(&p, "x foo", 5u, 2u, &span),
                SALTS_SEARCH_REGEX_OK);
    check_equal(span.byte_start, UINT64_C(2));
    check_equal(span.byte_end, UINT64_C(5));
    salts_search_regex_destroy(&p);
  }

  it("preserves original newline assertions through the extended TRE entry") {
    regex_t pattern = {0};
    regmatch_t match[1] = {{-1,-1}};
    const char *text = "b\na\n";
    check_equal(tre_regncompb(&pattern, "^a", 2u,
                              REG_EXTENDED | REG_NEWLINE), REG_OK);
    check_equal(tre_regnexecb_from(&pattern, text, 4u, 1u,
                                   1u, match, 0), REG_OK);
    check_equal(match[0].rm_so, 2);
    check_equal(match[0].rm_eo, 3);
    check_equal(tre_regnexecb_from(&pattern, text, 4u, 3u,
                                   1u, match, 0), REG_NOMATCH);
    tre_regfree(&pattern);
  }

  it("rejects forced alternate execution paths and oversized source lengths") {
    regex_t pattern = {0};
    regmatch_t match[1] = {{-1,-1}};
    const char subject[] = "a";
    check_equal(tre_regncompb(&pattern, "a", 1u, REG_EXTENDED), REG_OK);
    check_equal(tre_regnexecb_from(&pattern, subject, 1u, 0u, 1u,
                     match, REG_APPROX_MATCHER), REG_BADPAT);
    check_equal(tre_regnexecb_from(&pattern, subject, 1u, 0u, 1u,
                     match, REG_BACKTRACKING_MATCHER), REG_BADPAT);
#if SIZE_MAX > INT_MAX
    check_equal(tre_regnexecb_from(&pattern, subject, (size_t)INT_MAX + 1u,
                     0u, 1u, match, 0), REG_ESPACE);
#endif
    tre_regfree(&pattern);
  }

  it("matches embedded NUL and a terminal zero-width start at EOF") {
    salts_search_regex_pattern p = {0};
    salts_search_regex_span span = {0};
    const unsigned char bytes[] = {'a', 0, 'a'};
    check_equal(salts_search_regex_compile(&p, "a", 1u, 16u, 16u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_from(&p, bytes, sizeof(bytes),
               1u, &span), SALTS_SEARCH_REGEX_OK);
    check_equal(span.byte_start, UINT64_C(2));
    check_equal(span.byte_end, UINT64_C(3));
    salts_search_regex_destroy(&p);
    check_equal(salts_search_regex_compile(&p, "$", 1u, 16u, 16u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_from(&p, bytes, sizeof(bytes),
                sizeof(bytes), &span), SALTS_SEARCH_REGEX_OK);
    check_equal(span.byte_start, UINT64_C(3));
    check_equal(span.byte_end, UINT64_C(3));
    salts_search_regex_destroy(&p);
  }

  it("agrees with deterministic original-position differential for a|ab") {
    salts_search_regex_pattern p = {0};
    unsigned char subject[25] = {0};
    uint32_t seed = UINT32_C(0x512A2026);
    size_t trial, at;
    bool all_equal = true;
    check_equal(salts_search_regex_compile(&p, "a|ab", 4u, 16u, 32u),
                SALTS_SEARCH_REGEX_OK);
    for (trial = 0u; trial < 1024u && all_equal; ++trial) {
      const size_t length = trial % 25u;
      size_t j;
      for (j = 0u; j < length; ++j) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        subject[j] = (unsigned char)("abx"[seed % 3u]);
      }
      for (at = 0u; at <= length; ++at) {
        size_t expected = at;
        salts_search_regex_span match = {123, 456};
        salts_search_regex_status rc;
        while (expected < length && subject[expected] != 'a')
          ++expected;
        rc = salts_search_regex_find_from(&p, subject, length, at, &match);
        if (expected == length) {
          if (rc != SALTS_SEARCH_REGEX_NOT_FOUND ||
              match.byte_start != 0 || match.byte_end != 0)
            all_equal = false;
        } else {
          const size_t expected_end =
              expected + ((expected + 1u < length &&
                           subject[expected + 1u] == 'b') ? 2u : 1u);
          if (rc != SALTS_SEARCH_REGEX_OK ||
              match.byte_start != expected ||
              match.byte_end != expected_end)
            all_equal = false;
        }
        if (!all_equal)
          break;
      }
    }
    check_true(all_equal);
    check_equal(trial, 1024u);
    salts_search_regex_destroy(&p);
  }

  it("uses POSIX leftmost-longest, not rg leftmost-first, byte offsets") {
    salts_search_regex_pattern pattern = {0};
    salts_search_regex_span match = {SIZE_MAX, SIZE_MAX};
    check_equal(salts_search_regex_compile(&pattern, "a|ab", 4u, 16u, 256u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_first(&pattern, "ab", 2u, &match),
                SALTS_SEARCH_REGEX_OK);
    check_equal(match.byte_start, UINT64_C(0));
    check_equal(match.byte_end, UINT64_C(2));
    salts_search_regex_destroy(&pattern);
    salts_search_regex_destroy(&pattern);
  }

  it("finds a literal inside binary NUL data using explicit input length") {
    const unsigned char raw[] = {'x',0,'a','b',0,'z'};
    salts_search_regex_pattern pattern = {0};
    salts_search_regex_span match = {0};
    check_equal(salts_search_regex_compile(&pattern, "ab", 2u, 16u, 256u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_first(&pattern, raw, sizeof(raw), &match),
                SALTS_SEARCH_REGEX_OK);
    check_equal(match.byte_start, UINT64_C(2));
    check_equal(match.byte_end, UINT64_C(4));
    salts_search_regex_destroy(&pattern);
  }

  it("preserves regex anchoring and compiles no second regex provider") {
    salts_search_regex_pattern pattern = {0};
    salts_search_regex_span match = {99,99};
    check_equal(salts_search_regex_compile(&pattern, "^ab$", 4u, 16u, 256u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_first(&pattern, "ab", 2u, &match),
                SALTS_SEARCH_REGEX_OK);
    check_equal(match.byte_end, UINT64_C(2));
    check_equal(salts_search_regex_find_first(&pattern, "xab", 3u, &match),
                SALTS_SEARCH_REGEX_NOT_FOUND);
    check_equal(match.byte_start, UINT64_C(0));
    check_equal(match.byte_end, UINT64_C(0));
    salts_search_regex_destroy(&pattern);
  }

  it("rejects backreferences and TRE approximate matching before execution") {
    salts_search_regex_pattern pattern = {0};
    check_equal(salts_search_regex_compile(&pattern, "(a)\\1", 5u, 24u, 256u),
                SALTS_SEARCH_REGEX_UNSUPPORTED);
    check_true(!pattern.compiled);
    check_equal(salts_search_regex_compile(&pattern, "abc{~1}", 7u, 24u, 256u),
                SALTS_SEARCH_REGEX_UNSUPPORTED);
    check_true(!pattern.compiled);
  }

  it("rejects malformed patterns and finite input limits without truncation") {
    salts_search_regex_pattern pattern = {0};
    salts_search_regex_span match = {77,88};
    check_equal(salts_search_regex_compile(&pattern, "[", 1u, 16u, 256u),
                SALTS_SEARCH_REGEX_BAD_PATTERN);
    check_true(!pattern.compiled);
    check_equal(salts_search_regex_compile(&pattern, "abcdef", 6u, 5u, 256u),
                SALTS_SEARCH_REGEX_LIMIT);
    check_equal(salts_search_regex_compile(&pattern, "ab", 2u, 16u, 4u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_find_first(&pattern, "ababx", 5u, &match),
                SALTS_SEARCH_REGEX_LIMIT);
    check_equal(match.byte_start, UINT64_C(0));
    check_equal(match.byte_end, UINT64_C(0));
    salts_search_regex_destroy(&pattern);
  }

  it("has exact terminal states and validates null/empty input") {
    salts_search_regex_pattern pattern = {0};
    salts_search_regex_span match = {1,1};
    check_equal(salts_search_regex_compile(&pattern, "x", 1u, 16u, 16u),
                SALTS_SEARCH_REGEX_OK);
    check_equal(salts_search_regex_compile(&pattern, "x", 1u, 16u, 16u),
                SALTS_SEARCH_REGEX_INVALID);
    check_equal(salts_search_regex_find_first(&pattern, NULL, 0u, &match),
                SALTS_SEARCH_REGEX_NOT_FOUND);
    check_equal(salts_search_regex_find_first(&pattern, NULL, 1u, &match),
                SALTS_SEARCH_REGEX_INVALID);
    check_equal(salts_search_regex_find_first(&pattern, "x", 1u, NULL),
                SALTS_SEARCH_REGEX_INVALID);
    salts_search_regex_destroy(&pattern);
    check_equal(salts_search_regex_find_first(&pattern, "x", 1u, &match),
                SALTS_SEARCH_REGEX_INVALID);
    check_equal(salts_search_regex_compile(&pattern, NULL, 1u, 16u, 16u),
                SALTS_SEARCH_REGEX_INVALID);
  }
}
