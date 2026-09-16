/**
 * @file test_data_bind_value_boundary.c
 * @brief Public-link boundary regression for dynamic value ownership.
 */

#include "data_bind.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

spec("data_bind dynamic value public boundary") {
  it("keeps every public value accessor valid after clone releases its source") {
    const char *schema =
        "composite Meta { i32 seq; } "
        "message Values { i32 signed_value; i64 long_value; u64 unsigned_value; double ratio; "
        "bool active; uuid uid; datetime created; date day; time at; duration span; decimal price; "
        "bigint total; money cost; Meta meta; string note; bytes raw; list<u32> values; "
        "set<i8> tags; map<string,i64> attrs; }";
    const char *json =
        "{\"signed_value\":-7,\"long_value\":-9007199254740991,"
        "\"unsigned_value\":18446744073709551615,\"ratio\":1.25,\"active\":true,"
        "\"note\":\"public clone\",\"raw\":\"Az\","
        "\"uid\":\"01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001\","
        "\"created\":\"Sat, 04 Mar 2006 13:27:54 GMT\","
        "\"day\":\"2026-07-15\",\"at\":\"09:30:05.123\",\"span\":\"1h2m3s\","
        "\"price\":\"12.3400\",\"total\":\"123456789012345678901234567890\","
        "\"cost\":{\"amount\":\"12.34\",\"currency\":\"USD\"},"
        "\"meta\":{\"seq\":7},\"values\":[1,4294967295],\"tags\":[-128,127],"
        "\"attrs\":{\"min_value\":-9223372036854775808}}";
    DataBind *codec = NULL;
    DataBindValue *source = NULL;
    DataBindValue *clone = NULL;
    const DataBindValue *root;
    DataBindMapEntry entry;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint64_t unsigned_value = 0;
    int64_t signed_value = 0;
    size_t raw_len = 0;
    char text[128];

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error), DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(data_bind_parse_json(codec, "Values", json, strlen(json), &source, &error),
                  DATA_BIND_OK);
      check_not_null(source);
    }
    if (source != NULL) {
      check_equal(data_bind_value_clone(source, &clone), DATA_BIND_OK);
      check_not_null(clone);
    }
    data_bind_value_free(source);
    source = NULL;

    if (clone != NULL) {
      root = clone;
      check_equal(data_bind_value_kind(root), DATA_BIND_VALUE_OBJECT);
      check_equal(data_bind_value_field_count(root), (size_t)19u);
      check_equal(data_bind_value_as_int(data_bind_value_get(root, "signed_value")), -7);
      check_equal(data_bind_value_get_int64(data_bind_value_get(root, "long_value"), &signed_value),
                  DATA_BIND_OK);
      check(signed_value == INT64_C(-9007199254740991));
      check_equal(data_bind_value_get_uint64(data_bind_value_get(root, "unsigned_value"),
                                              &unsigned_value),
                  DATA_BIND_OK);
      check(unsigned_value == UINT64_MAX);
      check_within(data_bind_value_as_double(data_bind_value_get(root, "ratio")), 1.25, 0.0);
      check_true(data_bind_value_as_bool(data_bind_value_get(root, "active")));
      check_equal(data_bind_value_as_string(data_bind_value_get(root, "note")), "public clone");
      check_equal(data_bind_value_as_bytes(data_bind_value_get(root, "raw"), &raw_len), "Az", 2u);
      check_equal(raw_len, (size_t)2u);
      check_equal(data_bind_value_as_uuid_string(data_bind_value_get(root, "uid"), text, sizeof(text)),
                  "01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001");
      check_equal(data_bind_value_as_datetime_string(data_bind_value_get(root, "created"), text,
                                                      sizeof(text)),
                  "Sat, 04 Mar 2006 13:27:54 GMT");
      check_equal(data_bind_value_as_date_string(data_bind_value_get(root, "day"), text, sizeof(text)),
                  "2026-07-15");
      check_equal(data_bind_value_as_time_string(data_bind_value_get(root, "at"), text, sizeof(text)),
                  "09:30:05.123");
      check_equal(data_bind_value_as_duration_milliseconds(data_bind_value_get(root, "span")),
                  INT64_C(3723000));
      check_equal(data_bind_value_as_decimal_string(data_bind_value_get(root, "price"), text,
                                                     sizeof(text)),
                  "12.34");
      check_equal(data_bind_value_as_bigint_string(data_bind_value_get(root, "total")),
                  "123456789012345678901234567890");
      check_equal(data_bind_value_as_money_string(data_bind_value_get(root, "cost"), text,
                                                   sizeof(text)),
                  "USD 12.34");
      check_equal(data_bind_value_as_int(data_bind_value_get(data_bind_value_get(root, "meta"), "seq")),
                  7);
      check_equal(data_bind_value_count(data_bind_value_get(root, "values")), (size_t)2u);
      check_equal(data_bind_value_get_uint64(
                      data_bind_value_at(data_bind_value_get(root, "values"), 1u), &unsigned_value),
                  DATA_BIND_OK);
      check(unsigned_value == UINT32_MAX);
      check_equal(data_bind_value_kind(data_bind_value_get(root, "tags")), DATA_BIND_VALUE_SET);
      check_equal(data_bind_value_count(data_bind_value_get(root, "tags")), (size_t)2u);
      check_equal(data_bind_value_as_int(data_bind_value_at(data_bind_value_get(root, "tags"), 0u)),
                  -128);
      entry = data_bind_value_map_entry_at(data_bind_value_get(root, "attrs"), 0u);
      check_equal(entry.key, "min_value");
      check_equal(data_bind_value_get_int64(entry.value, &signed_value), DATA_BIND_OK);
      check(signed_value == INT64_MIN);
    }

    data_bind_value_free(clone);
    data_bind_free(codec);
  }
}
