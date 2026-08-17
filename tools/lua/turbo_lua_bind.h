/**
 * @file turbo_lua_bind.h
 * @brief Unified C facade for Lua values, DataBind values, and typed records.
 *
 * Include this header for application-facing C/Lua integration. The narrower
 * c11_lua_bind.h, c11_lua_data_bind.h, and c11_lua_typed.h headers remain
 * available when a consumer intentionally wants fewer dependencies.
 *
 * The low-level binding and executor APIs borrow lua_State and DataBind objects.
 * turbo_lua_worker_t is the optional managed boundary that owns a dedicated Lua
 * state/thread. Ownership and stack behavior otherwise remain defined by the
 * underlying APIs.
 */
#ifndef TURBO_LUA_BIND_H
#define TURBO_LUA_BIND_H

#include "c11_lua_bind.h"
#include "c11_lua_data_bind.h"
#include "c11_lua_typed.h"
#include "turbo_lua_executor.h"
#include "turbo_lua_worker.h"

#endif /* TURBO_LUA_BIND_H */
