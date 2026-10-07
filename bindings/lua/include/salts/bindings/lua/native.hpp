#ifndef SALTS_BINDINGS_LUA_NATIVE_HPP
#define SALTS_BINDINGS_LUA_NATIVE_HPP
#include <salts/bindings/native.hpp>
#include <salts/bindings/lua.hpp>
#include <salts/bindings/lua/module.h>

namespace Salts::Lua {
template <typename R, typename... A>
cmeta_status call(const Context &context, int function_index, R &result, const A &...arguments) {
  const std::array<const void *, sizeof...(A)> values{&arguments...};
  return salts_lua_call_script(context.native_handle(), function_index,
      &Binding::detail::Signature<R, A...>::data, &result, values.data(), values.size(), context.limits());
}
template <typename... A>
cmeta_status call_void(const Context &context, int function_index, const A &...arguments) {
  const std::array<const void *, sizeof...(A)> values{&arguments...};
  return salts_lua_call_script(context.native_handle(), function_index,
      &Binding::detail::Signature<void, A...>::data, nullptr, values.data(), values.size(), context.limits());
}
}
#endif
