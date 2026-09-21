#include "salts_unicode.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

spec("salts_unicode Unicode 17 line-break contract") {
  it("exposes Unicode 17 line-break classes including HH") {
    salts_unicode_line_break value = SALTS_UNICODE_LINE_BREAK_AL;

    check_equal(salts_unicode_line_break_class(0x000Au, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_LINE_BREAK_LF);
    check_equal(salts_unicode_line_break_class(0x000Du, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_LINE_BREAK_CR);
    check_equal(salts_unicode_line_break_class(0x0020u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_LINE_BREAK_SP);
    check_equal(salts_unicode_line_break_class(0x2060u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_LINE_BREAK_WJ);

    /* Unicode 17 splits Unambiguous_Hyphen (HH) from BA. */
    check_equal(salts_unicode_line_break_class(0x2010u, &value), SALTS_UNICODE_OK);
    check_equal(value, SALTS_UNICODE_LINE_BREAK_HH);

    value = SALTS_UNICODE_LINE_BREAK_BK;
    check_equal(salts_unicode_line_break_class(0xD800u, &value),
                SALTS_UNICODE_ERR_INVALID_ARGUMENT);
    check_equal(value, SALTS_UNICODE_LINE_BREAK_BK);
  }

  it("distinguishes soft opportunities from mandatory breaks") {
    static const unsigned char bytes[] = {'A', ' ', 'B', '\n', 'C'};
    const vstr input = vstr_from_buf((const char *)bytes, sizeof(bytes));
    size_t cursor = 0u;
    size_t break_offset = 999u;
    salts_unicode_line_break_opportunity opportunity =
        SALTS_UNICODE_LINE_BREAK_MANDATORY;

    check_equal(salts_unicode_line_break_next(
                    input, &cursor, &break_offset, &opportunity),
                SALTS_UNICODE_OK);
    check_equal(break_offset, 2u);
    check_equal(opportunity, SALTS_UNICODE_LINE_BREAK_ALLOWED);

    check_equal(salts_unicode_line_break_next(
                    input, &cursor, &break_offset, &opportunity),
                SALTS_UNICODE_OK);
    check_equal(break_offset, 4u);
    check_equal(opportunity, SALTS_UNICODE_LINE_BREAK_MANDATORY);
  }

  it("keeps outputs transactional on malformed UTF-8") {
    static const unsigned char malformed[] = {'A', 0xFFu};
    const vstr input = vstr_from_buf((const char *)malformed, sizeof(malformed));
    size_t cursor = 0u;
    size_t break_offset = 77u;
    salts_unicode_line_break_opportunity opportunity =
        SALTS_UNICODE_LINE_BREAK_ALLOWED;

    check_equal(salts_unicode_line_break_next(
                    input, &cursor, &break_offset, &opportunity),
                SALTS_UNICODE_ERR_INVALID_UTF8);
    check_equal(cursor, 0u);
    check_equal(break_offset, 77u);
    check_equal(opportunity, SALTS_UNICODE_LINE_BREAK_ALLOWED);
  }
}
