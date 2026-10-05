#include "binary_tail_only_generated.h"
#include "binary_array_big_generated.h"
#include "data_bind_binary_layout.h"
#include "tinytest.h"

#include <tstr.h>

#include <stdint.h>
#include <string.h>

static const char BINARY_TEXT[] = "cat";
static const char BINARY_PAYLOAD[] = "raw";
enum { BINARY_ARRAY_GENERIC_BUFFER_BYTES = 64u };

static int binary_array_check_generic(
    DataBind *codec, const char *type, const char *json,
    const uint8_t *expected, size_t expected_size) {
  DataBindObject *object = NULL;
  DataBindObject *decoded = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  uint8_t *wire = NULL;
  size_t size = 0u;
  uint8_t bounded[BINARY_ARRAY_GENERIC_BUFFER_BYTES];
  size_t bounded_size = 0u;
  char *source_json = NULL, *decoded_json = NULL;
  size_t source_json_size = 0u, decoded_json_size = 0u;
  int matches;
  status = data_bind_object_from_json(codec, type, json, strlen(json), &object, &error);
  matches = status == DATA_BIND_OK && object != NULL;
  check_equal_warn(status, DATA_BIND_OK);
  if (status == DATA_BIND_OK) {
    check_not_null_warn(object);
  }
  if (status == DATA_BIND_OK && object != NULL) {
    status = data_bind_object_serialize_bin(codec, object, &wire, &size, &error);
    /* Warnings retain diagnostics; the caller asserts the accumulated result
     * after native lifecycle checks and cleanup finish. */
    check_equal_warn(status, DATA_BIND_OK);
    matches = status == DATA_BIND_OK && wire != NULL && size == expected_size;
    if (status == DATA_BIND_OK) {
      check_equal_warn(size, expected_size);
      check_not_null_warn(wire);
      if (wire != NULL && size == expected_size) {
        matches = memcmp(wire, expected, size) == 0;
        check_equal_warn(wire, expected, size);
      }
    }
    check_less_equal_warn(expected_size, sizeof(bounded));
    if (expected_size <= sizeof(bounded)) {
      status = data_bind_object_serialize_bin_into(
          codec, object, bounded, expected_size, &bounded_size, &error);
      check_equal_warn(status, DATA_BIND_OK);
      matches &= status == DATA_BIND_OK && bounded_size == expected_size;
      if (status == DATA_BIND_OK) check_equal_warn(bounded_size, expected_size);
      if (status == DATA_BIND_OK && bounded_size == expected_size) {
        matches &= memcmp(bounded, expected, expected_size) == 0;
        check_equal_warn(bounded, expected, expected_size);
      }
    } else {
      matches = 0;
    }
  }
  data_bind_binary_free(wire);
  wire = NULL;
  size = 0u;
  status = data_bind_object_from_bin(codec, type, expected, expected_size, &decoded, &error);
  check_equal_warn(status, DATA_BIND_OK);
  matches &= status == DATA_BIND_OK && decoded != NULL;
  if (status == DATA_BIND_OK && decoded != NULL) {
    status = data_bind_object_serialize_bin(codec, decoded, &wire, &size, &error);
    check_equal_warn(status, DATA_BIND_OK);
    matches &= status == DATA_BIND_OK && wire != NULL && size == expected_size;
    if (status == DATA_BIND_OK && wire != NULL && size == expected_size) {
      matches &= memcmp(wire, expected, size) == 0;
      check_equal_warn(wire, expected, size);
    }
    if (object != NULL) {
      DataBindStatus source_status = data_bind_object_serialize_json(
          codec, object, &source_json, &source_json_size, &error);
      DataBindStatus decoded_status = data_bind_object_serialize_json(
          codec, decoded, &decoded_json, &decoded_json_size, &error);
      check_equal_warn(source_status, DATA_BIND_OK);
      check_equal_warn(decoded_status, DATA_BIND_OK);
      matches &= source_status == DATA_BIND_OK && decoded_status == DATA_BIND_OK &&
                 source_json != NULL && decoded_json != NULL && source_json_size == decoded_json_size;
      if (source_status == DATA_BIND_OK && decoded_status == DATA_BIND_OK &&
          source_json != NULL && decoded_json != NULL) {
        /* Symmetric endian mistakes can round-trip identical bytes. Compare
         * logical output independently against the object bound from JSON. */
        matches &= strcmp(source_json, decoded_json) == 0;
        check_equal_warn(decoded_json, source_json);
      }
    }
  }
  data_bind_serialized_free(source_json);
  data_bind_serialized_free(decoded_json);
  data_bind_binary_free(wire);
  data_bind_object_free(decoded);
  data_bind_object_free(object);
  return matches;
}

spec("generated and generic canonical Binary wire parity") {
  group("unsupported nested wire shape") {
    static DataBind *codec;
    static DataBindError error;
    static UnavailableNested_t source;
    static DataBindValue *value;
    static uint8_t *wire;
    static size_t wire_len;
    before_each() {
      codec = NULL;
      value = NULL;
      wire = NULL;
      wire_len = 0u;
      error = (DataBindError)DATA_BIND_ERROR_INIT;
      UnavailableNested_init(&source);
      check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
      static const char child_json[] = "{\"text\":\"stable\"}";
      check_equal(TailOnly_from_json(codec, &source.child, child_json, sizeof(child_json) - 1u, &error), DATA_BIND_OK);
    }
    after_each() {
      data_bind_binary_free(wire);
      data_bind_value_free(value);
      UnavailableNested_clear(&source);
      data_bind_free(codec);
    }
    it("rejects generated and generic Binary decode while preserving native owners") {
      static const uint8_t payload[] = {0u};
      tstr owner = source.child.text;
      check_not_null(owner);
      check_equal(UnavailableNested_to_bin(codec, &source, &wire, &wire_len, &error), DATA_BIND_ERR_SCHEMA);
      check_null(wire);
      check_equal(wire_len, 0u);
      check_equal(UnavailableNested_from_bin(codec, &source, payload, sizeof(payload), &error), DATA_BIND_ERR_SCHEMA);
      check(source.child.text == owner);
      check_equal(source.child.text, "stable");
      check_equal(data_bind_parse(codec, "UnavailableNested", payload, sizeof(payload),
                                  &value, &error), DATA_BIND_ERR_SCHEMA);
      check_null(value);
    }
  }

  it("uses canonical owned list set and map storage for counted wire") {
    static const char json[] =
        "{\"prefix\":4660,\"values\":[258,515],\"tags\":[\"cat\"],"
        "\"labels\":{\"k\":\"raw\"},\"last\":17185}";
    static const uint8_t expected[] = {
        0x34u, 0x12u,
        2u, 0u, 0u, 0u, 2u, 1u, 3u, 2u,
        1u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 'c', 'a', 't',
        1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 'k',
        3u, 0u, 0u, 0u, 'r', 'a', 'w', 0x21u, 0x43u};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    CountedWire_t source, decoded;
    uint8_t *wire = NULL;
    uint8_t bounded[sizeof(expected)];
    uint8_t unchanged[sizeof(expected)];
    size_t size = 0u, bounded_size = 0u;
    int generic_matches;
    CountedWire_init(&source);
    CountedWire_init(&decoded);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    generic_matches = binary_array_check_generic(codec, "CountedWire", json, expected, sizeof(expected));
    check(CountedWire_from_json(codec, &source, json, sizeof(json) - 1u, &error) == DATA_BIND_OK,
          "%s [%s]", error.message, error.path);
    check_equal(CountedWire_to_bin(codec, &source, &wire, &size, &error), DATA_BIND_OK);
    check_equal(size, sizeof(expected));
    if (wire != NULL && size == sizeof(expected)) check_equal(wire, expected, size);
    check_equal(CountedWire_to_bin_into(codec, &source, bounded, sizeof(bounded), &bounded_size, &error), DATA_BIND_OK);
    check_equal(bounded_size, sizeof(expected));
    check_equal(bounded, expected, sizeof(expected));
    memcpy(unchanged, bounded, sizeof(unchanged));
    check_equal(CountedWire_to_bin_into(codec, &source, bounded, sizeof(bounded) - 1u, &bounded_size, &error), DATA_BIND_ERR_LIMIT);
    check_equal(bounded_size, sizeof(expected));
    check_equal(bounded, unchanged, sizeof(bounded));
    check_equal(CountedWire_from_bin(codec, &decoded, expected, sizeof(expected), &error), DATA_BIND_OK);
    check_equal(CountedWire_values_vec_t_size(&decoded.values), (size_t)2u);
    check_equal(CountedWire_tags_set_t_size(&decoded.tags), (size_t)1u);
    check_equal(CountedWire_labels_map_t_size(&decoded.labels), (size_t)1u);
    {
      const CountedWire_t previous = decoded;
      check_equal(CountedWire_from_bin(codec, &decoded, expected, sizeof(expected) - 1u, &error), DATA_BIND_ERR_PARSE);
      check_equal(&decoded, &previous, sizeof(previous));
    }
    CountedWire_clear(&source);
    data_bind_binary_free(wire);
    wire = NULL;
    check_equal(CountedWire_to_bin(codec, &decoded, &wire, &size, &error), DATA_BIND_OK);
    check_equal(size, sizeof(expected));
    if (wire != NULL && size == sizeof(expected)) check_equal(wire, expected, size);
    CountedWire_clear(&decoded);
    CountedWire_clear(&decoded);
    data_bind_binary_free(wire);
    data_bind_free(codec);
    check_true(generic_matches);
  }

  it("copies counted fixed records through their canonical element provider") {
    static const char json[] =
        "{\"records\":[{\"point\":{\"delta\":-2,\"number\":72623859790382856},"
        "\"code\":4660}],\"last\":17185}";
    static const uint8_t expected[] = {
        1u, 0u, 0u, 0u, 0xfeu, 0xffu,
        8u, 7u, 6u, 5u, 4u, 3u, 2u, 1u,
        0x34u, 0x12u, 0x21u, 0x43u};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    CountedRecordWire_t source, decoded;
    uint8_t *wire = NULL;
    size_t size = 0u;
    int generic_matches;
    CountedRecordWire_init(&source);
    CountedRecordWire_init(&decoded);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    generic_matches = binary_array_check_generic(codec, "CountedRecordWire", json, expected, sizeof(expected));
    check_equal(CountedRecordWire_from_json(codec, &source, json, sizeof(json) - 1u, &error), DATA_BIND_OK);
    check_equal(CountedRecordWire_to_bin(codec, &source, &wire, &size, &error), DATA_BIND_OK);
    check_equal(size, sizeof(expected));
    if (wire != NULL && size == sizeof(expected)) check_equal(wire, expected, size);
    check_equal(CountedRecordWire_from_bin(codec, &decoded, expected, sizeof(expected), &error), DATA_BIND_OK);
    check_equal(CountedRecordWire_records_vec_t_size(&decoded.records), (size_t)1u);
    {
      const CountedRecordWire_t previous = decoded;
      check_equal(CountedRecordWire_from_bin(codec, &decoded, expected, sizeof(expected) - 1u, &error), DATA_BIND_ERR_PARSE);
      check_equal(&decoded, &previous, sizeof(previous));
    }
    CountedRecordWire_clear(&source);
    data_bind_binary_free(wire);
    wire = NULL;
    check_equal(CountedRecordWire_to_bin(codec, &decoded, &wire, &size, &error), DATA_BIND_OK);
    check_equal(size, sizeof(expected));
    if (wire != NULL && size == sizeof(expected)) check_equal(wire, expected, size);
    CountedRecordWire_clear(&decoded);
    CountedRecordWire_clear(&decoded);
    data_bind_binary_free(wire);
    data_bind_free(codec);
    check_true(generic_matches);
  }

  it("preserves scalar array state and exact little-endian historical wire") {
    static const char *const json[] = {
        "{\"ready\":[true,false],\"deltas\":[-2,4660,7],\"ratios\":[1.5,-2.0]}",
        "{\"ready\":[true,false],\"deltas\":[-2,4660,7],\"ratios\":[1.5,-2.0],\"samples\":null}",
        "{\"ready\":[true,false],\"deltas\":[-2,4660,7],\"ratios\":[1.5,-2.0],\"samples\":[1,515,65535]}"};
    static const uint8_t value_wire[] = {
        1u, 0u, 1u, 0u, 0xfeu, 0xffu, 0x34u, 0x12u, 7u, 0u,
        0u, 0u, 0xc0u, 0x3fu, 0u, 0u, 0u, 0xc0u, 1u, 0u, 3u, 2u, 0xffu, 0xffu};
    enum { SAMPLE_OFFSET = 18u };
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t scenario;
    int generic_matches = 1;
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    for (scenario = 0u; scenario < sizeof(json) / sizeof(json[0]); ++scenario) {
      ScalarArrayWire_t source, decoded;
      uint8_t expected[sizeof(value_wire)];
      uint8_t bounded[sizeof(value_wire)] = {0};
      uint8_t *wire = NULL;
      size_t size = 0u, bounded_size = 0u;
      ScalarArrayWire_init(&source);
      ScalarArrayWire_init(&decoded);
      memcpy(expected, value_wire, sizeof(expected));
      expected[0] = scenario == 0u ? 0u : 1u;
      expected[1] = scenario == 1u ? 1u : 0u;
      if (scenario != 2u) memset(expected + SAMPLE_OFFSET, 0, sizeof(expected) - SAMPLE_OFFSET);
      check_equal(ScalarArrayWire_from_json(codec, &source, json[scenario], strlen(json[scenario]), &error), DATA_BIND_OK);
      check_equal(ScalarArrayWire_to_bin(codec, &source, &wire, &size, &error), DATA_BIND_OK);
      check_equal(size, sizeof(expected));
      if (wire != NULL && size == sizeof(expected)) check_equal(wire, expected, size);
      generic_matches &= binary_array_check_generic(codec, "ScalarArrayWire", json[scenario], expected, sizeof(expected));
      check_equal(ScalarArrayWire_to_bin_into(codec, &source, bounded, sizeof(bounded), &bounded_size, &error), DATA_BIND_OK);
      check_equal(bounded_size, sizeof(expected));
      check_equal(bounded, expected, sizeof(expected));
      check_equal(ScalarArrayWire_from_bin(codec, &decoded, expected, sizeof(expected), &error), DATA_BIND_OK);
      check_equal(decoded._presence[0], expected[0]);
      check_equal(decoded._nulls[0], expected[1]);
      check_equal(decoded.ready[0], true);
      check_equal(decoded.deltas[0], (int16_t)-2);
      check_equal(decoded.ratios[1], -2.0f);
      check_equal(decoded.samples[1], scenario == 2u ? (uint16_t)515u : (uint16_t)0u);
      {
        const ScalarArrayWire_t previous = decoded;
        static const char short_array[] = "{\"ready\":[true],\"deltas\":[1,2,3],\"ratios\":[0,0]}";
        check(ScalarArrayWire_from_json(codec, &decoded, short_array, sizeof(short_array) - 1u, &error) != DATA_BIND_OK);
        check_equal(&decoded, &previous, sizeof(previous));
        check_equal(ScalarArrayWire_from_bin(codec, &decoded, expected, sizeof(expected) - 1u, &error), DATA_BIND_ERR_PARSE);
        check_equal(&decoded, &previous, sizeof(previous));
      }
      data_bind_binary_free(wire);
      ScalarArrayWire_clear(&source);
      ScalarArrayWire_clear(&decoded);
      ScalarArrayWire_clear(&decoded);
    }
    data_bind_free(codec);
    check_true(generic_matches);
  }

  it("matches big-endian scalar and enum arrays with generic Binary") {
    static const char json[] =
        "{\"ready\":[true,false],\"deltas\":[-2,4660,7],\"ratios\":[1.5,-2.0],\"samples\":[1,515,65535],\"codes\":[-2,1]}";
    static const uint8_t expected[] = {
        1u, 0u, 1u, 0u, 0xffu, 0xfeu, 0x12u, 0x34u, 0u, 7u,
        0x3fu, 0xc0u, 0u, 0u, 0xc0u, 0u, 0u, 0u, 0u, 1u, 2u, 3u, 0xffu, 0xffu,
        0xffu, 0xfeu, 0u, 1u};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    BigScalarArrayWire_t source, decoded;
    uint8_t *wire = NULL;
    size_t size = 0u;
    int generic_matches;
    BigScalarArrayWire_init(&source);
    BigScalarArrayWire_init(&decoded);
    check_equal(BigArray_codec_create(&codec, &error), DATA_BIND_OK);
    {
      DataBindStatus status = BigScalarArrayWire_from_json(
          codec, &source, json, sizeof(json) - 1u, &error);
      check(status == DATA_BIND_OK, "Big array decode (%d) %s: %s",
            (int)status, error.path, error.message);
    }
    {
      DataBindStatus status = BigScalarArrayWire_to_bin(codec, &source, &wire, &size, &error);
      check(status == DATA_BIND_OK, "Big array encode (%d) %s: %s",
            (int)status, error.path, error.message);
    }
    check_equal(size, sizeof(expected));
    if (wire != NULL && size == sizeof(expected)) check_equal(wire, expected, size);
    generic_matches = binary_array_check_generic(codec, "BigScalarArrayWire", json, expected, sizeof(expected));
    check_equal(BigScalarArrayWire_from_bin(codec, &decoded, expected, sizeof(expected), &error), DATA_BIND_OK);
    check_equal(decoded.codes[0], (BigArrayCode_t)BigArrayCode_Negative);
    check_equal(decoded.samples[1], (uint16_t)515u);
    check_equal(decoded.deltas[0], (int16_t)-2);
    data_bind_binary_free(wire);
    BigScalarArrayWire_clear(&source);
    BigScalarArrayWire_clear(&decoded);
    data_bind_free(codec);
    check_true(generic_matches);
  }

  it("round trips recursive record arrays and keeps their native C array ABI") {
    static const char json[] =
        "{\"records\":[{\"digest\":\"abcdef\",\"code\":4660},{\"digest\":\"ghijkl\",\"code\":7}],\"blocks\":[{\"values\":[1,515]},{\"values\":[3,4]}],\"text\":\"cat\"}";
    static const uint8_t expected[] = {
        1u, 0u, 'a','b','c','d','e','f', 0x34u,0x12u, 'g','h','i','j','k','l',7u,0u,
        1u,0u,3u,2u, 3u,0u,4u,0u, 3u,0u,0u,0u,'c','a','t'};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    RecordArrayWire_t source, copied, decoded;
    uint8_t *wire = NULL;
    size_t size = 0u;
    int generic_matches;
    RecordArrayWire_init(&source);
    RecordArrayWire_init(&copied);
    RecordArrayWire_init(&decoded);
    check_equal(sizeof(source.records), (size_t)2u * sizeof(FixedByteRecord_t));
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    /* Text strings have no BYTES tokens; fixed byte members are decoded from
     * Binary while the dynamic route independently verifies the old wire. */
    generic_matches = binary_array_check_generic(codec, "RecordArrayWire", json, expected, sizeof(expected));
    check_equal(RecordArrayWire_from_bin(codec, &source, expected, sizeof(expected), &error), DATA_BIND_OK);
    check_equal(RecordArrayWire_to_bin(codec, &source, &wire, &size, &error), DATA_BIND_OK);
    check_equal(size, sizeof(expected));
    if (wire != NULL && size == sizeof(expected)) check_equal(wire, expected, size);
    check_equal(RecordArrayWire_from_bin(codec, &copied, expected, sizeof(expected), &error), DATA_BIND_OK);
    check_true(copied.text != source.text);
    check_equal(copied.blocks[0].values[1], (uint16_t)515u);
    check_equal(copied.records[1].digest, expected + 10u, sizeof(copied.records[1].digest));
    {
      tstr previous_text = copied.text;
      uint8_t previous_records[sizeof(copied.records)];
      memcpy(previous_records, copied.records, sizeof(previous_records));
      check_equal(RecordArrayWire_from_bin(codec, &copied, expected, sizeof(expected) - 1u, &error), DATA_BIND_ERR_PARSE);
      check_true(copied.text == previous_text);
      check_equal(copied.records, previous_records, sizeof(previous_records));
    }
    RecordArrayWire_clear(&source);
    check_equal(copied.text, BINARY_TEXT);
    check_equal(RecordArrayWire_from_bin(codec, &decoded, expected, sizeof(expected), &error), DATA_BIND_OK);
    data_bind_binary_free(wire);
    RecordArrayWire_clear(&copied);
    RecordArrayWire_clear(&decoded);
    RecordArrayWire_clear(&decoded);
    data_bind_free(codec);
    check_true(generic_matches);
  }

  it("executes big-endian nested arrays and fixed arrays inside GROUP entries") {
    static const char big_json[] = "{\"entries\":[{\"code\":4660,\"tags\":[1,2]},{\"code\":43981,\"tags\":[3,4]}]}";
    static const uint8_t big_expected[] = {0x12u,0x34u,1u,2u,0xabu,0xcdu,3u,4u};
    static const char group_json[] = "{\"entries\":[{\"values\":[1,515]},{\"values\":[3,4]}],\"text\":\"cat\"}";
    static const uint8_t group_expected[] = {4u,0u,2u,0u,1u,0u,3u,2u,3u,0u,4u,0u,3u,0u,0u,0u,'c','a','t'};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    BigRecordArrayWire_t big;
    ArrayGroupWire_t group;
    uint8_t *wire = NULL;
    size_t size = 0u;
    int generic_matches;
    BigRecordArrayWire_init(&big);
    ArrayGroupWire_init(&group);
    check_equal(BigArray_codec_create(&codec, &error), DATA_BIND_OK);
    generic_matches = binary_array_check_generic(codec, "BigRecordArrayWire", big_json, big_expected, sizeof(big_expected));
    check_equal(BigRecordArrayWire_from_json(codec, &big, big_json, sizeof(big_json) - 1u, &error), DATA_BIND_OK);
    check_equal(BigRecordArrayWire_to_bin(codec, &big, &wire, &size, &error), DATA_BIND_OK);
    check_equal(size, sizeof(big_expected));
    if (wire != NULL && size == sizeof(big_expected)) check_equal(wire, big_expected, size);
    data_bind_binary_free(wire);
    wire = NULL;
    data_bind_free(codec);
    codec = NULL;
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    generic_matches &= binary_array_check_generic(codec, "ArrayGroupWire", group_json, group_expected, sizeof(group_expected));
    check_equal(ArrayGroupWire_from_json(codec, &group, group_json, sizeof(group_json) - 1u, &error), DATA_BIND_OK);
    check_equal(ArrayGroupWire_to_bin(codec, &group, &wire, &size, &error), DATA_BIND_OK);
    check_equal(size, sizeof(group_expected));
    if (wire != NULL && size == sizeof(group_expected)) check_equal(wire, group_expected, size);
    ArrayGroupWire_clear(&group);
    check_equal(ArrayGroupWire_from_bin(codec, &group, group_expected, sizeof(group_expected), &error), DATA_BIND_OK);
    check_equal(ArrayGroupWire_entries_vec_t_size(&group.entries), (size_t)2u);
    data_bind_binary_free(wire);
    ArrayGroupWire_clear(&group);
    BigRecordArrayWire_clear(&big);
    data_bind_free(codec);
    check_true(generic_matches);
  }

  it("matches fixed inline byte records with generic wire and preserves destination on failure") {
    static const char json[] = "{\"record\":{\"digest\":\"abcdef\",\"code\":4660},\"text\":\"cat\"}";
    static const uint8_t expected[] = {'a', 'b', 'c', 'd', 'e', 'f', 0x34u, 0x12u,
                                     3u, 0u, 0u, 0u, 'c', 'a', 't'};
    DataBind *codec = NULL;
    DataBindObject *dynamic = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    FixedByteWire_t source;
    FixedByteWire_t decoded;
    FixedByteWire_t copied;
    uint8_t *generic_wire = NULL;
    uint8_t *wire = NULL;
    uint8_t bounded[sizeof(expected)] = {0};
    uint8_t too_small[sizeof(expected) - 1u];
    size_t generic_len = 0u, wire_len = 0u, bounded_len = 0u;
    FixedByteWire_init(&source);
    FixedByteWire_init(&decoded);
    FixedByteWire_init(&copied);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(data_bind_object_from_json(codec, "FixedByteWire", json, sizeof(json) - 1u,
                &dynamic, &error), DATA_BIND_OK);
    check_equal(FixedByteWire_from_json(codec, &source, json, sizeof(json) - 1u, &error), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(FixedByteWire_from_bin(codec, &source, expected, sizeof(expected), &error), DATA_BIND_OK);
    check_equal(data_bind_object_serialize_bin(codec, dynamic, &generic_wire, &generic_len, &error), DATA_BIND_OK);
    check_equal(FixedByteWire_to_bin(codec, &source, &wire, &wire_len, &error), DATA_BIND_OK);
    check_equal(wire_len, sizeof(expected));
    check_equal(generic_len, wire_len);
    check_equal(wire, expected, sizeof(expected));
    check_equal(generic_wire, wire, wire_len);
    check_equal(FixedByteWire_to_bin_into(codec, &source, bounded, sizeof(bounded), &bounded_len, &error), DATA_BIND_OK);
    check_equal(bounded_len, sizeof(expected));
    check_equal(bounded, expected, sizeof(expected));
    memset(too_small, 0xa5, sizeof(too_small));
    check_equal(FixedByteWire_to_bin_into(codec, &source, too_small, sizeof(too_small), &bounded_len, &error), DATA_BIND_ERR_LIMIT);
    check_equal(bounded_len, sizeof(expected));
    for (size_t i = 0u; i < sizeof(too_small); ++i)
      check_equal(too_small[i], (uint8_t)0xa5u);
    check_equal(FixedByteWire_from_bin(codec, &decoded, wire, wire_len, &error), DATA_BIND_OK);
    check_equal(cmeta_data_value_copy(&FixedByteWire_CMETA_DATA, &copied, &decoded), CMETA_OK);
    check_true(copied.text != decoded.text);
    check_equal(copied.record.digest, decoded.record.digest, sizeof(decoded.record.digest));
    {
      tstr retained = decoded.text;
      check_equal(FixedByteWire_from_bin(codec, &decoded, wire, wire_len - 1u, &error), DATA_BIND_ERR_PARSE);
      check_true(decoded.text == retained);
      check_equal(decoded.record.digest, expected, sizeof(decoded.record.digest));
    }
    memset(wire, 0, wire_len);
    FixedByteWire_clear(&source);
    data_bind_object_free(dynamic);
    data_bind_binary_free(generic_wire);
    data_bind_binary_free(wire);
    check_equal(decoded.record.digest, expected, sizeof(decoded.record.digest));
    check_equal(decoded.text, BINARY_TEXT);
    FixedByteWire_clear(&decoded);
    check_equal(copied.record.digest, expected, sizeof(copied.record.digest));
    check_equal(copied.text, BINARY_TEXT);
    FixedByteWire_clear(&copied);
    FixedByteWire_clear(&copied);
    data_bind_free(codec);
  }

  it("keeps ABSENT NULL VALUE fixed bytes distinct and restores owners after rejection") {
    enum { FIXED_BYTES_WIRE_SIZE = 2u + 6u + sizeof(uint32_t) + 3u };
    static const char *const json[] = {"{\"text\":\"cat\"}",
        "{\"digest\":null,\"text\":\"cat\"}", "{\"digest\":\"abcdef\",\"text\":\"cat\"}"};
    static const uint8_t expected[][FIXED_BYTES_WIRE_SIZE] = {
        {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 'c', 'a', 't'},
        {1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 'c', 'a', 't'},
        {1u, 0u, 'a', 'b', 'c', 'd', 'e', 'f', 3u, 0u, 0u, 0u, 'c', 'a', 't'}};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_null(NullableFixedByteWire_CMETA_TYPE.traits);
    for (size_t scenario = 0u; scenario < sizeof(json) / sizeof(json[0]); ++scenario) {
      NullableFixedByteWire_t source;
      NullableFixedByteWire_t decoded;
      uint8_t *wire = NULL;
      size_t wire_len = 0u, bounded_len = 0u;
      uint8_t bounded[sizeof(expected[0])] = {0};
      NullableFixedByteWire_init(&source);
      NullableFixedByteWire_init(&decoded);
      check_equal(NullableFixedByteWire_from_json(codec, &source, json[scenario], strlen(json[scenario]), &error), DATA_BIND_OK);
      check_equal(NullableFixedByteWire_to_bin(codec, &source, &wire, &wire_len, &error), DATA_BIND_OK);
      check_equal(wire_len, sizeof(expected[scenario]));
      check_equal(wire, expected[scenario], wire_len);
      check_equal(NullableFixedByteWire_to_bin_into(codec, &source, bounded, sizeof(bounded), &bounded_len, &error), DATA_BIND_OK);
      check_equal(bounded_len, wire_len);
      check_equal(bounded, wire, wire_len);
      check_equal(NullableFixedByteWire_from_bin(codec, &decoded, wire, wire_len, &error), DATA_BIND_OK);
      check_equal(decoded._presence[0], expected[scenario][0]);
      check_equal(decoded._nulls[0], expected[scenario][1]);
      check_equal(decoded.digest, expected[scenario] + 2u, sizeof(decoded.digest));
      {
        tstr retained = decoded.text;
        static const char short_value[] = "{\"digest\":\"a\",\"text\":\"replace\"}";
        bounded[0] = 0u;
        bounded[1] = 1u;
        check_equal(NullableFixedByteWire_from_bin(codec, &decoded, bounded, sizeof(bounded), &error), DATA_BIND_ERR_PARSE);
        check(NullableFixedByteWire_from_json(codec, &decoded, short_value, sizeof(short_value) - 1u, &error) != DATA_BIND_OK);
        check_true(decoded.text == retained);
        check_equal(decoded.digest, expected[scenario] + 2u, sizeof(decoded.digest));
        check_equal(decoded._presence[0], expected[scenario][0]);
        check_equal(decoded._nulls[0], expected[scenario][1]);
      }
      memset(wire, 0, wire_len);
      check_equal(decoded.text, BINARY_TEXT);
      data_bind_binary_free(wire);
      NullableFixedByteWire_clear(&source);
      NullableFixedByteWire_clear(&decoded);
      NullableFixedByteWire_clear(&decoded);
      check_equal(decoded._presence[0], (uint8_t)0u);
      check_equal(decoded._nulls[0], (uint8_t)0u);
      check_equal(decoded.digest, (uint8_t[sizeof(decoded.digest)]){0}, sizeof(decoded.digest));
    }
    data_bind_free(codec);
  }

  it("owns fixed byte GROUP entries independently of wire and source storage") {
    static const char json[] = "{\"entries\":[{\"digest\":\"abcdef\",\"code\":4660}],\"text\":\"cat\"}";
    static const uint8_t expected[] = {8u, 0u, 1u, 0u, 'a', 'b', 'c', 'd', 'e', 'f',
                                     0x34u, 0x12u, 3u, 0u, 0u, 0u, 'c', 'a', 't'};
    DataBind *codec = NULL;
    DataBindObject *dynamic = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    FixedByteGroupWire_t source;
    FixedByteGroupWire_t decoded;
    FixedByteGroupWire_t copied;
    uint8_t *wire = NULL, *generic_wire = NULL;
    size_t wire_len = 0u, generic_len = 0u;
    uint8_t bounded[sizeof(expected)] = {0};
    FixedByteGroupWire_init(&source);
    FixedByteGroupWire_init(&decoded);
    FixedByteGroupWire_init(&copied);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(data_bind_object_from_json(codec, "FixedByteGroupWire", json, sizeof(json) - 1u, &dynamic, &error), DATA_BIND_OK);
    check_equal(FixedByteGroupWire_from_bin(codec, &source, expected, sizeof(expected), &error), DATA_BIND_OK);
    check_equal(data_bind_object_serialize_bin(codec, dynamic, &generic_wire, &generic_len, &error), DATA_BIND_OK);
    check_equal(FixedByteGroupWire_to_bin(codec, &source, &wire, &wire_len, &error), DATA_BIND_OK);
    check_equal(wire_len, sizeof(expected));
    check_equal(generic_len, wire_len);
    check_equal(wire, expected, wire_len);
    check_equal(generic_wire, wire, wire_len);
    check_equal(FixedByteGroupWire_to_bin_into(codec, &source, bounded, sizeof(bounded), &generic_len, &error), DATA_BIND_OK);
    check_equal(generic_len, wire_len);
    check_equal(bounded, wire, wire_len);
    check_equal(FixedByteGroupWire_from_bin(codec, &decoded, wire, wire_len, &error), DATA_BIND_OK);
    check_equal(cmeta_data_value_copy(&FixedByteGroupWire_CMETA_DATA, &copied, &decoded), CMETA_OK);
    {
      const FixedByteEntry_t *entry = FixedByteGroupWire_entries_vec_t_at_const(&decoded.entries, 0u);
      const FixedByteEntry_t *copy = FixedByteGroupWire_entries_vec_t_at_const(&copied.entries, 0u);
      static const char invalid[] =
          "{\"entries\":[{\"digest\":\"abcdef\",\"code\":1},{\"digest\":\"a\",\"code\":2}],\"text\":\"replace\"}";
      tstr retained = decoded.text;
      check_not_null(entry);
      check_not_null(copy);
      check_true(entry != copy);
      check_equal(entry->digest, expected + DATA_BIND_BINARY_GROUP_HEADER_SIZE, sizeof(entry->digest));
      check_equal(copy->digest, entry->digest, sizeof(entry->digest));
      check_equal(entry->code, (uint16_t)0x1234u);
      bounded[DATA_BIND_BINARY_GROUP_HEADER_SIZE] = 'z';
      check_equal(FixedByteGroupWire_from_bin(codec, &decoded, bounded, sizeof(bounded) - 1u, &error), DATA_BIND_ERR_PARSE);
      check_true(FixedByteGroupWire_entries_vec_t_at_const(&decoded.entries, 0u) == entry);
      check_equal(entry->digest[0], (uint8_t)'a');
      check(FixedByteGroupWire_from_json(codec, &decoded, invalid, sizeof(invalid) - 1u, &error) != DATA_BIND_OK);
      check_true(FixedByteGroupWire_entries_vec_t_at_const(&decoded.entries, 0u) == entry);
      check_true(decoded.text == retained);
      check_equal(FixedByteGroupWire_entries_vec_t_size(&decoded.entries), (size_t)1u);
    }
    memset(wire, 0, wire_len);
    FixedByteGroupWire_clear(&source);
    FixedByteGroupWire_clear(&decoded);
    data_bind_binary_free(wire);
    data_bind_binary_free(generic_wire);
    data_bind_object_free(dynamic);
    check_equal(FixedByteGroupWire_entries_vec_t_at_const(&copied.entries, 0u)->digest,
                expected + DATA_BIND_BINARY_GROUP_HEADER_SIZE,
                sizeof(((FixedByteEntry_t *)0)->digest));
    FixedByteGroupWire_clear(&copied);
    FixedByteGroupWire_clear(&copied);
    data_bind_free(codec);
  }

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
      check_equal(fixed_len, sizeof(expected));
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

  it("matches generic wire for two nested fixed records and preserves owners on decode failure") {
    static const char json[] =
        "{\"record\":{\"point\":{\"delta\":-32768,\"number\":18446744073709551615},"
        "\"code\":4660},\"text\":\"cat\"}";
    static const uint8_t expected[] = {
        0u, 0x80u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
        0x34u, 0x12u, 3u, 0u, 0u, 0u, 'c', 'a', 't'};
    DataBind *codec = NULL;
    DataBindObject *dynamic = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    NestedWire_t source;
    NestedWire_t decoded;
    uint8_t *dynamic_wire = NULL;
    uint8_t *generated_wire = NULL;
    uint8_t bounded[sizeof(expected) + 1u] = {0};
    size_t dynamic_len = 0u;
    size_t generated_len = 0u;
    size_t bounded_len = 0u;
    tstr old_text;
    NestedWire_init(&source);
    NestedWire_init(&decoded);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(data_bind_object_from_json(codec, "NestedWire", json, sizeof(json) - 1u,
                &dynamic, &error), DATA_BIND_OK);
    check_equal(NestedWire_from_json(codec, &source, json, sizeof(json) - 1u, &error), DATA_BIND_OK);
    check_equal(data_bind_object_serialize_bin(codec, dynamic, &dynamic_wire,
                &dynamic_len, &error), DATA_BIND_OK);
    check_equal(NestedWire_to_bin(codec, &source, &generated_wire, &generated_len, &error), DATA_BIND_OK);
    check_equal(dynamic_len, sizeof(expected));
    check_equal(generated_len, sizeof(expected));
    check_equal(dynamic_wire, expected, sizeof(expected));
    check_equal(generated_wire, expected, sizeof(expected));
    check_equal(NestedWire_to_bin_into(codec, &source, bounded, sizeof(bounded),
                &bounded_len, &error), DATA_BIND_OK);
    check_equal(bounded_len, sizeof(expected));
    check_equal(bounded, expected, sizeof(expected));
    check_equal(NestedWire_from_bin(codec, &decoded, dynamic_wire, dynamic_len, &error), DATA_BIND_OK);
    old_text = decoded.text;
    check_equal(NestedWire_from_bin(codec, &decoded, bounded, bounded_len - 1u, &error), DATA_BIND_ERR_PARSE);
    check_true(decoded.text == old_text);
    check_equal(NestedWire_from_bin(codec, &decoded, bounded, bounded_len + 1u, &error), DATA_BIND_ERR_PARSE);
    check_true(decoded.text == old_text);
    memset(bounded, 0xa5, sizeof(bounded));
    check_equal(NestedWire_to_bin_into(codec, &source, bounded, sizeof(expected) - 1u,
                &bounded_len, &error), DATA_BIND_ERR_LIMIT);
    check_equal(bounded_len, sizeof(expected));
    for (size_t i = 0u; i < sizeof(bounded); ++i)
      check_equal(bounded[i], (uint8_t)0xa5u);
    data_bind_object_free(dynamic);
    data_bind_binary_free(dynamic_wire);
    data_bind_binary_free(generated_wire);
    NestedWire_clear(&source);
    check_equal(decoded.record.point.delta, INT16_MIN);
    check_equal(decoded.record.point.number, UINT64_MAX);
    check_equal(decoded.record.code, UINT16_C(0x1234));
    check_equal(decoded.text, BINARY_TEXT);
    NestedWire_clear(&decoded);
    check_null(decoded.text);
    check_equal(decoded.record.point.number, UINT64_C(0));
    NestedWire_clear(&decoded);
    data_bind_free(codec);
  }

  it("owns GROUP entries and matches generic allocated and bounded Binary wire") {
    static const char json[] =
        "{\"id\":513,\"entries\":[{\"code\":4660,\"amount\":4294967295},"
        "{\"code\":22136,\"amount\":1}],\"text\":\"cat\"}";
    static const uint8_t expected[] = {
        1u, 2u, 6u, 0u, 2u, 0u,
        0x34u, 0x12u, 0xffu, 0xffu, 0xffu, 0xffu,
        0x78u, 0x56u, 1u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 'c', 'a', 't'};
    static const uint8_t extended[] = {
        1u, 2u, 8u, 0u, 2u, 0u,
        0x34u, 0x12u, 0xffu, 0xffu, 0xffu, 0xffu, 0xa5u, 0xa5u,
        0x78u, 0x56u, 1u, 0u, 0u, 0u, 0xa5u, 0xa5u,
        3u, 0u, 0u, 0u, 'c', 'a', 't'};
    DataBind *codec = NULL;
    DataBindObject *dynamic = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    GroupWire_t source, decoded, copied;
    uint8_t *wire = NULL, *dynamic_wire = NULL;
    uint8_t bounded[sizeof(expected) + 1u];
    size_t len = 0u, dynamic_len = 0u, bounded_len = 0u;
    const WireEntry_t *old_entries;
    tstr old_text;
    GroupWire_init(&source);
    GroupWire_init(&decoded);
    GroupWire_init(&copied);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(data_bind_object_from_json(codec, "GroupWire", json, sizeof(json) - 1u,
                &dynamic, &error), DATA_BIND_OK);
    check_equal(GroupWire_from_json(codec, &source, json, sizeof(json) - 1u, &error), DATA_BIND_OK);
    check_equal(GroupWire_entries_vec_t_size(&source.entries), (size_t)2u);
    check_equal(data_bind_object_serialize_bin(codec, dynamic, &dynamic_wire, &dynamic_len, &error), DATA_BIND_OK);
    check_equal(GroupWire_to_bin(codec, &source, &wire, &len, &error), DATA_BIND_OK);
    check_equal(len, sizeof(expected));
    check_equal(dynamic_len, len);
    check_equal(wire, expected, sizeof(expected));
    check_equal(dynamic_wire, expected, sizeof(expected));
    check_equal(GroupWire_to_bin_into(codec, &source, bounded, sizeof(bounded), &bounded_len, &error), DATA_BIND_OK);
    check_equal(bounded_len, sizeof(expected));
    check_equal(bounded, expected, sizeof(expected));
    check_equal(GroupWire_from_bin(codec, &decoded, extended, sizeof(extended), &error), DATA_BIND_OK);
    check_equal(GroupWire_entries_vec_t_size(&decoded.entries), (size_t)2u);
    old_entries = GroupWire_entries_vec_t_at_const(&decoded.entries, 0u);
    old_text = decoded.text;
    check_equal(GroupWire_from_bin(codec, &decoded, expected, sizeof(expected) - 1u, &error), DATA_BIND_ERR_PARSE);
    check_true(GroupWire_entries_vec_t_at_const(&decoded.entries, 0u) == old_entries);
    check_true(decoded.text == old_text);
    check_equal(GroupWire_from_bin(codec, &decoded, bounded, sizeof(expected) + 1u, &error), DATA_BIND_ERR_PARSE);
    check_true(decoded.text == old_text);
    check_equal(cmeta_data_value_copy(&GroupWire_CMETA_DATA, &copied, &decoded), CMETA_OK);
    check_true(GroupWire_entries_vec_t_at_const(&copied.entries, 0u) != old_entries);
    memset(bounded, 0xa5, sizeof(bounded));
    check_equal(GroupWire_to_bin_into(codec, &source, bounded, sizeof(expected) - 1u,
                &bounded_len, &error), DATA_BIND_ERR_LIMIT);
    check_equal(bounded_len, sizeof(expected));
    for (size_t i = 0u; i < sizeof(bounded); ++i) check_equal(bounded[i], (uint8_t)0xa5u);
    data_bind_object_free(dynamic);
    data_bind_binary_free(dynamic_wire);
    data_bind_binary_free(wire);
    GroupWire_clear(&source);
    GroupWire_clear(&decoded);
    check_equal(copied.id, UINT16_C(513));
    check_equal(copied.text, BINARY_TEXT);
    old_entries = GroupWire_entries_vec_t_at_const(&copied.entries, 0u);
    check_not_null(old_entries);
    if (old_entries != NULL) {
      check_equal(old_entries->code, UINT16_C(0x1234));
      check_equal(old_entries->amount, UINT32_MAX);
    }
    GroupWire_clear(&copied);
    GroupWire_clear(&copied);
    check_equal(GroupWire_entries_vec_t_size(&copied.entries), (size_t)0u);
    data_bind_free(codec);
  }

  it("aligns consecutive GROUPs with nested records before a variable tail") {
    static const char json[] =
        "{\"first\":[{\"point\":{\"delta\":-32768,\"number\":18446744073709551615},\"code\":4660}],"
        "\"second\":[{\"code\":2,\"amount\":1}],\"text\":\"cat\"}";
    static const uint8_t expected[] = {
        12u, 0u, 1u, 0u, 0u, 0x80u,
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x34u, 0x12u,
        6u, 0u, 1u, 0u, 2u, 0u, 1u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 'c', 'a', 't'};
    DataBind *codec = NULL;
    DataBindObject *dynamic = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    MultiGroupWire_t source, decoded;
    uint8_t *wire = NULL, *generic = NULL;
    size_t len = 0u, generic_len = 0u;
    MultiGroupWire_init(&source);
    MultiGroupWire_init(&decoded);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(MultiGroupWire_from_json(codec, &source, json, sizeof(json) - 1u, &error), DATA_BIND_OK);
    check_equal(data_bind_object_from_json(codec, "MultiGroupWire", json, sizeof(json) - 1u, &dynamic, &error), DATA_BIND_OK);
    check_equal(data_bind_object_serialize_bin(codec, dynamic, &generic, &generic_len, &error), DATA_BIND_OK);
    check_equal(MultiGroupWire_to_bin(codec, &source, &wire, &len, &error), DATA_BIND_OK);
    check_equal(len, sizeof(expected));
    check_equal(generic_len, len);
    check_equal(wire, expected, sizeof(expected));
    check_equal(generic, expected, sizeof(expected));
    check_equal(MultiGroupWire_from_bin(codec, &decoded, wire, len, &error), DATA_BIND_OK);
    data_bind_object_free(dynamic);
    data_bind_binary_free(generic);
    data_bind_binary_free(wire);
    MultiGroupWire_clear(&source);
    check_equal(MultiGroupWire_first_vec_t_size(&decoded.first), (size_t)1u);
    check_equal(MultiGroupWire_second_vec_t_size(&decoded.second), (size_t)1u);
    {
      const NestedEntry_t *entry = MultiGroupWire_first_vec_t_at_const(&decoded.first, 0u);
      check_not_null(entry);
      if (entry != NULL) {
        check_equal(entry->point.delta, INT16_MIN);
        check_equal(entry->point.number, UINT64_MAX);
      }
    }
    check_equal(decoded.text, BINARY_TEXT);
    MultiGroupWire_clear(&decoded);
    data_bind_free(codec);
  }

  it("keeps ABSENT NULL and empty GROUP distinct and rolls back invalid headers") {
    static const char *const json[] = {
        "{\"text\":\"cat\"}", "{\"entries\":null,\"text\":\"cat\"}",
        "{\"entries\":[],\"text\":\"cat\"}"};
    static const uint8_t absent_nonempty[] = {
        0u, 0u, 6u, 0u, 1u, 0u, 1u, 0u, 2u, 0u, 0u, 0u,
        3u, 0u, 0u, 0u, 'c', 'a', 't'};
    uint8_t expected[] = {0u, 0u, 6u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 'c', 'a', 't'};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    NullableGroupWire_t source, decoded;
    NullableGroupWire_init(&source);
    NullableGroupWire_init(&decoded);
    check_null(NullableGroupWire_CMETA_TYPE.traits);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    for (size_t state = 0u; state < sizeof(json) / sizeof(json[0]); ++state) {
      uint8_t *wire = NULL;
      size_t len = 0u;
      expected[0] = state != 0u;
      expected[1] = state == 1u;
      check_equal(NullableGroupWire_from_json(codec, &source, json[state], strlen(json[state]), &error), DATA_BIND_OK);
      check_equal(NullableGroupWire_to_bin(codec, &source, &wire, &len, &error), DATA_BIND_OK);
      check_equal(len, sizeof(expected));
      check_equal(wire, expected, sizeof(expected));
      check_equal(NullableGroupWire_from_bin(codec, &decoded, expected, sizeof(expected), &error), DATA_BIND_OK);
      check_equal(decoded._presence[0], expected[0]);
      check_equal(decoded._nulls[0], expected[1]);
      check_equal(NullableGroupWire_entries_vec_t_size(&decoded.entries), (size_t)0u);
      data_bind_binary_free(wire);
    }
    check_equal(NullableGroupWire_from_bin(codec, &decoded, absent_nonempty, sizeof(absent_nonempty), &error), DATA_BIND_OK);
    check_equal(decoded._presence[0], (uint8_t)0u);
    check_equal(decoded.text, BINARY_TEXT);
    {
      static const char value[] = "{\"entries\":[{\"code\":1,\"amount\":2}],\"text\":\"cat\"}";
      uint8_t nonempty[sizeof(absent_nonempty)];
      uint8_t *wire = NULL;
      size_t len = 0u;
      memcpy(nonempty, absent_nonempty, sizeof(nonempty));
      nonempty[0] = 1u;
      check_equal(NullableGroupWire_from_json(codec, &source, value, sizeof(value) - 1u, &error), DATA_BIND_OK);
      check_equal(NullableGroupWire_to_bin(codec, &source, &wire, &len, &error), DATA_BIND_OK);
      check_equal(len, sizeof(nonempty));
      check_equal(wire, nonempty, sizeof(nonempty));
      check_equal(NullableGroupWire_from_bin(codec, &decoded, wire, len, &error), DATA_BIND_OK);
      check_equal(NullableGroupWire_entries_vec_t_size(&decoded.entries), (size_t)1u);
      data_bind_binary_free(wire);
    }
    {
      tstr old_text = decoded.text;
      expected[0] = 1u;
      expected[1] = 1u;
      expected[4] = 1u;
      check_equal(NullableGroupWire_from_bin(codec, &decoded, expected, sizeof(expected), &error), DATA_BIND_ERR_PARSE);
      check_true(decoded.text == old_text);
      expected[4] = 0u;
      expected[2] = 5u;
      check_equal(NullableGroupWire_from_bin(codec, &decoded, expected, sizeof(expected), &error), DATA_BIND_ERR_PARSE);
      check_true(decoded.text == old_text);
    }
    NullableGroupWire_clear(&source);
    NullableGroupWire_clear(&decoded);
    check_equal(NullableGroupWire_entries_vec_t_size(&decoded.entries), (size_t)0u);
    NullableGroupWire_clear(&decoded);
    check_equal(decoded._presence[0], (uint8_t)0u);
    check_equal(decoded._nulls[0], (uint8_t)0u);
    data_bind_free(codec);
  }

  it("copies and releases GROUP element owners while rejecting variable Binary entries") {
    static const char json[] = "{\"entries\":[{\"label\":\"cat\"},{\"label\":\"raw\"}]}";
    static const char invalid[] = "{\"entries\":[{\"label\":\"replacement\"},{\"label\":{}}]}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    OwnedGroup_t source, copied;
    uint8_t *wire = NULL;
    size_t len = 0u;
    const OwnedEntry_t *old_entry;
    OwnedGroup_init(&source);
    OwnedGroup_init(&copied);
    check_equal(Tail_codec_create(&codec, &error), DATA_BIND_OK);
    check_equal(OwnedGroup_from_json(codec, &source, json, sizeof(json) - 1u, &error), DATA_BIND_OK);
    check_equal(OwnedGroup_entries_vec_t_size(&source.entries), (size_t)2u);
    old_entry = OwnedGroup_entries_vec_t_at_const(&source.entries, 0u);
    check_not_null(old_entry);
    check(OwnedGroup_from_json(codec, &source, invalid, sizeof(invalid) - 1u, &error) != DATA_BIND_OK);
    check_true(OwnedGroup_entries_vec_t_at_const(&source.entries, 0u) == old_entry);
    check_equal(cmeta_data_value_copy(&OwnedGroup_CMETA_DATA, &copied, &source), CMETA_OK);
    if (old_entry != NULL) {
      const OwnedEntry_t *copy = OwnedGroup_entries_vec_t_at_const(&copied.entries, 0u);
      check_not_null(copy);
      if (copy != NULL) check_true(copy->label != old_entry->label);
    }
    check_equal(OwnedGroup_to_bin(codec, &source, &wire, &len, &error), DATA_BIND_ERR_SCHEMA);
    check_null(wire);
    check_equal(len, (size_t)0u);
    OwnedGroup_clear(&source);
    old_entry = OwnedGroup_entries_vec_t_at_const(&copied.entries, 0u);
    check_not_null(old_entry);
    if (old_entry != NULL) check_equal(old_entry->label, BINARY_TEXT);
    OwnedGroup_clear(&copied);
    OwnedGroup_clear(&copied);
    data_bind_free(codec);
  }
}
