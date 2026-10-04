#include "binary_tail_only_generated.h"
#include "tinytest.h"

#include <tstr.h>

#include <stdint.h>
#include <string.h>

spec("generated Binary provider accepts a VAR_DATA-only message") {
  it("round-trips a string without a fixed block") {
    static const uint8_t expected[] = {3u, 0u, 0u, 0u, 'c', 'a', 't'};
    DataBind *codec = NULL;
    TailOnly_t source;
    TailOnly_t decoded;
    uint8_t *wire = NULL;
    uint8_t fixed[sizeof(expected)] = {0};
    uint8_t too_small[sizeof(expected) - 1u];
    size_t wire_len = 0u;
    size_t fixed_len = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    TailOnly_init(&source);
    TailOnly_init(&decoded);
    source.text = tstr_dup("cat");
    check_not_null(source.text);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (source.text != NULL && codec != NULL) {
      check_equal(TailOnly_to_bin(
                      codec, &source, &wire, &wire_len, &error),
                  DATA_BIND_OK);
      check_equal(wire_len, sizeof(expected));
      if (wire != NULL && wire_len == sizeof(expected)) {
        check(memcmp(wire, expected, sizeof(expected)) == 0);
        check_equal(TailOnly_from_bin(
                        codec, &decoded, wire, wire_len, &error),
                    DATA_BIND_OK);
        check_not_null(decoded.text);
        if (decoded.text != NULL)
          check_equal(decoded.text, "cat");

        error = (DataBindError)DATA_BIND_ERROR_INIT;
        check_equal(TailOnly_from_bin(
                        codec, &decoded, wire, wire_len - 1u, &error),
                    DATA_BIND_ERR_PARSE);
        check_not_null(decoded.text);
        if (decoded.text != NULL)
          check_equal(decoded.text, "cat");
      }
      check_equal(TailOnly_to_bin_into(
                      codec, &source, fixed, sizeof(fixed),
                      &fixed_len, &error),
                  DATA_BIND_OK);
      check_equal(fixed_len, sizeof(expected));
      check(memcmp(fixed, expected, sizeof(expected)) == 0);

      memset(too_small, 0xa5, sizeof(too_small));
      fixed_len = 99u;
      error = (DataBindError)DATA_BIND_ERROR_INIT;
      check_equal(TailOnly_to_bin_into(
                      codec, &source, too_small, sizeof(too_small),
                      &fixed_len, &error),
                  DATA_BIND_ERR_LIMIT);
      check_equal(fixed_len, (size_t)0u);
      for (size_t i = 0u; i < sizeof(too_small); ++i)
        check_equal(too_small[i], (uint8_t)0xa5u);
    }

    data_bind_binary_free(wire);
    TailOnly_clear(&decoded);
    TailOnly_clear(&source);
    data_bind_free(codec);
  }
}
