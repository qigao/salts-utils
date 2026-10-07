#ifndef SALTS_BINDINGS_LUA_MODULE_H
#define SALTS_BINDINGS_LUA_MODULE_H
#include <salts/bindings/module.h>
#include <salts/bindings/lua/cmeta.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Push one module table of native functions and borrowed object proxies.
 * On failure the original stack is restored. No globals are modified.
 * limits.max_items also bounds exports. Retained functions/proxies follow the
 * external owner lifetime documented in salts_binding_module. Lua functions
 * translate cmeta_status failures into Lua errors, after normal call cleanup.
 */
cmeta_status salts_lua_push_module(lua_State *state,
    const salts_binding_module *module, salts_lua_limits limits);

/* Convert and invoke an admitted native/generated binding. No signature catalog
 * is consulted for exact adapters. Failure returns no result count. */
cmeta_status salts_lua_call_binding(lua_State *state,
    const salts_binding_function *binding, int first_argument,
    size_t argument_count, salts_lua_limits limits, int *out_result_count);

/* Call a Lua function at index using the generated signature. Native output
 * must be semantic-zero storage. Restores the Lua stack on every exit. */
cmeta_status salts_lua_call_script(lua_State *state, int index,
    const cmeta_function_data_desc *signature, void *result,
    const void *const *arguments, size_t count, salts_lua_limits limits);

#ifdef __cplusplus
}
#endif
#endif
