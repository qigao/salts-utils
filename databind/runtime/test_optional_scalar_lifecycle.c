#include "optional_scalar_lifecycle_generated.h"
#include "tinytest.h"

#include <string.h>

spec("optional scalar lifecycle uses canonical CMeta") {
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
}
