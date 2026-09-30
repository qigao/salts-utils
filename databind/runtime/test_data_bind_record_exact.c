#include "data_bind.h"
#include "tinytest.h"

spec("DataBind exact JSON record binding") {
  it("keeps legacy coercion while exact mode rejects token and field drift") {
    static const char schema[] =
        "composite Child { int32 label; } "
        "message Plan { string tool; int32 count; bool enabled; Child child; }";
    static const char valid[] =
        "{\"tool\":\"inspect\",\"count\":2,\"enabled\":true,"
        "\"child\":{\"label\":7}}";
    static const char number_as_string[] =
        "{\"tool\":7,\"count\":2,\"enabled\":true,"
        "\"child\":{\"label\":7}}";
    static const char string_as_number[] =
        "{\"tool\":\"inspect\",\"count\":\"2\",\"enabled\":true,"
        "\"child\":{\"label\":7}}";
    static const char nested_unknown[] =
        "{\"tool\":\"inspect\",\"count\":2,\"enabled\":true,"
        "\"child\":{\"label\":7,\"backend\":\"forbidden\"}}";
    DataBindJsonOptions exact = DATA_BIND_JSON_OPTIONS_INIT;
    DataBind *codec = NULL;
    DataBindRecord *record = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    exact.flags = DATA_BIND_JSON_BIND_EXACT_SCALAR_TOKENS |
                  DATA_BIND_JSON_BIND_REJECT_UNKNOWN_FIELDS;

    check_equal(data_bind_create_from_text(schema, sizeof(schema) - 1u,
                                           &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    /* Historical dynamic binding remains permissive. */
    check_equal(data_bind_record_from_json(
                    codec, "Plan", number_as_string,
                    sizeof(number_as_string) - 1u, &record, &error),
                DATA_BIND_OK);
    check_not_null(record);
    data_bind_record_free(record);
    record = NULL;

    check_equal(data_bind_record_from_json_ex(
                    codec, "Plan", valid, sizeof(valid) - 1u, &exact,
                    &record, &error),
                DATA_BIND_OK);
    check_not_null(record);
    data_bind_record_free(record);
    record = NULL;

    check_equal(data_bind_record_from_json_ex(
                    codec, "Plan", number_as_string,
                    sizeof(number_as_string) - 1u, &exact, &record, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(record);

    check_equal(data_bind_record_from_json_ex(
                    codec, "Plan", string_as_number,
                    sizeof(string_as_number) - 1u, &exact, &record, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(record);

    check_equal(data_bind_record_from_json_ex(
                    codec, "Plan", nested_unknown,
                    sizeof(nested_unknown) - 1u, &exact, &record, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(record);

    exact.flags = UINT32_C(1) << 31;
    check_equal(data_bind_record_from_json_ex(
                    codec, "Plan", valid, sizeof(valid) - 1u, &exact,
                    &record, &error),
                DATA_BIND_ERR_INVALID_ARG);
    check_null(record);

    data_bind_free(codec);
  }
}
