#include "c11_lua_data_bind.h"
#include "tinytest.h"

#include <string.h>

suite("c11 lua data bind") {
  static lua_State *L;

  before_each() {
    L = luaL_newstate();
    check_not_null(L);
  }

  after_each() {
    if (L) lua_close(L);
    L = NULL;
  }

  it("copies schema-validated objects, lists, and maps into Lua") {
    static const char schema[] =
        "message Payload { int64 id; bool active; string name; "
        "list<uint32> values; map<string,int32> attrs; }";
    static const char json[] =
        "{\"id\":42,\"active\":true,\"name\":\"schema\","
        "\"values\":[7,9],\"attrs\":{\"left\":3,\"right\":4}}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *value = NULL;

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_parse_json(codec, "Payload", json, sizeof(json) - 1, &value, &error),
                   DATA_BIND_OK);
    }
    check_not_null(value);
    if (value) {
      check_int_eq(c11_lua_push_data_bind_value(
                       L, value, C11_LUA_DATA_BIND_DEFAULT_MAX_DEPTH),
                   DATA_BIND_OK);
      check_true(lua_istable(L, -1));
      lua_getfield(L, -1, "id");
      check_int_eq(lua_tointeger(L, -1), 42);
      lua_pop(L, 1);
      lua_getfield(L, -1, "active");
      check_true(lua_toboolean(L, -1));
      lua_pop(L, 1);
      lua_getfield(L, -1, "name");
      check_str_eq(lua_tostring(L, -1), "schema");
      lua_pop(L, 1);
      lua_getfield(L, -1, "values");
      lua_rawgeti(L, -1, 2);
      check_int_eq(lua_tointeger(L, -1), 9);
      lua_pop(L, 2);
      lua_getfield(L, -1, "attrs");
      lua_getfield(L, -1, "right");
      check_int_eq(lua_tointeger(L, -1), 4);
      lua_pop(L, 3);
    }

    data_bind_value_free(value);
    data_bind_free(codec);
  }

  it("restores the Lua stack when the depth limit is exceeded") {
    static const char schema[] =
        "composite Child { int32 value; } message Parent { Child child; }";
    static const char json[] = "{\"child\":{\"value\":7}}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *value = NULL;
    int base = lua_gettop(L);

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_parse_json(codec, "Parent", json, sizeof(json) - 1, &value, &error),
                   DATA_BIND_OK);
    }
    if (value) {
      check_int_eq(c11_lua_push_data_bind_value(L, value, 1u), DATA_BIND_ERR_LIMIT);
      check_int_eq(lua_gettop(L), base);
    }

    data_bind_value_free(value);
    data_bind_free(codec);
  }

  it("rejects uint64 values that Lua cannot represent exactly") {
    static const char schema[] = "message Exact { uint64 id; }";
    static const char json[] = "{\"id\":18446744073709551615}";
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBind *codec = NULL;
    DataBindValue *value = NULL;
    int base = lua_gettop(L);

    check_int_eq(data_bind_create_from_text(schema, sizeof(schema) - 1, &codec, &error),
                 DATA_BIND_OK);
    if (codec) {
      check_int_eq(data_bind_parse_json(codec, "Exact", json, sizeof(json) - 1, &value, &error),
                   DATA_BIND_OK);
    }
    if (value) {
      check_int_eq(c11_lua_push_data_bind_value(
                       L, value, C11_LUA_DATA_BIND_DEFAULT_MAX_DEPTH),
                   DATA_BIND_ERR_LIMIT);
      check_int_eq(lua_gettop(L), base);
    }

    data_bind_value_free(value);
    data_bind_free(codec);
  }
}
