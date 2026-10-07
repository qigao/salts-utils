#include "fixtures/native_math.h"
#include "fixtures/native_counter.hpp"
#include <salts/bindings/lua/native.hpp>
#include <salts/bindings/quickjs/native.hpp>
#include <data_bind_typescript.h>
extern "C" {
#include <lauxlib.h>
}
#include <string>
#include <cstring>
#include "tinytest.hpp"

static JSValue eval(JSContext *js, const char *source) {
  return JS_Eval(js, source, std::strlen(source), "native.js", JS_EVAL_TYPE_GLOBAL);
}

spec("External C++ binding declarations") {
  it("infers free functions, methods and fields from ordinary C/C++ headers") {
    NativeCounter counter;
    auto binding = Salts::Binding::object(counter,
        SALTS_BIND_MEMBERS(NativeCounter, add, read, reset, fail, value, id));
    const salts_binding_function functions[] = {SALTS_BIND_FUNCTION(native_add), SALTS_BIND_FUNCTION(native_sum3)};
    const salts_binding_object objects[] = {binding.export_as("counter")};
    const salts_binding_module module{functions, 2, objects, 1};
    const salts_lua_limits lua_limits{8, 32, 4096};
    const salts_quickjs_limits js_limits{8, 32, 4096};
    lua_State *lua = luaL_newstate();
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *js = JS_NewContext(runtime);
    check_equal(salts_lua_push_module(lua, &module, lua_limits), CMETA_OK);
    lua_setglobal(lua, "native");
    check_equal(luaL_dostring(lua, "native.counter.value = 10; return native.counter:add(3, 4)"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), 17);
    check_equal(counter.value, 17);
    JSValue root = JS_UNDEFINED;
    check_equal(salts_quickjs_push_module(js, &module, js_limits, &root), CMETA_OK);
    JSValue global = JS_GetGlobalObject(js);
    check_equal(JS_SetPropertyStr(js, global, "native", root), 1);
    JS_FreeValue(js, global);
    JSValue result = eval(js, "native.counter.add(2, 3); native.native_sum3(native.counter.read(), 10, 10)");
    int32_t number = 0;
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &number, result), 0);
    check_equal(number, 42);
    check_equal(counter.value, 22);
    JS_FreeValue(js, result);
    check_equal(luaL_dostring(lua, "native.counter:reset(); return native.counter:read()"), LUA_OK);
    check_equal(counter.value, 0);
    check_not_equal(luaL_dostring(lua, "native.counter:add(1, {})"), LUA_OK);
    check_equal(counter.value, 0);
    result = eval(js, "native.counter.add(1)");
    check_true(JS_IsException(result));
    JS_FreeValue(js, JS_GetException(js));
    check_equal(counter.value, 0);
    check_not_equal(luaL_dostring(lua, "native.counter.id = 99"), LUA_OK);
    check_not_equal(luaL_dostring(lua, "native.counter:fail()"), LUA_OK);
    result = eval(js, "native.counter.fail()");
    check_true(JS_IsException(result));
    JS_FreeValue(js, JS_GetException(js));
    result = eval(js, "'use strict'; native.counter.id = 99");
    check_true(JS_IsException(result));
    JS_FreeValue(js, JS_GetException(js));

    std::string declaration;
    const DataBindTypeScriptOptions options{"Native", 32, 8, 128, 4096};
    check_equal(data_bind_typescript_emit(&module, &options,
        [](void *context, const char *text, size_t size) -> bool {
          static_cast<std::string *>(context)->append(text, size); return true;
        }, &declaration), CMETA_OK);
    check_contains(declaration.c_str(), "\"native_add\": (arg0: number, arg1: number) => number;");
    check_contains(declaration.c_str(), "\"value\": number; readonly \"id\": number;");
    check_contains(declaration.c_str(), "readonly \"reset\": () => void;");

    /* A retained method borrows the native instance even after its table is gone. */
    check_equal(luaL_dostring(lua, "saved = native.counter.add; native = nil; return saved(5, 6)"), LUA_OK);
    lua_gc(lua, LUA_GCCOLLECT);
    check_equal(counter.value, 11);
    result = eval(js, "var saved = native.counter.add; native = null; saved(1, 2)");
    check_false(JS_IsException(result));
    check_equal(counter.value, 14);
    JS_FreeValue(js, result);
    lua_close(lua);
    JS_FreeContext(js);
    JS_FreeRuntime(runtime);
  }

  it("calls script functions from C++ with typed arguments, void results and errors") {
    lua_State *lua = luaL_newstate();
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *js = JS_NewContext(runtime);
    Salts::Lua::Context lua_context{lua, {8, 32, 4096}};
    Salts::QuickJS::Context js_context{js, {8, 32, 4096}};
    int result = 0;
    check_equal(luaL_dostring(lua, "return function(a,b,c) return a+b+c end"), LUA_OK);
    int top = lua_gettop(lua);
    check_equal(Salts::Lua::call(lua_context, -1, result, 10, 20, 12), CMETA_OK);
    check_equal(result, 42);
    check_equal(lua_gettop(lua), top);
    JSValue function = eval(js, "(function(a,b,c) { return a+b+c; })");
    check_equal(Salts::QuickJS::call(js_context, function, result, 10, 20, 12), CMETA_OK);
    check_equal(result, 42);
    JS_FreeValue(js, function);
    check_equal(luaL_dostring(lua, "return function(x) script_value = x end"), LUA_OK);
    check_equal(Salts::Lua::call_void(lua_context, -1, 7), CMETA_OK);
    lua_getglobal(lua, "script_value");
    check_equal(lua_tointeger(lua, -1), 7);
    function = eval(js, "(function(x) { globalThis.script_value = x; })");
    check_equal(Salts::QuickJS::call_void(js_context, function, 8), CMETA_OK);
    JS_FreeValue(js, function);
    function = eval(js, "(function() { throw new Error('script failure'); })");
    result = 123;
    check_equal(Salts::QuickJS::call(js_context, function, result), CMETA_CALLBACK_ERROR);
    check_equal(result, 123);
    JS_FreeValue(js, JS_GetException(js));
    JS_FreeValue(js, function);
    JSValue wrong_type = eval(js, "(function() { return 'bad'; })");
    check_not_equal(Salts::QuickJS::call(js_context, wrong_type, result), CMETA_OK);
    check_equal(result, 123);
    JS_FreeValue(js, wrong_type);
    check_equal(luaL_dostring(lua, "return function() return {} end"), LUA_OK);
    check_not_equal(Salts::Lua::call(lua_context, -1, result), CMETA_OK);
    check_equal(result, 123);
    check_equal(luaL_dostring(lua, "return function() return nil + 1 end"), LUA_OK);
    top = lua_gettop(lua);
    check_equal(Salts::Lua::call(lua_context, -1, result), CMETA_CALLBACK_ERROR);
    check_equal(lua_gettop(lua), top);
    check_equal(result, 123);
    lua_close(lua);
    JS_FreeContext(js);
    JS_FreeRuntime(runtime);
  }
}
