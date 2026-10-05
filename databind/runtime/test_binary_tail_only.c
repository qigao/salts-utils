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
    check_equal(bounded_len, (size_t)0u);
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
    check_equal(bounded_len, (size_t)0u);
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
