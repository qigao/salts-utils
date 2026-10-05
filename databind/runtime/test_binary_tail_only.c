#include "binary_tail_only_generated.h"
#include "tinytest.h"

#include <tstr.h>

#include <stdint.h>
#include <string.h>

static const char BINARY_TEXT[] = "cat";
static const char BINARY_PAYLOAD[] = "raw";

spec("generated and generic canonical Binary wire parity") {
  it("round-trips a string without a fixed block") {
    static const uint8_t expected[] = {3u, 0u, 0u, 0u, 'c', 'a', 't'};
    DataBind *codec = NULL;
    DataBindObject *dynamic = NULL;
    TailOnly_t source;
    TailOnly_t decoded;
    uint8_t *wire = NULL;
    uint8_t *dynamic_wire = NULL;
    uint8_t fixed[sizeof(expected)] = {0};
    uint8_t too_small[sizeof(expected) - 1u];
    size_t wire_len = 0u;
    size_t dynamic_len = 0u;
    size_t fixed_len = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    TailOnly_init(&source);
    TailOnly_init(&decoded);
    source.text = tstr_dup(BINARY_TEXT);
    check_not_null(source.text);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (source.text != NULL && codec != NULL) {
      static const char json[] = "{\"text\":\"cat\"}";
      check_equal(data_bind_object_from_json(codec, "TailOnly", json, sizeof(json) - 1u,
                  &dynamic, &error), DATA_BIND_OK);
      check_equal(data_bind_object_serialize_bin(codec, dynamic, &dynamic_wire,
                  &dynamic_len, &error), DATA_BIND_OK);
      check_equal(dynamic_len, sizeof(expected));
      check_equal(dynamic_wire, expected, sizeof(expected));
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
          check_equal(decoded.text, BINARY_TEXT);

        error = (DataBindError)DATA_BIND_ERROR_INIT;
        check_equal(TailOnly_from_bin(
                        codec, &decoded, wire, wire_len - 1u, &error),
                    DATA_BIND_ERR_PARSE);
        check_not_null(decoded.text);
        if (decoded.text != NULL)
          check_equal(decoded.text, BINARY_TEXT);
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
    data_bind_binary_free(dynamic_wire);
    data_bind_object_free(dynamic);
    TailOnly_clear(&decoded);
    TailOnly_clear(&source);
    data_bind_free(codec);
  }

  it("matches generic Binary bytes for unsigned scalars and independent owned tails") {
    static const char json[] =
        "{\"number\":18446744073709551615,\"delta\":-32768,"
        "\"text\":\"cat\",\"payload\":\"raw\"}";
    static const uint8_t expected[] = {
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0u, 0x80u,
        3u, 0u, 0u, 0u, 'c', 'a', 't', 3u, 0u, 0u, 0u, 'r', 'a', 'w'};
    DataBind *codec = NULL;
    DataBindObject *dynamic = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    MixedWire_t source;
    MixedWire_t decoded;
    uint8_t *dynamic_wire = NULL;
    uint8_t *generated_wire = NULL;
    uint8_t dynamic_fixed[sizeof(expected)] = {0};
    uint8_t generated_fixed[sizeof(expected)] = {0};
    size_t dynamic_len = 0u;
    size_t generated_len = 0u;
    size_t fixed_len = 0u;
    MixedWire_init(&source);
    MixedWire_init(&decoded);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(data_bind_object_from_json(codec, "MixedWire", json, sizeof(json) - 1u,
                &dynamic, &error), DATA_BIND_OK);
    check_equal(MixedWire_from_json(codec, &source, json, sizeof(json) - 1u, &error), DATA_BIND_OK);
    check_equal(data_bind_object_serialize_bin(codec, dynamic, &dynamic_wire,
                &dynamic_len, &error), DATA_BIND_OK);
    check_equal(MixedWire_to_bin(codec, &source, &generated_wire, &generated_len, &error), DATA_BIND_OK);
    check_equal(dynamic_len, sizeof(expected));
    check_equal(generated_len, dynamic_len);
    check_equal(dynamic_wire, expected, sizeof(expected));
    check_equal(generated_wire, dynamic_wire, dynamic_len);
    check_equal(data_bind_object_serialize_bin_into(codec, dynamic, dynamic_fixed,
                sizeof(dynamic_fixed), &fixed_len, &error), DATA_BIND_OK);
    check_equal(fixed_len, sizeof(expected));
    check_equal(MixedWire_to_bin_into(codec, &source, generated_fixed,
                sizeof(generated_fixed), &fixed_len, &error), DATA_BIND_OK);
    check_equal(fixed_len, sizeof(expected));
    check_equal(generated_fixed, dynamic_fixed, sizeof(expected));
    check_equal(MixedWire_from_bin(codec, &decoded, dynamic_wire, dynamic_len, &error), DATA_BIND_OK);
    data_bind_object_free(dynamic);
    dynamic = NULL;
    data_bind_binary_free(dynamic_wire);
    dynamic_wire = NULL;
    MixedWire_clear(&source);
    memset(generated_wire, 0, generated_len);
    check_equal(decoded.number, UINT64_MAX);
    check_equal(decoded.delta, INT16_MIN);
    check_equal(decoded.text, BINARY_TEXT);
    check_equal(stl_byte_buffer_size(&decoded.payload), sizeof(BINARY_PAYLOAD) - 1u);
    check_equal(stl_byte_buffer_data_const(&decoded.payload), BINARY_PAYLOAD, sizeof(BINARY_PAYLOAD) - 1u);
    data_bind_binary_free(generated_wire);
    MixedWire_clear(&decoded);
    data_bind_free(codec);
  }
}
