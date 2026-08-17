/**
 * @file turbo_lua_bind.hpp
 * @brief Unified C++ facade for TurboParser Lua integration.
 *
 * The C facade supplies stack, DataBind, and generated typed-record adapters.
 * cpp_lua_bind.hpp adds C++ functions, classes, properties, inheritance, and
 * reflected bindings. Schema-generated operations remain the service boundary
 * and can be registered in the same lua_State.
 */
#pragma once

/* lua.hpp must establish C linkage before the C facade reaches lua.h. */
#include "cpp/cpp_lua_bind.hpp"
#include "turbo_lua_bind.h"
