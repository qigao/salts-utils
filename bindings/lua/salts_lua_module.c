#include <salts/bindings/lua/module.h>
#include <lauxlib.h>
#include <lua.h>
#include <string.h>
#include <limits.h>

typedef struct LuaFunction {
  salts_binding_function binding;
  cmeta_invokable invokable;
  salts_lua_limits limits;
} LuaFunction;

static int module_function(lua_State *state) {
  LuaFunction *function = lua_touserdata(state, lua_upvalueindex(1));
  int results = 0;
  int first = lua_type(state, lua_upvalueindex(2)) != LUA_TNONE &&
      lua_rawequal(state, 1, lua_upvalueindex(2)) ? 2 : 1;
  cmeta_status status = salts_lua_call_binding(state, &function->binding,
      first, (size_t)(lua_gettop(state) - first + 1), function->limits, &results);
  if (status != CMETA_OK)
    return luaL_error(state, "native function invocation failed (%d)", (int)status);
  return results;
}

static void push_function(lua_State *state, const salts_binding_function *binding,
    salts_lua_limits limits, int receiver) {
  LuaFunction *function = lua_newuserdatauv(state, sizeof(*function), 0);
  function->binding = *binding;
  if (binding->invokable != NULL) {
    function->invokable = *binding->invokable;
    function->binding.invokable = &function->invokable;
  }
  function->limits = limits;
  if (receiver != 0) lua_pushvalue(state, receiver);
  lua_pushcclosure(state, module_function, receiver != 0 ? 2 : 1);
}

typedef struct LuaNativeObject {
  salts_binding_native_object object;
  salts_lua_limits limits;
} LuaNativeObject;

#define SALTS_LUA_NATIVE_OBJECT "salts.bindings.native_object"

static int native_index(lua_State *state) {
  LuaNativeObject *proxy = luaL_checkudata(state, 1, SALTS_LUA_NATIVE_OBJECT);
  const char *name = luaL_checkstring(state, 2);
  size_t i;
  for (i = 0; i < proxy->object.method_count; ++i) {
    salts_binding_function binding = proxy->object.methods[i];
    if (strcmp(binding.name, name) != 0) continue;
    binding.context = proxy->object.instance;
    push_function(state, &binding, proxy->limits, 1);
    return 1;
  }
  for (i = 0; i < proxy->object.property_count; ++i) {
    const salts_binding_property *property = &proxy->object.properties[i];
    salts_binding_function binding = {property->name, NULL, property->get, proxy->object.instance};
    int results = 0;
    cmeta_status status;
    if (strcmp(property->name, name) != 0) continue;
    status = salts_lua_call_binding(state, &binding, 1, 0, proxy->limits, &results);
    if (status != CMETA_OK) return luaL_error(state, "native property read failed (%d)", (int)status);
    return results;
  }
  return luaL_error(state, "unknown native member: %s", name);
}

static int native_newindex(lua_State *state) {
  LuaNativeObject *proxy = luaL_checkudata(state, 1, SALTS_LUA_NATIVE_OBJECT);
  const char *name = luaL_checkstring(state, 2);
  size_t i;
  for (i = 0; i < proxy->object.property_count; ++i) {
    const salts_binding_property *property = &proxy->object.properties[i];
    salts_binding_function binding = {property->name, NULL, property->set, proxy->object.instance};
    cmeta_status status;
    if (strcmp(property->name, name) != 0) continue;
    if (property->set == NULL) return luaL_error(state, "read-only native member: %s", name);
    status = salts_lua_call_binding(state, &binding, 3, 1, proxy->limits, NULL);
    if (status != CMETA_OK) return luaL_error(state, "native property write failed (%d)", (int)status);
    return 0;
  }
  return luaL_error(state, "unknown or read-only native member: %s", name);
}

static void push_native_object(lua_State *state, const salts_binding_native_object *object,
    salts_lua_limits limits) {
  LuaNativeObject *proxy;
  if (luaL_newmetatable(state, SALTS_LUA_NATIVE_OBJECT)) {
    lua_pushcfunction(state, native_index);
    lua_setfield(state, -2, "__index");
    lua_pushcfunction(state, native_newindex);
    lua_setfield(state, -2, "__newindex");
    lua_pushliteral(state, "native object");
    lua_setfield(state, -2, "__metatable");
  }
  proxy = lua_newuserdatauv(state, sizeof(*proxy), 0);
  proxy->object = *object;
  proxy->limits = limits;
  lua_pushvalue(state, -2);
  lua_setmetatable(state, -2);
  lua_remove(state, -2);
}

typedef struct LuaModule {
  const salts_binding_module *module;
  salts_lua_limits limits;
  cmeta_status status;
} LuaModule;

static int module_build(lua_State *state) {
  LuaModule *build = lua_touserdata(state, 1);
  const salts_binding_module *module = build->module;
  size_t i;
  lua_newtable(state);
  for (i = 0; i < module->function_count; ++i) {
    lua_pushstring(state, module->functions[i].name);
    push_function(state, &module->functions[i], build->limits, 0);
    lua_rawset(state, -3);
  }
  for (i = 0; i < module->object_count; ++i) {
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    if (module->objects[i].native != NULL) {
      lua_pushstring(state, module->objects[i].name);
      push_native_object(state, module->objects[i].native, build->limits);
      lua_rawset(state, -3);
      continue;
    }
    build->status = salts_binding_object_borrow(module->objects[i].object, &object);
    if (build->status != CMETA_OK) return 0;
    lua_pushstring(state, module->objects[i].name);
    build->status = salts_lua_push_object(state, &object, build->limits);
    cmeta_object_release(&object);
    if (build->status != CMETA_OK) return 0;
    lua_rawset(state, -3);
  }
  return 1;
}

cmeta_status salts_lua_push_module(lua_State *state,
    const salts_binding_module *module, salts_lua_limits limits) {
  LuaModule build = {module, limits, CMETA_OK};
  int top, status;
  if (state == NULL) return CMETA_INVALID_ARGUMENT;
  build.status = salts_binding_module_validate(module, limits.max_items);
  if (build.status != CMETA_OK) return build.status;
  if (!lua_checkstack(state, LUA_MINSTACK)) return CMETA_OUT_OF_MEMORY;
  top = lua_gettop(state);
  lua_pushcfunction(state, module_build);
  lua_pushlightuserdata(state, &build);
  status = lua_pcall(state, 1, 1, 0);
  if (status != LUA_OK || build.status != CMETA_OK) {
    lua_settop(state, top);
    return status != LUA_OK
        ? (status == LUA_ERRMEM ? CMETA_OUT_OF_MEMORY : CMETA_CALLBACK_ERROR)
        : build.status;
  }
  return CMETA_OK;
}

typedef struct LuaScriptCall {
  const cmeta_function_data_desc *data;
  void *result;
  const void *const *arguments;
  salts_lua_limits limits;
  cmeta_status status;
} LuaScriptCall;

static int script_call(lua_State *state) {
  LuaScriptCall *call = lua_touserdata(state, 1);
  size_t i;
  if (!lua_checkstack(state, (int)call->data->param_count + LUA_MINSTACK)) {
    call->status = CMETA_OUT_OF_MEMORY;
    return 0;
  }
  for (i = 0; i < call->data->param_count; ++i) {
    call->status = salts_lua_push_cmeta(state, call->data->params[i], call->arguments[i], call->limits);
    if (call->status != CMETA_OK) return 0;
  }
  lua_call(state, (int)call->data->param_count, call->data->return_data != NULL ? 1 : 0);
  if (call->data->return_data != NULL)
    call->status = salts_lua_read_cmeta(state, -1, call->data->return_data, call->result, call->limits);
  return 0;
}

cmeta_status salts_lua_call_script(lua_State *state, int index,
    const cmeta_function_data_desc *signature, void *result,
    const void *const *arguments, size_t count, salts_lua_limits limits) {
  LuaScriptCall call = {signature, result, arguments, limits, CMETA_OK};
  int top, status;
  size_t i;
  if (state == NULL || !cmeta_function_data_desc_valid(signature) ||
      count != signature->param_count || (count != 0 && arguments == NULL) ||
      (signature->return_data != NULL && result == NULL) || !lua_isfunction(state, index))
    return CMETA_INVALID_ARGUMENT;
  if (count > limits.max_items || count > INT_MAX - LUA_MINSTACK) return CMETA_CAPACITY_EXCEEDED;
  for (i = 0; i < count; ++i) {
    const cmeta_param_desc *param = cmeta_function_param(signature->function, i);
    if (arguments[i] == NULL) return CMETA_INVALID_ARGUMENT;
    if ((param->flags & CMETA_PARAM_DIRECTION_MASK) != CMETA_PARAM_IN ||
        (param->flags & CMETA_PARAM_OWNED) != 0) return CMETA_TRAIT_MISSING;
  }
  if (!lua_checkstack(state, (int)count + LUA_MINSTACK)) return CMETA_OUT_OF_MEMORY;
  index = lua_absindex(state, index);
  top = lua_gettop(state);
  lua_pushcfunction(state, script_call);
  lua_pushlightuserdata(state, &call);
  lua_pushvalue(state, index);
  status = lua_pcall(state, 2, 0, 0);
  lua_settop(state, top);
  return status == LUA_OK ? call.status :
      status == LUA_ERRMEM ? CMETA_OUT_OF_MEMORY : CMETA_CALLBACK_ERROR;
}
