#include "optional_scalar_lifecycle_generated.h"
#include "tinytest.h"

#include <string.h>

spec("optional local lifecycle uses canonical CMeta") {
  it("initializes and clears scalar storage and state overlays") {
    ScalarState_t value;
    memset(&value, 0xa5, sizeof(value));
    ScalarState_init(&value);
    check_equal(value.optional_value, 0);
    check_equal(value.nullable_value, 0u);
    check_equal(value.tri_value, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    value.optional_value = 7;
    value.nullable_value = 8u;
    value.tri_value = 9u;
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    ScalarState_clear(&value);
    check_equal(value.optional_value, 0);
    check_equal(value.nullable_value, 0u);
    check_equal(value.tri_value, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
  }

  it("initializes and releases owned strings with local state overlays") {
    OwnedState_t value;
    const cmeta_data_desc *data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    memset(&value, 0xa5, sizeof(value));
    check_equal(OwnedState_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (data != NULL) {
      check_equal(cmeta_data_value_init_zero(data, &value), CMETA_OK);
      check_equal(cmeta_data_value_restore_zero(data, &value), CMETA_OK);
    }
    OwnedState_init(&value);
    check_null(value.label);
    check_null(value.note);
    check_null(value.tri_value);
    check_equal(value.count, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    value.label = tstr_dup("label");
    value.note = tstr_dup("note");
    value.tri_value = tstr_dup("tri");
    check_not_null(value.label);
    check_not_null(value.note);
    check_not_null(value.tri_value);
    value.count = 17u;
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    OwnedState_clear(&value);
    check_null(value.label);
    check_null(value.note);
    check_null(value.tri_value);
    check_equal(value.count, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    value.label = tstr_dup("direct-restore");
    check_not_null(value.label);
    if (data != NULL && value.label != NULL)
      check_equal(cmeta_data_value_restore_zero(data, &value), CMETA_OK);
    OwnedState_clear(&value);
    check_null(value.label);
  }

  it("keeps unsupported JSON conversion closed") {
    static const char input[] =
        "{\"nullable_value\":null,\"tri_value\":9}";
    DataBind *codec = NULL;
    ScalarState_t value;
    char *json = NULL;
    size_t json_len = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    ScalarState_init(&value);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(ScalarState_from_json(
                      codec, &value, input, sizeof(input) - 1u, &error),
                  DATA_BIND_ERR_SCHEMA);
      check_equal(value.optional_value, 0);
      check_equal(value.nullable_value, 0u);
      check_equal(value.tri_value, 0u);
      check_equal(ScalarState_to_json(
                      codec, &value, &json, &json_len, &error),
                  DATA_BIND_ERR_SCHEMA);
      check_null(json);
      check_equal(json_len, (size_t)0u);
    }
    data_bind_serialized_free(json);
    ScalarState_clear(&value);
    data_bind_free(codec);
  }

  it("keeps owned string JSON conversion closed without changing storage") {
    static const char input[] = "{\"count\":5,\"label\":\"new\"}";
    DataBind *codec = NULL;
    OwnedState_t value;
    tstr original;
    char *json = NULL;
    size_t json_len = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    OwnedState_init(&value);
    value.label = tstr_dup("keep");
    check_not_null(value.label);
    original = value.label;
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    if (codec != NULL && original != NULL) {
      check_equal(OwnedState_from_json(
                      codec, &value, input, sizeof(input) - 1u, &error),
                  DATA_BIND_ERR_SCHEMA);
      check(value.label == original);
      check_equal(OwnedState_to_json(codec, &value, &json, &json_len, &error),
                  DATA_BIND_ERR_SCHEMA);
      check_null(json);
      check_equal(json_len, (size_t)0u);
    }
    data_bind_serialized_free(json);
    OwnedState_clear(&value);
    data_bind_free(codec);
  }
}
