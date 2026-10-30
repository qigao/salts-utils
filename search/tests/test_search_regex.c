#include "search_regex.h"
#include <tinytest.h>
#include <stdint.h>
#include <string.h>

spec("search A1 unique TRE exact Tagged NFA (C11)") {
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
