#ifndef SALTS_BINDINGS_QUICKJS_NATIVE_HPP
#define SALTS_BINDINGS_QUICKJS_NATIVE_HPP
#include <salts/bindings/native.hpp>
#include <salts/bindings/quickjs.hpp>
#include <salts/bindings/quickjs/module.h>

namespace Salts::QuickJS {
template <typename R, typename... A>
cmeta_status call(const Context &context, JSValueConst function, R &result, const A &...arguments) {
  const std::array<const void *, sizeof...(A)> values{&arguments...};
  return salts_quickjs_call_script(context.native_handle(), function,
      &Binding::detail::Signature<R, A...>::data, &result, values.data(), values.size(), context.limits());
}
template <typename... A>
cmeta_status call_void(const Context &context, JSValueConst function, const A &...arguments) {
  const std::array<const void *, sizeof...(A)> values{&arguments...};
  return salts_quickjs_call_script(context.native_handle(), function,
      &Binding::detail::Signature<void, A...>::data, nullptr, values.data(), values.size(), context.limits());
}
}
#endif
