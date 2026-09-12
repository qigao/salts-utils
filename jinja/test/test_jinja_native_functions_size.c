#include "test_jinja_native_functions_support.h"
spec("Jinja string size admission") {
  it("rejects unrepresentable lengths before allocating a string") {
    static const struct { size_t used, incoming; } cases[] = {
      {0u, SIZE_MAX}, {SIZE_MAX, 0u}, {0u, UINT32_MAX},
      {UINT32_MAX, 1u}, {SIZE_MAX / 2u, 1u}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      info("used=%zu incoming=%zu", cases[i].used, cases[i].incoming);
      check_equal(jinja_cmeta_string_append_fits(cases[i].used, cases[i].incoming, SIZE_MAX), 0);
    }
  }

  it("admits the safe storage boundary and rejects one byte beyond it") {
    const size_t boundary = SIZE_MAX == UINT32_MAX ? 1073741823u : 2147483647u;
    check_equal(jinja_cmeta_string_append_fits(0u, boundary, SIZE_MAX), 1);
    check_equal(jinja_cmeta_string_append_fits(boundary - 1u, 1u, SIZE_MAX), 1);
    check_equal(jinja_cmeta_string_append_fits(boundary, 0u, SIZE_MAX), 1);
    check_equal(jinja_cmeta_string_append_fits(boundary, 1u, SIZE_MAX), 0);
    check_equal(jinja_cmeta_string_append_fits(0u, boundary + 1u, SIZE_MAX), 0);
  }

  it("keeps the caller byte limit independent of the storage bound") {
    check_equal(jinja_cmeta_string_append_fits(0u, 0u, 0u), 1);
    check_equal(jinja_cmeta_string_append_fits(3u, 4u, 7u), 1);
    check_equal(jinja_cmeta_string_append_fits(3u, 5u, 7u), 0);
    check_equal(jinja_cmeta_string_append_fits(8u, 0u, 7u), 0);
    check_equal(jinja_cmeta_string_append_fits(1u, SIZE_MAX, SIZE_MAX), 0);
  }
}
