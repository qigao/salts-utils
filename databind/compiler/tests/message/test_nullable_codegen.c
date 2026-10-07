#include "nullable_generated.h"

#include "tinytest.h"

#include <stdint.h>
#include <string.h>

enum {
  NULLABLE_SCORE_MASK = 1u << User_NULLABLE_score,
  NULLABLE_DISPLAY_NAME_MASK = 1u << User_NULLABLE_display_name,
  OPTIONAL_SCORE_MASK = 1u << User_OPTIONAL_score,
  NULLABLE_REUSE_COUNT = 3
};

static void check_nullable_user(const User_t *user) {
  check_not_null(user);
  if (user == NULL) return;

  check_equal(user->_nulls[0], (uint8_t)NULLABLE_DISPLAY_NAME_MASK);
  check_equal(user->_presence[0], (uint8_t)OPTIONAL_SCORE_MASK);
  check_equal(user->score, UINT32_C(7));
}

static void check_user_from_json(DataBind *codec, User_t *user,
                                 const char *json, size_t length,
                                 DataBindError *error) {
  DataBindStatus status = User_from_json(codec, user, json, length, error);
  info("generated nullable JSON: status=%d path=%s message=%s",
       (int)status, error->path, error->message);
  check_equal(status, DATA_BIND_OK);
}

spec("generated nullable DataBind artifact") {
  static DataBind *codec;
  static DataBindError error;
  static User_t user;
  static User_t decoded;
  static char *text;
  static size_t text_len;
  static uint8_t *wire;
  static size_t wire_len;

  before_each() {
    codec = NULL;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    text = NULL;
    text_len = 0u;
    wire = NULL;
    wire_len = 0u;
    User_init(&user);
    User_init(&decoded);
    check_equal(Nullable_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
  }

  after_each() {
    data_bind_serialized_free(text);
    data_bind_binary_free(wire);
    text = NULL;
    wire = NULL;
    User_clear(&decoded);
    User_clear(&user);
    data_bind_free(codec);
    codec = NULL;
  }

  it("executes JSON YAML and TBE while CSV XML remain explicit fail-fast") {
    static const char json[] = "{\"display_name\":null}";

    check_user_from_json(codec, &user, json, sizeof(json) - 1u, &error);
    check_nullable_user(&user);

    check_equal(
        User_to_json(codec, &user, &text, &text_len, &error),
        DATA_BIND_OK);
    check_not_null(text);
    if (text != NULL) {
      check_not_null(strstr(text, "\"display_name\":null"));
      check_not_null(strstr(text, "\"score\":7"));
    }
    data_bind_serialized_free(text);
    text = NULL;
    text_len = 0u;

    check_equal(
        User_to_yaml(codec, &user, &text, &text_len, &error),
        DATA_BIND_OK);
    check_not_null(text);
    check(text_len != 0u);
    check_equal(User_from_yaml(codec, &decoded, text, text_len, &error), DATA_BIND_OK);
    check_nullable_user(&decoded);
    data_bind_serialized_free(text);
    text = NULL;
    text_len = 0u;

    check_equal(User_to_bin(codec, &user, &wire, &wire_len, &error), DATA_BIND_OK);
    check_not_null(wire);
    {
      static const uint8_t expected[] = {
          OPTIONAL_SCORE_MASK, NULLABLE_DISPLAY_NAME_MASK,
          7u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
      check_equal(wire_len, sizeof(expected));
      check_equal(wire, expected, sizeof(expected));
    }
    if (wire != NULL) {
      check_equal(
          User_from_bin(codec, &decoded, wire, wire_len, &error),
          DATA_BIND_OK);
      check_nullable_user(&decoded);
    }
    data_bind_binary_free(wire);
    wire = NULL;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        User_to_csv(codec, &user, &text, &text_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(text);
    check_equal(text_len, 0u);
    check_contains(error.message, "CSV");

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        User_to_xml(codec, &user, &text, &text_len, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(text);
    check_equal(text_len, 0u);
    check_contains(error.message, "XML");
  }

  it("keeps explicit null distinct from an absent defaulted nullable field") {
    static const char json[] =
        "{\"display_name\":\"Ada\",\"score\":null}";

    check_user_from_json(codec, &user, json, sizeof(json) - 1u, &error);
    check_equal(user._nulls[0], (uint8_t)NULLABLE_SCORE_MASK);
    check_equal(user._presence[0], (uint8_t)OPTIONAL_SCORE_MASK);
    check_equal(user.score, UINT32_C(0));

    check_equal(
        User_to_json(codec, &user, &text, &text_len, &error),
        DATA_BIND_OK);
    check_not_null(text);
    if (text != NULL)
      check_not_null(strstr(text, "\"score\":null"));
    data_bind_serialized_free(text);
    text = NULL;
  }

  it("preserves owned values and state after JSON and Binary decode failures") {
    static const char source[] = "{\"display_name\":\"Ada\",\"score\":null}";
    static const char previous[] = "{\"display_name\":\"previous\",\"score\":1}";
    static const char invalid[] = "{\"score\":2,\"display_name\":42}";
    static const uint8_t expected[] = {
        OPTIONAL_SCORE_MASK, NULLABLE_SCORE_MASK,
        0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 'A', 'd', 'a'};
    size_t iteration;

    check_user_from_json(codec, &user, source, sizeof(source) - 1u, &error);
    check_equal(User_to_bin(codec, &user, &wire, &wire_len, &error), DATA_BIND_OK);
    check_equal(wire_len, sizeof(expected));
    check_equal(wire, expected, sizeof(expected));

    for (iteration = 0u; iteration < NULLABLE_REUSE_COUNT; ++iteration) {
      check_equal(User_from_json(codec, &user, invalid, sizeof(invalid) - 1u, &error),
                  DATA_BIND_ERR_TYPE_MISMATCH);
      check_equal(user._nulls[0], (uint8_t)NULLABLE_SCORE_MASK);
      check_equal(user.score, UINT32_C(0));
      check_equal(User_to_json(codec, &user, &text, &text_len, &error), DATA_BIND_OK);
      check_contains(text, "\"display_name\":\"Ada\"");
      check_contains(text, "\"score\":null");
      data_bind_serialized_free(text);
      text = NULL;

      check_user_from_json(codec, &decoded, previous, sizeof(previous) - 1u, &error);
      check_equal(User_from_bin(codec, &decoded, wire, wire_len - 1u, &error),
                  DATA_BIND_ERR_PARSE);
      check_equal(decoded._nulls[0], UINT8_C(0));
      check_equal(decoded.score, UINT32_C(1));
      check_equal(User_to_json(codec, &decoded, &text, &text_len, &error), DATA_BIND_OK);
      check_contains(text, "\"display_name\":\"previous\"");
      data_bind_serialized_free(text);
      text = NULL;

      check_equal(User_from_bin(codec, &decoded, wire, wire_len, &error), DATA_BIND_OK);
      check_equal(decoded._nulls[0], (uint8_t)NULLABLE_SCORE_MASK);
      check_equal(decoded.score, UINT32_C(0));
    }
  }
}
