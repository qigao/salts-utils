#ifndef SALTS_BINDINGS_NATIVE_HPP
#define SALTS_BINDINGS_NATIVE_HPP

#include <salts/bindings/module.h>
#include <array>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Salts::Binding {

/* Specialize Data<T> for a non-scalar value using its canonical conversion
 * provider. The original library never needs to include this header. */
template <typename T> struct Data {
  static const cmeta_data_desc *get() {
    static_assert(std::is_arithmetic_v<T> || std::is_void_v<T>,
                  "Supply Salts::Binding::Data<T> for this native value type");
    if constexpr (std::is_void_v<T>) return nullptr;
    else if constexpr (std::is_same_v<T, bool>) return &cmeta_data_bool;
    else if constexpr (std::is_same_v<T, int>) return &cmeta_data_int;
    else if constexpr (std::is_same_v<T, long>) return &cmeta_data_long;
    else if constexpr (std::is_same_v<T, float>) return &cmeta_data_float;
    else if constexpr (std::is_same_v<T, double>) return &cmeta_data_double;
    else {
      static_assert(std::is_integral_v<T> && sizeof(T) <= sizeof(uint64_t),
                    "Native scalar representation is not supported");
      if constexpr (std::is_signed_v<T>) {
        if constexpr (sizeof(T) == sizeof(int8_t)) return &cmeta_data_int8;
        else if constexpr (sizeof(T) == sizeof(int16_t)) return &cmeta_data_int16;
        else if constexpr (sizeof(T) == sizeof(int32_t)) return &cmeta_data_int32;
        else return &cmeta_data_int64;
      } else {
        if constexpr (sizeof(T) == sizeof(uint8_t)) return &cmeta_data_uint8;
        else if constexpr (sizeof(T) == sizeof(uint16_t)) return &cmeta_data_uint16;
        else if constexpr (sizeof(T) == sizeof(uint32_t)) return &cmeta_data_uint32;
        else return &cmeta_data_uint64;
      }
    }
  }
};

namespace detail {
inline constexpr std::size_t max_parameters = 16;
template <typename T> using Value = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T> Value<T> argument(const void *storage) {
  if constexpr (std::is_trivially_copyable_v<Value<T>>) {
    Value<T> value;
    std::memcpy(&value, storage, sizeof(value));
    return value;
  } else return *static_cast<const Value<T> *>(storage);
}

template <typename R, typename... A> struct Signature {
  static_assert(sizeof...(A) <= max_parameters, "Native binding supports at most 16 arguments");
  static_assert(!std::is_reference_v<R> && (!std::is_reference_v<A> && ...),
                "Reference arguments/results require an explicit ownership adapter");
  inline static const std::array<const cmeta_data_desc *, sizeof...(A)> values{Data<Value<A>>::get()...};
  inline static constexpr auto names = [] {
    std::array<std::array<char, 6>, sizeof...(A)> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
      result[i] = {'a', 'r', 'g', static_cast<char>('0' + i / 10), static_cast<char>('0' + i % 10), 0};
    return result;
  }();
  inline static const auto params = [] {
    std::array<cmeta_param_desc, sizeof...(A)> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
      result[i] = {sizeof(cmeta_param_desc), names[i].data(), values[i]->storage_type, CMETA_PARAM_IN};
    return result;
  }();
  inline static const cmeta_function_desc function{
      sizeof(cmeta_function_desc), "native", std::is_void_v<R> ? &cmeta_type_void : Data<Value<R>>::get()->storage_type,
      params.data(), sizeof...(A), CMETA_EFFECT_UNKNOWN | CMETA_EFFECT_MAY_FAIL, CMETA_PROP_NONE, CMETA_RESULT_UNKNOWN};
  inline static const cmeta_function_data_desc data{
      sizeof(cmeta_function_data_desc), &function, Data<Value<R>>::get(), values.data(), sizeof...(A)};
};

template <typename R, typename Call> cmeta_status result(void *out, Call &&call) {
  try {
    if constexpr (std::is_void_v<R>) call();
    else if constexpr (std::is_trivially_copyable_v<R>) {
      R value = call();
      std::memcpy(out, &value, sizeof(value));
    } else *static_cast<R *>(out) = call();
    return CMETA_OK;
  } catch (...) { return CMETA_CALLBACK_ERROR; }
}

template <auto Function, typename Type = std::remove_cv_t<decltype(Function)>> struct Thunk;

template <auto Function, typename R, typename... A>
struct Thunk<Function, R (*)(A...)> {
  template <std::size_t... I> static cmeta_status call(void *out,
      const void *const *args, std::index_sequence<I...>) {
    return result<R>(out, [&]() -> R { return Function(argument<A>(args[I])...); });
  }
  static cmeta_status invoke(void *, void *out, const void *const *args, std::size_t count) {
    if (count != sizeof...(A)) return CMETA_INVALID_ARGUMENT;
    return call(out, args, std::index_sequence_for<A...>{});
  }
  inline static const salts_binding_native native{&Signature<R, A...>::data, invoke};
};
template <auto Function, typename R, typename... A>
struct Thunk<Function, R (*)(A...) noexcept> : Thunk<Function, R (*)(A...)> {};

template <auto Member, typename C, typename R, typename... A> struct Method {
  using Class = C;
  template <std::size_t... I> static cmeta_status call(C *object, void *out,
      const void *const *args, std::index_sequence<I...>) {
    return result<R>(out, [&]() -> R { return (object->*Member)(argument<A>(args[I])...); });
  }
  static cmeta_status invoke(void *context, void *out, const void *const *args, std::size_t count) {
    if (context == nullptr || count != sizeof...(A)) return CMETA_INVALID_ARGUMENT;
    return call(static_cast<C *>(context), out, args, std::index_sequence_for<A...>{});
  }
  inline static const salts_binding_native native{&Signature<R, A...>::data, invoke};
};
template <auto M, typename C, typename R, typename... A>
struct Thunk<M, R (C::*)(A...)> : Method<M, C, R, A...> {};
template <auto M, typename C, typename R, typename... A>
struct Thunk<M, R (C::*)(A...) const> : Method<M, C, R, A...> {};
template <auto M, typename C, typename R, typename... A>
struct Thunk<M, R (C::*)(A...) noexcept> : Method<M, C, R, A...> {};
template <auto M, typename C, typename R, typename... A>
struct Thunk<M, R (C::*)(A...) const noexcept> : Method<M, C, R, A...> {};

template <auto M, typename C, typename T> struct Thunk<M, T C::*> {
  using Class = C;
  using Type = Value<T>;
  static_assert(!std::is_volatile_v<T>, "Volatile fields require an explicit adapter");
  static cmeta_status get(void *context, void *out, const void *const *, std::size_t count) {
    if (context == nullptr || count != 0) return CMETA_INVALID_ARGUMENT;
    return result<Type>(out, [&] { return static_cast<C *>(context)->*M; });
  }
  static cmeta_status set(void *context, void *, const void *const *args, std::size_t count) {
    if (context == nullptr || count != 1) return CMETA_INVALID_ARGUMENT;
    if constexpr (std::is_const_v<T>) return CMETA_TRAIT_MISSING;
    else return result<void>(nullptr, [&] { static_cast<C *>(context)->*M = argument<Type>(args[0]); });
  }
  inline static const salts_binding_native getter{&Signature<Type>::data, get};
  inline static const salts_binding_native setter{&Signature<void, Type>::data, set};
  static salts_binding_property property(const char *name) {
    return {name, &getter, std::is_const_v<T> ? nullptr : &setter};
  }
};
} // namespace detail

template <auto Function> salts_binding_function function(const char *name) {
  static_assert(std::is_pointer_v<decltype(Function)>, "Use object() for member functions");
  return {name, nullptr, &detail::Thunk<Function>::native, nullptr};
}

template <auto Pointer> struct Member {
  const char *name;
  static constexpr auto pointer = Pointer;
};
template <auto Pointer> constexpr Member<Pointer> member(const char *name) { return {name}; }

/* This owner keeps member tables stable. The native instance remains borrowed;
 * both must outlive all VM proxies and extracted methods. */
template <typename C, typename... E> class Object {
  static constexpr std::size_t method_count = (std::size_t{0} + ... + std::is_member_function_pointer_v<decltype(E::pointer)>);
  std::array<salts_binding_function, method_count> methods_{};
  std::array<salts_binding_property, sizeof...(E) - method_count> properties_{};
  salts_binding_native_object native_{};

  template <typename Entry> void add(Entry entry, std::size_t &method, std::size_t &property) {
    using Adapter = detail::Thunk<Entry::pointer>;
    static_assert(std::is_same_v<C, typename Adapter::Class>, "Member must belong to this native class");
    if constexpr (std::is_member_function_pointer_v<decltype(Entry::pointer)>)
      methods_[method++] = {entry.name, nullptr, &Adapter::native, nullptr};
    else properties_[property++] = Adapter::property(entry.name);
  }
public:
  Object(C &instance, E... entries) {
    std::size_t method = 0, property = 0;
    (add(entries, method, property), ...);
    native_ = {&instance, methods_.data(), methods_.size(), properties_.data(), properties_.size()};
  }
  Object(const Object &) = delete;
  Object &operator=(const Object &) = delete;
  Object(Object &&) = delete;
  Object &operator=(Object &&) = delete;
  salts_binding_object export_as(const char *name) const { return {name, nullptr, &native_}; }
};
template <typename C, typename... E> auto object(C &instance, E... entries) {
  return Object<C, E...>{instance, entries...};
}

} // namespace Salts::Binding

#define SALTS_BIND_FUNCTION(name) ::Salts::Binding::function<&name>(#name)
#define SALTS_BIND_DETAIL_MEMBER(item, Class) ::Salts::Binding::member<&Class::item>(#item)
#define SALTS_BIND_MEMBERS(Class, ...) CMETA_PP_MAP_COMMA(SALTS_BIND_DETAIL_MEMBER, Class, __VA_ARGS__)

#endif
