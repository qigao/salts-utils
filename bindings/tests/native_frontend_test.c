#include "fixtures/native_math.h"
#include <salts/bindings/native.h>
#include <salts/bindings/lua/module.h>
#include <salts/bindings/quickjs/module.h>
#include <lauxlib.h>
#include "tinytest.h"

SALTS_BIND_C_FUNCTION(int, native_add, (int, left), (int, right));
SALTS_BIND_C_FUNCTION(int, native_sum3, (int, a), (int, b), (int, c));
SALTS_BIND_C_FUNCTION(void, native_store, (int, value));
SALTS_BIND_C_FUNCTION0(int, native_load);
SALTS_BIND_C_FIELD(point_x, native_point, x, int);
SALTS_BIND_C_FIELD(point_y, native_point, y, int);

spec("External C binding declarations") {
  it("binds unrelated C source without the CMeta finite callable catalog") {
    const salts_binding_function functions[] = {
        SALTS_BIND_C_EXPORT(native_add), SALTS_BIND_C_EXPORT(native_sum3),
        SALTS_BIND_C_EXPORT(native_store), SALTS_BIND_C_EXPORT(native_load)};
    native_point point = {3, 4};
    const salts_binding_property properties[] = {point_x, point_y};
    const salts_binding_native_object point_binding = {&point, NULL, 0, properties, 2};
    const salts_binding_object objects[] = {{"point", NULL, &point_binding}};
    const salts_binding_module module = {functions, 4, objects, 1};
    const salts_lua_limits lua_limits = {8, 32, 4096};
    const salts_quickjs_limits js_limits = {8, 32, 4096};
    lua_State *lua = luaL_newstate();
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *js = JS_NewContext(runtime);
    JSValue value = JS_UNDEFINED, global, result;
    int32_t number = 0;
    const char script[] = "native.native_sum3(10, 20, native.native_add(5, 7))";
    check_equal(salts_lua_push_module(lua, &module, lua_limits), CMETA_OK);
    lua_setglobal(lua, "native");
    check_equal(luaL_dostring(lua, "return native.native_sum3(10, 20, native.native_add(5, 7))"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), (lua_Integer)42);
    check_equal(luaL_dostring(lua, "native.point.x = 9; return native.point.x + native.point.y"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), (lua_Integer)13);
    check_equal(point.x, 9);
    check_equal(luaL_dostring(lua, "native.native_store(13); return native.native_load()"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), (lua_Integer)13);
    check_equal(salts_quickjs_push_module(js, &module, js_limits, &value), CMETA_OK);
    global = JS_GetGlobalObject(js);
    check_equal(JS_SetPropertyStr(js, global, "native", value), 1);
    JS_FreeValue(js, global);
    result = JS_Eval(js, script, sizeof(script) - 1, "native.js", JS_EVAL_TYPE_GLOBAL);
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &number, result), 0);
    check_equal(number, 42);
    JS_FreeValue(js, result);
    {
      const char store[] = "native.native_store(21); native.native_load()";
      result = JS_Eval(js, store, sizeof(store) - 1, "store.js", JS_EVAL_TYPE_GLOBAL);
      check_false(JS_IsException(result));
      check_equal(JS_ToInt32(js, &number, result), 0);
      check_equal(number, 21);
      JS_FreeValue(js, result);
    }
    {
      const char write_field[] = "native.point.y = 8; native.point.x + native.point.y";
      result = JS_Eval(js, write_field, sizeof(write_field) - 1, "point.js", JS_EVAL_TYPE_GLOBAL);
      check_false(JS_IsException(result));
      check_equal(JS_ToInt32(js, &number, result), 0);
      check_equal(number, 17);
      check_equal(point.y, 8);
      JS_FreeValue(js, result);
    }
    lua_close(lua);
    JS_FreeContext(js);
    JS_FreeRuntime(runtime);
  }
}
