#include "data_bind_value_reader.h"
#include <tinytest.hpp>
#include <type_traits>

static_assert(std::is_standard_layout<DataBindValueReaderLimits>::value,
              "value reader limits must have C layout");
using OpenValueReader = DataBindStatus (*)(const DataBindValue *,
    const DataBindValueReaderLimits *, cserde_reader **, DataBindError *);
static_assert(std::is_same<decltype(&data_bind_value_reader_open), OpenValueReader>::value,
              "value reader open signature drift");
using CloseValueReader = void (*)(cserde_reader *);
static_assert(std::is_same<decltype(&data_bind_value_reader_close), CloseValueReader>::value,
              "value reader close signature drift");

spec("DataBind value reader public C++ boundary") {
  it("links C ABI and traverses a borrowed string through public APIs") {
    static const char schema[] = "message Text { string text; }";
    static const char json[] = "{\"text\":\"C++ reader\"}";
    DataBind *codec = nullptr;
    DataBindValue *value = nullptr;
    cserde_reader *reader = nullptr;
    cserde_token token = {};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindValueReaderLimits limits = DATA_BIND_VALUE_READER_LIMITS_INIT;
    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1u, &codec, &error), DATA_BIND_OK);
    check_equal(data_bind_parse_json(codec, "Text", json, sizeof(json) - 1u, &value, &error), DATA_BIND_OK);
    check_equal(data_bind_value_reader_open(data_bind_value_get(value, "text"), &limits, &reader, &error), DATA_BIND_OK);
    data_bind_free(codec);
    check_equal(cserde_reader_next(reader, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_STRING);
    check_equal(token.value.slice.size, sizeof("C++ reader") - 1u);
    check_equal(token.value.slice.data, "C++ reader", token.value.slice.size);
    check_equal(cserde_reader_next(reader, &token), CSERDE_DONE);
    data_bind_value_reader_close(reader);
    data_bind_value_free(value);
  }
}
