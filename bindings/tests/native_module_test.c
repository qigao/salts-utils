#include "fixtures/native_math.h"
#include <salts/bindings/lua/module.h>
#include <salts/bindings/quickjs/module.h>
#include <data_bind_typescript.h>
#include <cmeta/cmeta.h>
#include <lauxlib.h>
#include <lua.h>
#include <string.h>
#include "tinytest.h"

FunctionDecl(value, double, native_sum,
    (double, left, CMETA_PARAM_IN), (double, right, CMETA_PARAM_IN));

typed_any(value, double, native_sum_adapter, (double left, double right)) {
  return native_sum(left, right);
}

static const cmeta_data_desc *const sum_params[] = {&cmeta_data_double, &cmeta_data_double};
static const cmeta_function_data_desc sum_data = {
    sizeof(cmeta_function_data_desc), FunctionMeta(native_sum),
    &cmeta_data_double, sum_params, 2};

enum { TEST_DEPTH = 8, TEST_ITEMS = 32, TEST_BYTES = 4096 };
typedef struct Output {
  char text[TEST_BYTES];
  size_t size;
  int fail;
} Output;

static bool write_output(void *context, const char *text, size_t size) {
  Output *output = context;
  if (output->fail || size >= sizeof(output->text) - output->size) return false;
  memcpy(output->text + output->size, text, size);
  output->size += size;
  output->text[output->size] = 0;
  return true;
}

typedef struct FailingAllocator {
  lua_Alloc original;
  void *context;
} FailingAllocator;

static void *fail_allocation(void *context, void *pointer, size_t old_size, size_t new_size) {
  FailingAllocator *allocator = context;
  if (new_size != 0 && (pointer == NULL || new_size > old_size)) return NULL;
  return allocator->original(allocator->context, pointer, old_size, new_size);
}

spec("Existing C source native module exports") {
  static lua_State *lua;
  static JSRuntime *runtime;
  static JSContext *js;
  static cmeta_invokable sum;
  static salts_binding_function exports[2];
  static salts_binding_module module;
  static Output output;
  static DataBindTypeScriptOptions typescript;
  const salts_lua_limits lua_limits = {TEST_DEPTH, TEST_ITEMS, TEST_BYTES};
  const salts_quickjs_limits js_limits = {TEST_DEPTH, TEST_ITEMS, TEST_BYTES};

  before_each() {
    sum = (cmeta_invokable)CMETA_INVOKABLE_INIT;
    check_equal(cmeta_invokable_bind_data(&sum_data, native_sum_adapter, &sum), CMETA_OK);
    exports[0] = (salts_binding_function){"sum", &sum};
    module = (salts_binding_module){exports, 1, NULL, 0};
    memset(&output, 0, sizeof(output));
    typescript = (DataBindTypeScriptOptions){"Native", TEST_ITEMS, TEST_DEPTH, TEST_ITEMS, TEST_BYTES};
    lua = luaL_newstate();
    check_not_null(lua);
    runtime = JS_NewRuntime();
    check_not_null(runtime);
    js = JS_NewContext(runtime);
    check_not_null(js);
  }
  after_each() {
    if (lua != NULL) lua_close(lua);
    if (js != NULL) JS_FreeContext(js);
    if (runtime != NULL) JS_FreeRuntime(runtime);
    lua = NULL; js = NULL; runtime = NULL;
  }

  it("calls the existing C implementation from both VMs and emits its TS signature") {
    JSValue value = JS_UNDEFINED, global, result;
    int32_t number = 0;
    static const char expression[] = "native.sum(20, 22)";
    check_equal(salts_lua_push_module(lua, &module, lua_limits), CMETA_OK);
    lua_setglobal(lua, "native");
    check_equal(luaL_dostring(lua, "return native.sum(20, 22)"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), (lua_Integer)42);
    check_equal(salts_quickjs_push_module(js, &module, js_limits, &value), CMETA_OK);
    global = JS_GetGlobalObject(js);
    check_equal(JS_SetPropertyStr(js, global, "native", value), 1);
    JS_FreeValue(js, global);
    result = JS_Eval(js, expression, sizeof(expression) - 1, "native.js", JS_EVAL_TYPE_GLOBAL);
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &number, result), 0);
    check_equal(number, native_sum(20, 22));
    JS_FreeValue(js, result);
    check_equal(data_bind_typescript_emit(&module, &typescript, write_output, &output), CMETA_OK);
    check_equal(output.text,
        "export interface NativeBindings {\n  \"sum\": (arg0: number, arg1: number) => number;\n}\n");
  }

  it("copies binding records so extracted functions survive module table collection") {
    JSValue value = JS_UNDEFINED, saved, result;
    double number = 0;
    JSValue arguments[] = {JS_NewInt32(js, 3), JS_NewInt32(js, 4)};
    check_equal(salts_quickjs_push_module(js, &module, js_limits, &value), CMETA_OK);
    saved = JS_GetPropertyStr(js, value, "sum");
    JS_FreeValue(js, value);
    JS_RunGC(runtime);
    check_equal(salts_lua_push_module(lua, &module, lua_limits), CMETA_OK);
    lua_setglobal(lua, "native");
    check_equal(luaL_dostring(lua, "saved = native.sum; native = nil"), LUA_OK);
    lua_gc(lua, LUA_GCCOLLECT);
    /* Metadata is static; only the registration-time handle/rows are gone. */
    memset(&sum, 0, sizeof(sum));
    memset(exports, 0, sizeof(exports));
    check_equal(luaL_dostring(lua, "return saved(3, 4)"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), (lua_Integer)7);
    result = JS_Call(js, saved, JS_UNDEFINED, 2, arguments);
    check_false(JS_IsException(result));
    check_equal(JS_ToFloat64(js, &number, result), 0);
    check_equal(number, 7.0);
    JS_FreeValue(js, result);
    JS_FreeValue(js, saved);
    JS_FreeValue(js, arguments[0]);
    JS_FreeValue(js, arguments[1]);
  }

  it("restores publication outputs when VM allocation fails") {
    FailingAllocator allocator;
    JSValue value = JS_UNDEFINED, error;
    allocator.original = lua_getallocf(lua, &allocator.context);
    lua_pushinteger(lua, 123);
    lua_setallocf(lua, fail_allocation, &allocator);
    cmeta_status status = salts_lua_push_module(lua, &module, lua_limits);
    lua_setallocf(lua, allocator.original, allocator.context);
    check_equal(status, CMETA_OUT_OF_MEMORY);
    check_equal(lua_gettop(lua), 1);
    check_equal(lua_tointeger(lua, 1), (lua_Integer)123);
    check_equal(salts_lua_push_module(lua, &module, lua_limits), CMETA_OK);
    JS_SetMemoryLimit(runtime, 1);
    status = salts_quickjs_push_module(js, &module, js_limits, &value);
    JS_SetMemoryLimit(runtime, (size_t)-1);
    check_equal(status, CMETA_OUT_OF_MEMORY);
    check_true(JS_IsUndefined(value));
    error = JS_GetException(js);
    JS_FreeValue(js, error);
    check_equal(salts_quickjs_push_module(js, &module, js_limits, &value), CMETA_OK);
    JS_FreeValue(js, value);
  }

  it("rejects duplicate names before publishing values or declaration bytes") {
    JSValue value = JS_UNDEFINED;
    exports[1] = exports[0];
    module.function_count = 2;
    check_equal(salts_lua_push_module(lua, &module, lua_limits), CMETA_INVALID_ARGUMENT);
    check_equal(lua_gettop(lua), 0);
    check_equal(salts_quickjs_push_module(js, &module, js_limits, &value), CMETA_INVALID_ARGUMENT);
    check_true(JS_IsUndefined(value));
    check_equal(data_bind_typescript_emit(&module, &typescript, write_output, &output), CMETA_INVALID_ARGUMENT);
    check_equal(output.size, (size_t)0);
  }

  it("bounds registration and preflights the complete declaration before writing") {
    salts_lua_limits small = lua_limits;
    small.max_items = 0;
    check_equal(salts_lua_push_module(lua, &module, small), CMETA_CAPACITY_EXCEEDED);
    typescript.max_output_bytes = 8;
    check_equal(data_bind_typescript_emit(&module, &typescript, write_output, &output), CMETA_CAPACITY_EXCEEDED);
    check_equal(output.size, (size_t)0);
    typescript.max_output_bytes = TEST_BYTES;
    output.fail = 1;
    check_equal(data_bind_typescript_emit(&module, &typescript, write_output, &output), CMETA_CALLBACK_ERROR);
  }

  it("quotes arbitrary export keys without turning them into TS syntax") {
    exports[0].name = "sum\"\\\n";
    check_equal(data_bind_typescript_emit(&module, &typescript, write_output, &output), CMETA_OK);
    check_contains(output.text, "\"sum\\\"\\\\\\u000a\":");
  }

  it("rejects unsupported names and invalid interface prefixes before writing") {
    exports[0].name = "\xc3\xa9";
    check_equal(data_bind_typescript_emit(&module, &typescript, write_output, &output), CMETA_TRAIT_MISSING);
    check_equal(output.size, (size_t)0);
    exports[0].name = "sum";
    typescript.name_prefix = "invalid-prefix";
    check_equal(data_bind_typescript_emit(&module, &typescript, write_output, &output), CMETA_INVALID_ARGUMENT);
    check_equal(output.size, (size_t)0);
  }

  it("propagates argument errors through ordinary Lua and JS exceptions") {
    JSValue value = JS_UNDEFINED, global, result;
    static const char expression[] = "native.sum('bad', 22)";
    check_equal(salts_lua_push_module(lua, &module, lua_limits), CMETA_OK);
    lua_setglobal(lua, "native");
    check_not_equal(luaL_dostring(lua, "return native.sum({}, 22)"), LUA_OK);
    lua_settop(lua, 0);
    check_equal(luaL_dostring(lua, "return native.sum(1, 2)"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), (lua_Integer)3);
    check_equal(salts_quickjs_push_module(js, &module, js_limits, &value), CMETA_OK);
    global = JS_GetGlobalObject(js);
    check_equal(JS_SetPropertyStr(js, global, "native", value), 1);
    JS_FreeValue(js, global);
    result = JS_Eval(js, expression, sizeof(expression) - 1, "bad.js", JS_EVAL_TYPE_GLOBAL);
    check_true(JS_IsException(result));
    result = JS_GetException(js);
    check_true(JS_IsError(result));
    JS_FreeValue(js, result);
  }
}
