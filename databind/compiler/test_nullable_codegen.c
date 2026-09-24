#include "nullable_generated.h"

#include "tinytest.h"

#include <stdint.h>
#include <string.h>

static void check_nullable_user(const User_t *user) {
  check_not_null(user);
  if (user == NULL) return;

  check((user->_nulls[0] & UINT8_C(0x01)) != 0u);
  check((user->_nulls[0] & UINT8_C(0x02)) == 0u);
  check((user->_presence[0] & UINT8_C(0x01)) != 0u);
  check_equal(user->score, UINT32_C(7));
}

spec("generated nullable DataBind artifact") {
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  User_t user;
  User_t decoded;

  before_each() {
    User_init(&user);
    User_init(&decoded);
    check_equal(Nullable_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
  }

  after_each() {
    User_clear(&decoded);
    User_clear(&user);
    data_bind_free(codec);
    codec = NULL;
  }

  it("executes JSON YAML and TBE while CSV XML remain explicit fail-fast") {
    static const char json[] = "{\"display_name\":null}";
    char *text = NULL;
    size_t text_len = 0u;
    uint8_t *wire = NULL;
    size_t wire_len = 0u;

    check_equal(
        User_from_json(codec, &user, json, sizeof(json) - 1u, &error),
        DATA_BIND_OK);
    check_nullable_user(&user);

    check_equal(
        User_to_json(codec, &user, &text, &text_len, &error),
        DATA_BIND_OK);
    check_not_null(text);
    if (text != NULL) {
      check_not_null(strstr(text, "\"display_name\":null"));
      check_not_null(strstr(text, "\"score\":7"));
    }
    tbe_typed_serialized_free(text);
    text = NULL;
    text_len = 0u;

    check_equal(
        User_to_yaml(codec, &user, &text, &text_len, &error),
        DATA_BIND_OK);
    check_not_null(text);
    check(text_len != 0u);
    tbe_typed_serialized_free(text);
    text = NULL;
    text_len = 0u;

    check_equal(User_to_bin(&user, &wire, &wire_len, &error), DATA_BIND_OK);
    check_not_null(wire);
    check(wire_len != 0u);
    if (wire != NULL) {
      check_equal(
          User_from_bin(codec, &decoded, wire, wire_len, &error),
          DATA_BIND_OK);
      check_nullable_user(&decoded);
    }
    tbe_typed_serialized_free(wire);
    wire = NULL;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        User_to_csv(codec, &user, &text, &text_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(text);
    check_equal(text_len, 0u);
    check_not_null(strstr(error.message, "nullable"));

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        User_to_xml(codec, &user, &text, &text_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(text);
    check_equal(text_len, 0u);
    check_not_null(strstr(error.message, "nullable"));
  }

  it("keeps explicit null distinct from an absent defaulted nullable field") {
    static const char json[] =
        "{\"display_name\":\"Ada\",\"score\":null}";
    char *text = NULL;
    size_t text_len = 0u;

    check_equal(
        User_from_json(codec, &user, json, sizeof(json) - 1u, &error),
        DATA_BIND_OK);
    check((user._nulls[0] & UINT8_C(0x01)) == 0u);
    check((user._nulls[0] & UINT8_C(0x02)) != 0u);
    check((user._presence[0] & UINT8_C(0x01)) != 0u);
    check_equal(user.score, UINT32_C(0));

    check_equal(
        User_to_json(codec, &user, &text, &text_len, &error),
        DATA_BIND_OK);
    check_not_null(text);
    if (text != NULL)
      check_not_null(strstr(text, "\"score\":null"));
    tbe_typed_serialized_free(text);
  }
}
