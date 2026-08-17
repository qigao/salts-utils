#pragma once

#include <turbo_parser.h>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <istream>
#include <iterator>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <cstring>

namespace turbo::json {

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class ParseError final : public Error {
 public:
  using Error::Error;
};

class TypeError final : public Error {
 public:
  using Error::Error;
};

template <typename T>
struct IsVector : std::false_type {};

template <typename T, typename Allocator>
struct IsVector<std::vector<T, Allocator>> : std::true_type {};

class Value {
 public:
  using Array = std::vector<Value>;
  using Object = std::vector<std::pair<std::string, Value>>;
  using exception = Error;
  using parse_error = ParseError;

 private:
  struct Discarded {
    friend bool operator==(Discarded, Discarded) { return true; }
  };
  using Storage =
      std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double, std::string, Array,
                   Object, Discarded>;

 public:
  template <bool IsConst>
  class BasicIterator {
   public:
    using Owner = std::conditional_t<IsConst, const Value, Value>;
    using Reference = std::conditional_t<IsConst, const Value&, Value&>;
    using Pointer = std::conditional_t<IsConst, const Value*, Value*>;
    using difference_type = std::ptrdiff_t;
    using value_type = Value;
    using iterator_category = std::forward_iterator_tag;

    BasicIterator() = default;

    template <bool OtherConst, typename = std::enable_if_t<IsConst || !OtherConst>>
    BasicIterator(const BasicIterator<OtherConst>& other)
        : owner_(other.owner_), index_(other.index_) {}

    Reference operator*() const { return owner_->ElementAt(index_); }
    Pointer operator->() const { return &owner_->ElementAt(index_); }

    BasicIterator& operator++() {
      ++index_;
      return *this;
    }

    BasicIterator operator++(int) {
      BasicIterator copy = *this;
      ++(*this);
      return copy;
    }

    friend bool operator==(const BasicIterator& lhs, const BasicIterator& rhs) {
      return lhs.owner_ == rhs.owner_ && lhs.index_ == rhs.index_;
    }

    friend bool operator!=(const BasicIterator& lhs, const BasicIterator& rhs) {
      return !(lhs == rhs);
    }

    [[nodiscard]] std::string_view key() const {
      if (owner_ == nullptr || !owner_->is_object()) {
        throw TypeError("JSON iterator has no object key");
      }
      return std::get<Object>(owner_->storage_).at(index_).first;
    }

    Reference value() const { return operator*(); }

   private:
    friend class Value;
    template <bool>
    friend class BasicIterator;

    BasicIterator(Owner* owner, std::size_t index) : owner_(owner), index_(index) {}

    Owner* owner_ = nullptr;
    std::size_t index_ = 0;
  };

  using iterator = BasicIterator<false>;
  using const_iterator = BasicIterator<true>;

  Value() = default;
  Value(std::nullptr_t) : storage_(nullptr) {}
  Value(bool value) : storage_(value) {}
  Value(const char* value) : storage_(value == nullptr ? Storage(nullptr) : Storage(std::string(value))) {}
  Value(std::string value) : storage_(std::move(value)) {}
  Value(std::string_view value) : storage_(std::string(value)) {}
  Value(float value) : storage_(static_cast<double>(value)) {}
  Value(double value) : storage_(value) {}

  template <typename Integer,
            std::enable_if_t<std::is_integral_v<Integer> && std::is_signed_v<Integer> &&
                                 !std::is_same_v<std::remove_cv_t<Integer>, bool>,
                             int> = 0>
  Value(Integer value) : storage_(static_cast<std::int64_t>(value)) {}

  template <typename Integer,
            std::enable_if_t<std::is_integral_v<Integer> && std::is_unsigned_v<Integer> &&
                                 !std::is_same_v<std::remove_cv_t<Integer>, bool>,
                             int> = 0>
  Value(Integer value) : storage_(static_cast<std::uint64_t>(value)) {}

  template <typename T>
  Value(const std::vector<T>& values) : storage_(Array{}) {
    auto& array = std::get<Array>(storage_);
    array.reserve(values.size());
    for (const auto& value : values) {
      array.emplace_back(value);
    }
  }

  template <typename T>
  Value(const std::map<std::string, T>& values) : storage_(Object{}) {
    auto& object = std::get<Object>(storage_);
    object.reserve(values.size());
    for (const auto& [key, value] : values) {
      object.emplace_back(key, Value(value));
    }
  }

  Value(std::initializer_list<Value> values) { AssignInitializerList(values); }

  [[nodiscard]] static Value object() { return Value(Object{}); }

  [[nodiscard]] static Value array() { return Value(Array{}); }

  [[nodiscard]] static Value array(std::initializer_list<Value> values) {
    return Value(Array(values));
  }

  [[nodiscard]] static Value parse(std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf) {
      text.remove_prefix(3);
    }
    turbo_json_doc_t* document = nullptr;
    const int result = turbo_parse_json(reinterpret_cast<const std::uint8_t*>(text.data()),
                                        text.size(), &document);
    if (result != 0 || document == nullptr) {
      throw ParseError("TurboParser rejected JSON document (code " + std::to_string(result) + ")");
    }
    try {
      Value value = FromTurbo(document);
      turbo_free_json(&document);
      return value;
    } catch (...) {
      turbo_free_json(&document);
      throw;
    }
  }

  [[nodiscard]] static Value parse(const std::string& text) {
    return parse(std::string_view(text));
  }

  [[nodiscard]] static Value parse(const char* text) {
    if (text == nullptr) {
      throw ParseError("JSON input is null");
    }
    return parse(std::string_view(text));
  }

  [[nodiscard]] static Value parse(std::istream& stream) {
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return parse(std::string_view(text));
  }

  template <typename Callback>
  [[nodiscard]] static Value parse(std::string_view text, Callback* callback, bool allow_exceptions) {
    (void)callback;
    try {
      return parse(text);
    } catch (const ParseError&) {
      if (allow_exceptions) {
        throw;
      }
      return DiscardedValue();
    }
  }

  [[nodiscard]] static Value parse(std::string_view text, std::nullptr_t,
                                   bool allow_exceptions) {
    return parse<std::nullptr_t>(text, nullptr, allow_exceptions);
  }

  [[nodiscard]] std::string dump(int indent = -1) const {
    if (is_discarded()) {
      throw TypeError("discarded JSON value cannot be serialized");
    }
    turbo_json_doc_t* document = ToTurbo(*this);
    if (document == nullptr) {
      throw Error("TurboParser could not allocate JSON document");
    }
    size_t length = 0;
    char* serialized = indent >= 0 ? turbo_json_serialize_pretty(document, &length)
                                   : turbo_json_serialize(document, &length);
    turbo_free_json(&document);
    if (serialized == nullptr) {
      throw Error("TurboParser could not serialize JSON document");
    }
    std::string result(serialized, length);
    turbo_json_serialize_free(serialized);
    return result;
  }

  [[nodiscard]] bool is_null() const { return std::holds_alternative<std::nullptr_t>(storage_); }
  [[nodiscard]] bool is_boolean() const { return std::holds_alternative<bool>(storage_); }
  [[nodiscard]] bool is_number_integer() const { return std::holds_alternative<std::int64_t>(storage_); }
  [[nodiscard]] bool is_number_unsigned() const { return std::holds_alternative<std::uint64_t>(storage_); }
  [[nodiscard]] bool is_number_float() const { return std::holds_alternative<double>(storage_); }
  [[nodiscard]] bool is_number() const {
    return is_number_integer() || is_number_unsigned() || is_number_float();
  }
  [[nodiscard]] bool is_string() const { return std::holds_alternative<std::string>(storage_); }
  [[nodiscard]] bool is_array() const { return std::holds_alternative<Array>(storage_); }
  [[nodiscard]] bool is_object() const { return std::holds_alternative<Object>(storage_); }
  [[nodiscard]] bool is_discarded() const { return std::holds_alternative<Discarded>(storage_); }

  [[nodiscard]] std::size_t size() const {
    if (const auto* array = std::get_if<Array>(&storage_)) {
      return array->size();
    }
    if (const auto* object = std::get_if<Object>(&storage_)) {
      return object->size();
    }
    if (const auto* string = std::get_if<std::string>(&storage_)) {
      return string->size();
    }
    return is_null() ? 0U : 1U;
  }

  [[nodiscard]] bool empty() const { return size() == 0; }

  [[nodiscard]] bool contains(std::string_view key) const { return FindObjectIndex(key).has_value(); }

  Value& operator[](std::string_view key) {
    if (is_null()) {
      storage_ = Object{};
    }
    auto* object = std::get_if<Object>(&storage_);
    if (object == nullptr) {
      throw TypeError("JSON value is not an object");
    }
    if (const auto index = FindObjectIndex(key)) {
      return (*object)[*index].second;
    }
    object->emplace_back(std::string(key), Value());
    return object->back().second;
  }

  const Value& operator[](std::string_view key) const { return at(key); }

  Value& operator[](std::size_t index) {
    auto* array = std::get_if<Array>(&storage_);
    if (array == nullptr) {
      throw TypeError("JSON value is not an array");
    }
    return array->at(index);
  }

  const Value& operator[](std::size_t index) const { return at(index); }

  Value& at(std::string_view key) {
    const auto index = FindObjectIndex(key);
    if (!index) {
      throw TypeError("JSON object key not found: " + std::string(key));
    }
    return std::get<Object>(storage_)[*index].second;
  }

  const Value& at(std::string_view key) const {
    const auto index = FindObjectIndex(key);
    if (!index) {
      throw TypeError("JSON object key not found: " + std::string(key));
    }
    return std::get<Object>(storage_)[*index].second;
  }

  Value& at(std::size_t index) {
    auto* array = std::get_if<Array>(&storage_);
    if (array == nullptr) {
      throw TypeError("JSON value is not an array");
    }
    return array->at(index);
  }

  const Value& at(std::size_t index) const {
    const auto* array = std::get_if<Array>(&storage_);
    if (array == nullptr) {
      throw TypeError("JSON value is not an array");
    }
    return array->at(index);
  }

  void push_back(Value value) {
    if (is_null()) {
      storage_ = Array{};
    }
    auto* array = std::get_if<Array>(&storage_);
    if (array == nullptr) {
      throw TypeError("JSON value is not an array");
    }
    array->push_back(std::move(value));
  }

  template <typename... Args>
  Value& emplace_back(Args&&... args) {
    push_back(Value(std::forward<Args>(args)...));
    return std::get<Array>(storage_).back();
  }

  template <typename T>
  void push_back(T&& value) {
    push_back(Value(std::forward<T>(value)));
  }

  std::size_t erase(std::string_view key) {
    auto* object = std::get_if<Object>(&storage_);
    if (object == nullptr) {
      return 0;
    }
    const auto index = FindObjectIndex(key);
    if (!index) {
      return 0;
    }
    object->erase(object->begin() + static_cast<std::ptrdiff_t>(*index));
    return 1;
  }

  iterator erase(iterator position) {
    if (position.owner_ != this) {
      throw TypeError("JSON iterator belongs to another value");
    }
    if (position.index_ >= size()) {
      throw TypeError("JSON iterator out of range");
    }
    if (auto* object = std::get_if<Object>(&storage_)) {
      object->erase(object->begin() + static_cast<std::ptrdiff_t>(position.index_));
    } else if (auto* array = std::get_if<Array>(&storage_)) {
      array->erase(array->begin() + static_cast<std::ptrdiff_t>(position.index_));
    } else {
      throw TypeError("JSON value is not an array or object");
    }
    return iterator(this, position.index_);
  }

  iterator find(std::string_view key) {
    const auto index = FindObjectIndex(key);
    return iterator(this, index.value_or(size()));
  }

  const_iterator find(std::string_view key) const {
    const auto index = FindObjectIndex(key);
    return const_iterator(this, index.value_or(size()));
  }

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size()); }
  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, size()); }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  [[nodiscard]] Object& object_items() {
    auto* object = std::get_if<Object>(&storage_);
    if (object == nullptr) {
      throw TypeError("JSON value is not an object");
    }
    return *object;
  }

  [[nodiscard]] const char* type_name() const {
    if (is_null()) return "null";
    if (is_boolean()) return "boolean";
    if (is_number_integer()) return "number_integer";
    if (is_number_unsigned()) return "number_unsigned";
    if (is_number_float()) return "number_float";
    if (is_string()) return "string";
    if (is_array()) return "array";
    if (is_object()) return "object";
    return "discarded";
  }

  [[nodiscard]] const Object& object_items() const {
    const auto* object = std::get_if<Object>(&storage_);
    if (object == nullptr) {
      throw TypeError("JSON value is not an object");
    }
    return *object;
  }

  template <typename T>
  [[nodiscard]] T get() const {
    using Requested = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<Requested, Value>) {
      return *this;
    } else if constexpr (std::is_same_v<Requested, std::string>) {
      if (const auto* value = std::get_if<std::string>(&storage_)) {
        return *value;
      }
      throw TypeError("JSON value is not a string");
    } else if constexpr (std::is_same_v<Requested, bool>) {
      if (const auto* value = std::get_if<bool>(&storage_)) {
        return *value;
      }
      throw TypeError("JSON value is not a boolean");
    } else if constexpr (std::is_integral_v<Requested> && std::is_signed_v<Requested>) {
      return CheckedSigned<Requested>();
    } else if constexpr (std::is_integral_v<Requested> && std::is_unsigned_v<Requested>) {
      return CheckedUnsigned<Requested>();
    } else if constexpr (std::is_floating_point_v<Requested>) {
      return static_cast<Requested>(NumberAsLongDouble());
    } else if constexpr (turbo::json::IsVector<Requested>::value) {
      const auto* array = std::get_if<Array>(&storage_);
      if (array == nullptr) {
        throw TypeError("JSON value is not an array");
      }
      Requested result;
      result.reserve(array->size());
      for (const auto& item : *array) {
        result.push_back(item.template get<typename Requested::value_type>());
      }
      return result;
    } else {
      static_assert(!sizeof(Requested), "unsupported JSON conversion");
    }
  }

  template <typename T>
  [[nodiscard]] T value(std::string_view key, T fallback) const {
    const auto index = FindObjectIndex(key);
    if (!index) {
      return fallback;
    }
    try {
      return std::get<Object>(storage_)[*index].second.template get<T>();
    } catch (const TypeError&) {
      return fallback;
    }
  }

  [[nodiscard]] std::string value(std::string_view key, const char* fallback) const {
    return value<std::string>(key, fallback == nullptr ? std::string() : std::string(fallback));
  }

  friend bool operator==(const Value& lhs, const Value& rhs) { return lhs.storage_ == rhs.storage_; }
  friend bool operator!=(const Value& lhs, const Value& rhs) { return !(lhs == rhs); }

  template <typename T>
  friend bool operator==(const Value& lhs, const T& rhs) {
    try {
      return lhs.template get<std::decay_t<T>>() == rhs;
    } catch (const Error&) {
      return false;
    }
  }

  template <typename T>
  friend bool operator==(const T& lhs, const Value& rhs) {
    return rhs == lhs;
  }

  template <typename T>
  friend bool operator!=(const Value& lhs, const T& rhs) {
    return !(lhs == rhs);
  }

 private:
  explicit Value(Array values) : storage_(std::move(values)) {}
  explicit Value(Object values) : storage_(std::move(values)) {}
  explicit Value(Discarded value) : storage_(value) {}

  [[nodiscard]] static Value DiscardedValue() { return Value(Discarded{}); }

  void AssignInitializerList(std::initializer_list<Value> values) {
    bool object_shape = values.size() != 0;
    for (const auto& item : values) {
      if (!item.is_array() || item.size() != 2 || !item.at(0).is_string()) {
        object_shape = false;
        break;
      }
    }
    if (!object_shape) {
      storage_ = Array(values);
      return;
    }
    Object object;
    object.reserve(values.size());
    for (const auto& item : values) {
      object.emplace_back(item.at(0).get<std::string>(), item.at(1));
    }
    storage_ = std::move(object);
  }

  [[nodiscard]] std::optional<std::size_t> FindObjectIndex(std::string_view key) const {
    const auto* object = std::get_if<Object>(&storage_);
    if (object == nullptr) {
      return std::nullopt;
    }
    for (std::size_t index = 0; index < object->size(); ++index) {
      if ((*object)[index].first == key) {
        return index;
      }
    }
    return std::nullopt;
  }

  Value& ElementAt(std::size_t index) {
    if (auto* array = std::get_if<Array>(&storage_)) {
      return array->at(index);
    }
    if (auto* object = std::get_if<Object>(&storage_)) {
      return object->at(index).second;
    }
    throw TypeError("JSON value is not iterable");
  }

  const Value& ElementAt(std::size_t index) const {
    if (const auto* array = std::get_if<Array>(&storage_)) {
      return array->at(index);
    }
    if (const auto* object = std::get_if<Object>(&storage_)) {
      return object->at(index).second;
    }
    throw TypeError("JSON value is not iterable");
  }

  template <typename Integer>
  [[nodiscard]] Integer CheckedSigned() const {
    long double value = NumberAsLongDouble();
    if (!std::isfinite(value) || !IsIntegralNumber(value)) {
      throw TypeError("JSON number is not an integer");
    }
    if (value < static_cast<long double>((std::numeric_limits<Integer>::min)()) ||
        value > static_cast<long double>((std::numeric_limits<Integer>::max)())) {
      throw TypeError("JSON number is outside requested integer range");
    }
    return static_cast<Integer>(value);
  }

  template <typename Integer>
  [[nodiscard]] Integer CheckedUnsigned() const {
    long double value = NumberAsLongDouble();
    if (!std::isfinite(value) || !IsIntegralNumber(value)) {
      throw TypeError("JSON number is not an integer");
    }
    if (value < 0 || value > static_cast<long double>((std::numeric_limits<Integer>::max)())) {
      throw TypeError("JSON number is outside requested integer range");
    }
    return static_cast<Integer>(value);
  }

  [[nodiscard]] static bool IsIntegralNumber(long double value) {
    return std::trunc(value) == value;
  }

  [[nodiscard]] long double NumberAsLongDouble() const {
    if (const auto* value = std::get_if<std::int64_t>(&storage_)) {
      return static_cast<long double>(*value);
    }
    if (const auto* value = std::get_if<std::uint64_t>(&storage_)) {
      return static_cast<long double>(*value);
    }
    if (const auto* value = std::get_if<double>(&storage_)) {
      return static_cast<long double>(*value);
    }
    throw TypeError("JSON value is not a number");
  }

 public:
  [[nodiscard]] static Value FromTurbo(const json_value_t* value) {
    if (value == nullptr) {
      throw Error("TurboParser returned a null JSON node");
    }
    switch (turbo_json_type(value)) {
      case TURBO_JSON_NULL:
        return nullptr;
      case TURBO_JSON_BOOL:
        return turbo_json_bool(value);
      case TURBO_JSON_NUMBER: {
        size_t length = 0;
        const char* token = turbo_json_number_text(value, &length);
        if (token != nullptr && length != 0) {
          const std::string_view text(token, length);
          if (text.find_first_of(".eE") == std::string_view::npos) {
            if (!text.empty() && text.front() == '-') {
              std::int64_t signed_value = 0;
              const auto result = std::from_chars(text.data(), text.data() + text.size(), signed_value);
              if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
                return signed_value;
              }
            } else {
              std::uint64_t unsigned_value = 0;
              const auto result =
                  std::from_chars(text.data(), text.data() + text.size(), unsigned_value);
              if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
                if (unsigned_value <=
                    static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
                  return static_cast<std::int64_t>(unsigned_value);
                }
                return unsigned_value;
              }
            }
          }
        }
        return turbo_json_number(value);
      }
      case TURBO_JSON_STRING:
        return std::string(turbo_json_string(value), turbo_json_string_len(value));
      case TURBO_JSON_ARRAY: {
        Array array;
        const std::size_t count = turbo_json_array_size(value);
        array.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
          array.push_back(FromTurbo(turbo_json_array_get(value, index)));
        }
        return Value(std::move(array));
      }
      case TURBO_JSON_OBJECT: {
        Object object;
        const std::size_t count = turbo_json_object_size(value);
        object.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
          const char* key = turbo_json_object_key(value, index);
          if (key == nullptr) {
            throw Error("TurboParser returned a null object key");
          }
          object.emplace_back(key, FromTurbo(turbo_json_object_value(value, index)));
        }
        return Value(std::move(object));
      }
    }
    throw Error("TurboParser returned an unknown JSON node type");
  }

  [[nodiscard]] static json_value_t* ToTurbo(const Value& value) {
    if (value.is_null()) {
      return turbo_json_create_null();
    }
    if (const auto* boolean = std::get_if<bool>(&value.storage_)) {
      return turbo_json_create_bool(*boolean);
    }
    if (const auto* number = std::get_if<std::int64_t>(&value.storage_)) {
      return turbo_json_create_int64(*number);
    }
    if (const auto* number = std::get_if<std::uint64_t>(&value.storage_)) {
      return turbo_json_create_uint64(*number);
    }
    if (const auto* number = std::get_if<double>(&value.storage_)) {
      return turbo_json_create_number(*number);
    }
    if (const auto* string = std::get_if<std::string>(&value.storage_)) {
      return turbo_json_create_string_n(string->data(), string->size());
    }
    if (const auto* array = std::get_if<Array>(&value.storage_)) {
      json_value_t* result = turbo_json_create_array();
      if (result == nullptr) {
        return nullptr;
      }
      for (const auto& item : *array) {
        json_value_t* child = ToTurbo(item);
        if (child == nullptr || !turbo_json_array_add_checked(result, child)) {
          turbo_json_doc_t* document = result;
          turbo_free_json(&document);
          if (child != nullptr) {
            turbo_json_doc_t* child_document = child;
            turbo_free_json(&child_document);
          }
          return nullptr;
        }
      }
      return result;
    }
    if (const auto* object = std::get_if<Object>(&value.storage_)) {
      json_value_t* result = turbo_json_create_object();
      if (result == nullptr) {
        return nullptr;
      }
      for (const auto& [key, item] : *object) {
        json_value_t* child = ToTurbo(item);
        if (child == nullptr || !turbo_json_object_add_checked(result, key.c_str(), child)) {
          turbo_json_doc_t* document = result;
          turbo_free_json(&document);
          if (child != nullptr) {
            turbo_json_doc_t* child_document = child;
            turbo_free_json(&child_document);
          }
          return nullptr;
        }
      }
      return result;
    }
    return nullptr;
  }

 private:
  Storage storage_ = nullptr;
};

namespace compat {

namespace detail {

inline const char* cache_text(std::string_view text) {
  thread_local std::string cache;
  cache.assign(text.data(), text.size());
  return cache.c_str();
}

inline const char* cache_error(std::string_view message) {
  thread_local std::string cache;
  cache.assign(message);
  return cache.c_str();
}

inline std::string number_text(const Value& value) {
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  if (value.is_number_float()) {
    return std::to_string(value.get<double>());
  }
  return {};
}

inline std::string pretty_text(const Value& value, bool crlf) {
  std::string text = value.dump(2);
  if (!crlf) {
    return text;
  }
  std::string crlf_text;
  crlf_text.reserve(text.size() * 2);
  for (const char c : text) {
    if (c == '\n') {
      crlf_text.push_back('\r');
    }
    crlf_text.push_back(c);
  }
  return crlf_text;
}

struct SaxParserState {
  ::turbo_json_sax_parser_t* native = nullptr;
};

struct PathProgramState {
  ::turbo_json_path_program_t* native = nullptr;
};

struct PathResultState {
  std::vector<Value> values;
};

struct PathStreamState {
  const PathProgramState* program = nullptr;
  const turbo_json_path_stream_handler_t* handler = nullptr;
  void* ctx = nullptr;
  ::turbo_json_path_stream_t* native = nullptr;
};

inline int path_stream_match_start(void* user, turbo_json_type_t type) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->on_match_start
             ? stream->handler->on_match_start(stream->ctx, type)
             : 0;
}

inline int path_stream_match_end(void* user, turbo_json_type_t type) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->on_match_end
             ? stream->handler->on_match_end(stream->ctx, type)
             : 0;
}

inline int path_stream_null(void* user) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_null
             ? stream->handler->events.on_null(stream->ctx)
             : 0;
}

inline int path_stream_bool(void* user, bool value) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_bool
             ? stream->handler->events.on_bool(stream->ctx, value)
             : 0;
}

inline int path_stream_number(void* user, const char* value, std::size_t len) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_number
             ? stream->handler->events.on_number(stream->ctx, value, len)
             : 0;
}

inline int path_stream_string(void* user, const char* value, std::size_t len) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_string
             ? stream->handler->events.on_string(stream->ctx, value, len)
             : 0;
}

inline int path_stream_object_start(void* user) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_object_start
             ? stream->handler->events.on_object_start(stream->ctx)
             : 0;
}

inline int path_stream_object_key(void* user, const char* key, std::size_t len) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_object_key
             ? stream->handler->events.on_object_key(stream->ctx, key, len)
             : 0;
}

inline int path_stream_object_end(void* user) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_object_end
             ? stream->handler->events.on_object_end(stream->ctx)
             : 0;
}

inline int path_stream_array_start(void* user) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_array_start
             ? stream->handler->events.on_array_start(stream->ctx)
             : 0;
}

inline int path_stream_array_end(void* user) {
  const auto* stream = static_cast<const PathStreamState*>(user);
  return stream && stream->handler && stream->handler->events.on_array_end
             ? stream->handler->events.on_array_end(stream->ctx)
             : 0;
}

inline bool collect_path_result_values(const ::turbo_json_path_result_t* source,
                                      PathResultState& target) {
  try {
    if (source == nullptr) {
      return false;
    }
    const std::size_t count = ::turbo_json_path_result_size(source);
    target.values.clear();
    target.values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const ::json_value_t* value = ::turbo_json_path_result_get(source, index);
      if (value == nullptr) {
        return false;
      }
      target.values.push_back(Value::FromTurbo(value));
    }
    return true;
  } catch (...) {
    target.values.clear();
    return false;
  }
}

}  // namespace detail

using turbo_json_sax_parser_t = detail::SaxParserState;
using turbo_json_path_program_t = detail::PathProgramState;
using turbo_json_path_stream_t = detail::PathStreamState;
using turbo_json_path_result_t = detail::PathResultState;
using turbo_json_jpath_program_t = turbo_json_path_program_t;
using turbo_json_jpath_stream_t = turbo_json_path_stream_t;
using turbo_json_jpath_result_t = turbo_json_path_result_t;
using json_value_t = Value;
using turbo_json_doc_t = Value;

inline int turbo_parse_json(const std::uint8_t* data, std::size_t len, turbo_json_doc_t** out) {
  if (out == nullptr || data == nullptr) {
    return -1;
  }
  *out = nullptr;
  ::json_value_t* document = nullptr;
  if (::turbo_parse_json(data, len, &document) != 0 || document == nullptr) {
    return -1;
  }
  try {
    *out = new Value(Value::FromTurbo(document));
    ::turbo_free_json(&document);
    return 0;
  } catch (...) {
    ::turbo_free_json(&document);
    return -1;
  }
}

inline int turbo_parse_json_sax(const std::uint8_t* data, std::size_t len,
                               const turbo_json_sax_handler_t* handler, void* ctx) {
  if (data == nullptr) {
    return -1;
  }
  return ::turbo_parse_json_sax(data, len, handler, ctx) == 0 ? 0 : -1;
}

inline int turbo_parse_json_sax_raw(const std::uint8_t* data, std::size_t len,
                                   const turbo_json_sax_handler_raw_t* handler, void* ctx) {
  if (data == nullptr) {
    return -1;
  }
  return ::turbo_parse_json_sax_raw(data, len, handler, ctx) == 0 ? 0 : -1;
}

inline turbo_json_sax_parser_t* turbo_json_sax_parser_create(
    const turbo_json_sax_handler_t* handler, void* ctx) {
  auto* state = new turbo_json_sax_parser_t();
  state->native = ::turbo_json_sax_parser_create(handler, ctx);
  if (state->native == nullptr) {
    delete state;
    return nullptr;
  }
  return state;
}

inline turbo_json_sax_parser_t* turbo_json_sax_parser_create_raw(
    const turbo_json_sax_handler_raw_t* handler, void* ctx) {
  auto* state = new turbo_json_sax_parser_t();
  state->native = ::turbo_json_sax_parser_create_raw(handler, ctx);
  if (state->native == nullptr) {
    delete state;
    return nullptr;
  }
  return state;
}

inline int turbo_json_sax_parser_feed(turbo_json_sax_parser_t* parser, const char* data, std::size_t len) {
  if (parser == nullptr) {
    return -1;
  }
  if (data == nullptr || parser->native == nullptr) {
    detail::cache_error("C++ compat layer: invalid SAX parser");
    return -1;
  }
  return ::turbo_json_sax_parser_feed(parser->native, data, len);
}

inline int turbo_json_sax_parser_finish(turbo_json_sax_parser_t* parser) {
  if (parser == nullptr) {
    return -1;
  }
  if (parser->native == nullptr) {
    detail::cache_error("C++ compat layer: invalid SAX parser");
    return -1;
  }
  return ::turbo_json_sax_parser_finish(parser->native);
}

inline const char* turbo_json_sax_parser_error(const turbo_json_sax_parser_t* parser) {
  if (parser == nullptr || parser->native == nullptr) {
    return nullptr;
  }
  return ::turbo_json_sax_parser_error(parser->native);
}

inline void turbo_json_sax_parser_destroy(turbo_json_sax_parser_t* parser) {
  if (parser == nullptr) {
    return;
  }
  if (parser->native != nullptr) {
    ::turbo_json_sax_parser_destroy(parser->native);
  }
  delete parser;
}

inline void turbo_free_json(turbo_json_doc_t** out) {
  if (out == nullptr || *out == nullptr) {
    return;
  }
  delete *out;
  *out = nullptr;
}

inline turbo_json_type_t turbo_json_type(const json_value_t* value) {
  if (value == nullptr) {
    return TURBO_JSON_NULL;
  }
  if (value->is_null()) {
    return TURBO_JSON_NULL;
  }
  if (value->is_boolean()) {
    return TURBO_JSON_BOOL;
  }
  if (value->is_number()) {
    return TURBO_JSON_NUMBER;
  }
  if (value->is_string()) {
    return TURBO_JSON_STRING;
  }
  if (value->is_array()) {
    return TURBO_JSON_ARRAY;
  }
  return TURBO_JSON_OBJECT;
}

inline bool turbo_json_is_null(const json_value_t* value) {
  return value != nullptr && value->is_null();
}

inline bool turbo_json_bool(const json_value_t* value) {
  if (value == nullptr) {
    return false;
  }
  try {
    return value->get<bool>();
  } catch (...) {
    return false;
  }
}

inline double turbo_json_number(const json_value_t* value) {
  if (value == nullptr) {
    return 0.0;
  }
  try {
    return value->get<double>();
  } catch (...) {
    return 0.0;
  }
}

inline const char* turbo_json_number_text(const json_value_t* value, std::size_t* len) {
  if (len != nullptr) {
    *len = 0;
  }
  if (value == nullptr || !value->is_number()) {
    return nullptr;
  }
  const char* text = detail::cache_text(detail::number_text(*value));
  if (len != nullptr) {
    *len = std::strlen(text);
  }
  return text;
}

inline const char* turbo_json_string(const json_value_t* value) {
  if (value == nullptr || !value->is_string()) {
    return nullptr;
  }
  try {
    return detail::cache_text(value->get<std::string>());
  } catch (...) {
    return nullptr;
  }
}

inline std::size_t turbo_json_string_len(const json_value_t* value) {
  if (value == nullptr || !value->is_string()) {
    return 0;
  }
  try {
    return value->get<std::string>().size();
  } catch (...) {
    return 0;
  }
}

inline std::size_t turbo_json_object_size(const json_value_t* obj) {
  return obj != nullptr && obj->is_object() ? obj->size() : 0;
}

inline const char* turbo_json_object_key(const json_value_t* obj, std::size_t index) {
  if (obj == nullptr || !obj->is_object()) {
    return nullptr;
  }
  try {
    const auto& object = obj->object_items();
    if (index >= object.size()) {
      return nullptr;
    }
    return object[index].first.c_str();
  } catch (...) {
    return nullptr;
  }
}

inline Value* turbo_json_object_value(Value* obj, std::size_t index) {
  if (obj == nullptr || !obj->is_object()) {
    return nullptr;
  }
  try {
    auto& object = obj->object_items();
    if (index >= object.size()) {
      return nullptr;
    }
    return &object[index].second;
  } catch (...) {
    return nullptr;
  }
}

inline const Value* turbo_json_object_value(const Value* obj, std::size_t index) {
  if (obj == nullptr || !obj->is_object()) {
    return nullptr;
  }
  try {
    const auto& object = obj->object_items();
    if (index >= object.size()) {
      return nullptr;
    }
    return &object[index].second;
  } catch (...) {
    return nullptr;
  }
}

inline Value* turbo_json_object_get(Value* obj, const char* key) {
  if (obj == nullptr || key == nullptr) {
    return nullptr;
  }
  try {
    return &(*obj)[std::string_view(key)];
  } catch (...) {
    return nullptr;
  }
}

inline const Value* turbo_json_object_get(const Value* obj, const char* key) {
  if (obj == nullptr || key == nullptr) {
    return nullptr;
  }
  try {
    return &obj->at(key);
  } catch (...) {
    return nullptr;
  }
}

inline std::size_t turbo_json_array_size(const json_value_t* arr) {
  return arr != nullptr && arr->is_array() ? arr->size() : 0;
}

inline Value* turbo_json_array_get(Value* arr, std::size_t index) {
  if (arr == nullptr || !arr->is_array()) {
    return nullptr;
  }
  try {
    return &arr->at(index);
  } catch (...) {
    return nullptr;
  }
}

inline const Value* turbo_json_array_get(const Value* arr, std::size_t index) {
  if (arr == nullptr || !arr->is_array()) {
    return nullptr;
  }
  try {
    return &arr->at(index);
  } catch (...) {
    return nullptr;
  }
}

inline int turbo_json_get_int(const json_value_t* obj, const char* key, int def) {
  if (obj == nullptr || key == nullptr) {
    return def;
  }
  return obj->value<int>(key, def);
}

inline bool turbo_json_get_bool(const json_value_t* obj, const char* key, bool def) {
  if (obj == nullptr || key == nullptr) {
    return def;
  }
  return obj->value<bool>(key, def);
}

inline double turbo_json_get_double(const json_value_t* obj, const char* key, double def) {
  if (obj == nullptr || key == nullptr) {
    return def;
  }
  return obj->value<double>(key, def);
}

inline const char* turbo_json_get_string(const json_value_t* obj, const char* key) {
  if (obj == nullptr || key == nullptr) {
    return nullptr;
  }
  try {
    if (!obj->is_object()) {
      return nullptr;
    }
    return detail::cache_text(obj->at(std::string_view(key)).get<std::string>());
  } catch (...) {
    return nullptr;
  }
}

inline char* turbo_json_serialize(const json_value_t* value, std::size_t* out_len) {
  if (out_len != nullptr) {
    *out_len = 0;
  }
  if (value == nullptr) {
    return nullptr;
  }
  ::json_value_t* native = Value::ToTurbo(*value);
  if (native == nullptr) {
    return nullptr;
  }
  char* serialized = ::turbo_json_serialize(native, out_len);
  ::turbo_free_json(&native);
  return serialized;
}

inline char* turbo_json_serialize_pretty(const json_value_t* value, std::size_t* out_len) {
  if (out_len != nullptr) {
    *out_len = 0;
  }
  if (value == nullptr) {
    return nullptr;
  }
  ::json_value_t* native = Value::ToTurbo(*value);
  if (native == nullptr) {
    return nullptr;
  }
  char* serialized = ::turbo_json_serialize_pretty(native, out_len);
  ::turbo_free_json(&native);
  return serialized;
}

inline char* turbo_json_serialize_pretty_crlf(const json_value_t* value, std::size_t* out_len) {
  if (out_len != nullptr) {
    *out_len = 0;
  }
  if (value == nullptr) {
    return nullptr;
  }
  ::json_value_t* native = Value::ToTurbo(*value);
  if (native == nullptr) {
    return nullptr;
  }
  char* serialized = ::turbo_json_serialize_pretty_crlf(native, out_len);
  ::turbo_free_json(&native);
  return serialized;
}

inline void turbo_json_serialize_free(char* str) {
  ::turbo_json_serialize_free(str);
}

inline int turbo_json_write(const json_value_t* value, turbo_write_fn write, void* user) {
  if (value == nullptr || write == nullptr) {
    return -1;
  }
  ::json_value_t* native = Value::ToTurbo(*value);
  if (native == nullptr) {
    return -1;
  }
  const int result = ::turbo_json_write(native, write, user);
  ::turbo_free_json(&native);
  return result;
}

inline Value* turbo_json_clone(const json_value_t* value) {
  if (value == nullptr) {
    return nullptr;
  }
  return new Value(*value);
}

inline Value* turbo_json_path_get(const json_value_t* root, const char* expr) {
  if (root == nullptr || expr == nullptr) {
    return nullptr;
  }
  ::json_value_t* native_root = Value::ToTurbo(*root);
  if (native_root == nullptr) {
    return nullptr;
  }
  ::json_value_t* match = ::turbo_json_path_get(native_root, expr);
  if (match == nullptr) {
    ::turbo_free_json(&native_root);
    return nullptr;
  }
  Value* result = nullptr;
  try {
    result = new Value(Value::FromTurbo(match));
    ::turbo_free_json(&native_root);
    return result;
  } catch (...) {
    ::turbo_free_json(&native_root);
    return nullptr;
  }
}

inline turbo_json_path_result_t* turbo_json_path_query(const json_value_t* root, const char* expr) {
  if (root == nullptr || expr == nullptr) {
    detail::cache_error("C++ compat layer: path query requires root and expr");
    return nullptr;
  }
  ::json_value_t* native_root = Value::ToTurbo(*root);
  if (native_root == nullptr) {
    return nullptr;
  }
  ::turbo_json_path_result_t* native_result = ::turbo_json_path_query(native_root, expr);
  ::turbo_free_json(&native_root);
  if (native_result == nullptr) {
    return nullptr;
  }
  auto* result = new turbo_json_path_result_t();
  const bool collected = detail::collect_path_result_values(native_result, *result);
  ::turbo_json_path_result_free((::turbo_json_path_result_t*)native_result);
  if (!collected) {
    delete result;
    return nullptr;
  }
  return result;
}

inline turbo_json_path_program_t* turbo_json_path_compile(const char* expr) {
  if (expr == nullptr) {
    return nullptr;
  }
  auto* program = new turbo_json_path_program_t();
  program->native = ::turbo_json_path_compile(expr);
  if (program->native == nullptr) {
    delete program;
    return nullptr;
  }
  return program;
}

inline turbo_json_path_program_t* turbo_json_path_compile_ex(const char* expr,
                                                           const turbo_query_limits_t* limits,
                                                           turbo_query_diagnostic_t* diagnostic) {
  if (expr == nullptr) {
    return nullptr;
  }
  auto* program = new turbo_json_path_program_t();
  program->native = ::turbo_json_path_compile_ex(expr, limits, diagnostic);
  if (program->native == nullptr) {
    delete program;
    return nullptr;
  }
  return program;
}

inline Value* turbo_json_path_get_compiled(const json_value_t* root, const turbo_json_path_program_t* program) {
  if (root == nullptr || program == nullptr || program->native == nullptr) {
    return nullptr;
  }
  ::json_value_t* native_root = Value::ToTurbo(*root);
  if (native_root == nullptr) {
    return nullptr;
  }
  ::json_value_t* match = ::turbo_json_path_get_compiled(native_root, program->native);
  if (match == nullptr) {
    ::turbo_free_json(&native_root);
    return nullptr;
  }
  Value* result = nullptr;
  try {
    result = new Value(Value::FromTurbo(match));
  } catch (...) {
    result = nullptr;
  }
  ::turbo_free_json(&native_root);
  return result;
}

inline Value* turbo_json_path_get_compiled_ex(const json_value_t* root, const turbo_json_path_program_t* program,
                                            turbo_query_diagnostic_t* diagnostic) {
  if (root == nullptr || program == nullptr || program->native == nullptr) {
    return nullptr;
  }
  ::json_value_t* native_root = Value::ToTurbo(*root);
  if (native_root == nullptr) {
    return nullptr;
  }
  ::json_value_t* match = ::turbo_json_path_get_compiled_ex(native_root, program->native, diagnostic);
  if (match == nullptr) {
    ::turbo_free_json(&native_root);
    return nullptr;
  }
  Value* result = nullptr;
  try {
    result = new Value(Value::FromTurbo(match));
  } catch (...) {
    result = nullptr;
  }
  ::turbo_free_json(&native_root);
  return result;
}

inline turbo_json_path_result_t* turbo_json_path_query_compiled(const json_value_t* root,
                                                              const turbo_json_path_program_t* program) {
  if (root == nullptr || program == nullptr || program->native == nullptr) {
    return nullptr;
  }
  ::json_value_t* native_root = Value::ToTurbo(*root);
  if (native_root == nullptr) {
    return nullptr;
  }
  ::turbo_json_path_result_t* native_result =
      ::turbo_json_path_query_compiled(native_root, program->native);
  ::turbo_free_json(&native_root);
  if (native_result == nullptr) {
    return nullptr;
  }
  auto* result = new turbo_json_path_result_t();
  if (!detail::collect_path_result_values(native_result, *result)) {
    ::turbo_json_path_result_free((::turbo_json_path_result_t*)native_result);
    delete result;
    return nullptr;
  }
  ::turbo_json_path_result_free((::turbo_json_path_result_t*)native_result);
  return result;
}

inline turbo_json_path_result_t* turbo_json_path_query_compiled_ex(
    const json_value_t* root, const turbo_json_path_program_t* program,
    turbo_query_diagnostic_t* diagnostic) {
  if (root == nullptr || program == nullptr || program->native == nullptr) {
    return nullptr;
  }
  ::json_value_t* native_root = Value::ToTurbo(*root);
  if (native_root == nullptr) {
    return nullptr;
  }
  ::turbo_json_path_result_t* native_result =
      ::turbo_json_path_query_compiled_ex(native_root, program->native, diagnostic);
  ::turbo_free_json(&native_root);
  if (native_result == nullptr) {
    return nullptr;
  }
  auto* result = new turbo_json_path_result_t();
  if (!detail::collect_path_result_values(native_result, *result)) {
    ::turbo_json_path_result_free((::turbo_json_path_result_t*)native_result);
    delete result;
    return nullptr;
  }
  ::turbo_json_path_result_free((::turbo_json_path_result_t*)native_result);
  return result;
}

inline Value* turbo_jpath_get(const json_value_t* root, const char* expr) {
  return turbo_json_path_get(root, expr);
}

inline turbo_json_jpath_result_t* turbo_jpath_query(const json_value_t* root, const char* expr) {
  return turbo_json_path_query(root, expr);
}

inline turbo_json_jpath_program_t* turbo_jpath_compile(const char* expr) {
  return turbo_json_path_compile(expr);
}

inline turbo_json_jpath_program_t* turbo_jpath_compile_ex(const char* expr,
                                                        const turbo_query_limits_t* limits,
                                                        turbo_query_diagnostic_t* diagnostic) {
  return ::turbo::json::compat::turbo_json_path_compile_ex(expr, limits, diagnostic);
}

inline Value* turbo_jpath_get_compiled(const json_value_t* root,
                                      const turbo_json_jpath_program_t* program) {
  return turbo_json_path_get_compiled(root, program);
}

inline Value* turbo_jpath_get_compiled_ex(const json_value_t* root,
                                         const turbo_json_jpath_program_t* program,
                                         turbo_query_diagnostic_t* diagnostic) {
  return turbo_json_path_get_compiled_ex(root, program, diagnostic);
}

inline turbo_json_jpath_result_t* turbo_jpath_query_compiled(const json_value_t* root,
                                                           const turbo_json_jpath_program_t* program) {
  return turbo_json_path_query_compiled(root, program);
}

inline turbo_json_jpath_result_t* turbo_jpath_query_compiled_ex(
    const json_value_t* root, const turbo_json_jpath_program_t* program,
    turbo_query_diagnostic_t* diagnostic) {
  return turbo_json_path_query_compiled_ex(root, program, diagnostic);
}

inline void turbo_json_path_program_free(turbo_json_path_program_t* program) {
  if (program == nullptr) {
    return;
  }
  if (program->native != nullptr) {
    ::turbo_json_path_program_free(program->native);
  }
  delete program;
}

inline void turbo_jpath_program_free(turbo_json_jpath_program_t* program) {
  turbo_json_path_program_free(program);
}

inline turbo_json_path_stream_t* turbo_json_path_stream_create(
    const turbo_json_path_program_t* program, const turbo_json_path_stream_handler_t* handler, void* ctx) {
  if (program == nullptr || handler == nullptr || program->native == nullptr) {
    detail::cache_error("C++ compat layer: stream create requires program and handler");
    return nullptr;
  }
  auto* stream = new turbo_json_path_stream_t();
  stream->program = program;
  stream->handler = handler;
  stream->ctx = ctx;
  turbo_json_path_stream_handler_t raw_handler = {};
  raw_handler.on_match_start =
      handler->on_match_start != nullptr ? detail::path_stream_match_start : nullptr;
  raw_handler.on_match_end =
      handler->on_match_end != nullptr ? detail::path_stream_match_end : nullptr;
  raw_handler.events.on_null = handler->events.on_null != nullptr ? detail::path_stream_null : nullptr;
  raw_handler.events.on_bool = handler->events.on_bool != nullptr ? detail::path_stream_bool : nullptr;
  raw_handler.events.on_number = handler->events.on_number != nullptr ? detail::path_stream_number : nullptr;
  raw_handler.events.on_string = handler->events.on_string != nullptr ? detail::path_stream_string : nullptr;
  raw_handler.events.on_object_start =
      handler->events.on_object_start != nullptr ? detail::path_stream_object_start : nullptr;
  raw_handler.events.on_object_key =
      handler->events.on_object_key != nullptr ? detail::path_stream_object_key : nullptr;
  raw_handler.events.on_object_end =
      handler->events.on_object_end != nullptr ? detail::path_stream_object_end : nullptr;
  raw_handler.events.on_array_start =
      handler->events.on_array_start != nullptr ? detail::path_stream_array_start : nullptr;
  raw_handler.events.on_array_end =
      handler->events.on_array_end != nullptr ? detail::path_stream_array_end : nullptr;
  stream->native = ::turbo_json_path_stream_create(program->native, &raw_handler, stream);
  if (stream->native == nullptr) {
    delete stream;
    return nullptr;
  }
  return stream;
}

inline turbo_json_jpath_stream_t* turbo_jpath_stream_create(
    const turbo_json_jpath_program_t* program, const turbo_json_path_stream_handler_t* handler,
    void* ctx) {
  return turbo_json_path_stream_create(program, handler, ctx);
}

inline int turbo_json_path_stream_feed(turbo_json_path_stream_t* stream, const char* data, std::size_t len) {
  if (stream == nullptr) {
    return -1;
  }
  if (data == nullptr) {
    detail::cache_error("C++ compat layer: null input");
    return -1;
  }
  if (stream->native == nullptr) {
    detail::cache_error("C++ compat layer: invalid path stream");
    return -1;
  }
  return ::turbo_json_path_stream_feed(stream->native, data, len);
}

inline int turbo_jpath_stream_feed(turbo_json_jpath_stream_t* stream, const char* data,
                                  std::size_t len) {
  return turbo_json_path_stream_feed(stream, data, len);
}

inline int turbo_json_path_stream_finish(turbo_json_path_stream_t* stream) {
  if (stream == nullptr) {
    return -1;
  }
  if (stream->native == nullptr) {
    detail::cache_error("C++ compat layer: invalid path stream");
    return -1;
  }
  return ::turbo_json_path_stream_finish(stream->native);
}

inline int turbo_jpath_stream_finish(turbo_json_jpath_stream_t* stream) {
  return turbo_json_path_stream_finish(stream);
}

inline std::size_t turbo_json_path_stream_match_count(const turbo_json_path_stream_t* stream) {
  if (stream == nullptr || stream->native == nullptr) {
    return 0;
  }
  return ::turbo_json_path_stream_match_count(stream->native);
}

inline std::size_t turbo_jpath_stream_match_count(const turbo_json_jpath_stream_t* stream) {
  return turbo_json_path_stream_match_count(stream);
}

inline const char* turbo_json_path_stream_error(const turbo_json_path_stream_t* stream) {
  if (stream == nullptr || stream->native == nullptr) {
    return nullptr;
  }
  return ::turbo_json_path_stream_error(stream->native);
}

inline const char* turbo_jpath_stream_error(const turbo_json_jpath_stream_t* stream) {
  return turbo_json_path_stream_error(stream);
}

inline void turbo_json_path_stream_destroy(turbo_json_path_stream_t* stream) {
  if (stream == nullptr) {
    return;
  }
  if (stream->native != nullptr) {
    ::turbo_json_path_stream_destroy(stream->native);
  }
  delete stream;
}

inline void turbo_jpath_stream_destroy(turbo_json_jpath_stream_t* stream) {
  turbo_json_path_stream_destroy(stream);
}

inline std::size_t turbo_json_path_result_size(const turbo_json_path_result_t* result) {
  if (result == nullptr) {
    return 0;
  }
  return result->values.size();
}

inline std::size_t turbo_jpath_result_size(const turbo_json_jpath_result_t* result) {
  return turbo_json_path_result_size(result);
}

inline Value* turbo_json_path_result_get(turbo_json_path_result_t* result, std::size_t index) {
  if (result == nullptr || index >= result->values.size()) {
    return nullptr;
  }
  return &result->values[index];
}

inline const Value* turbo_json_path_result_get(const turbo_json_path_result_t* result, std::size_t index) {
  if (result == nullptr || index >= result->values.size()) {
    return nullptr;
  }
  return &result->values[index];
}

inline Value* turbo_jpath_result_get(turbo_json_jpath_result_t* result, std::size_t index) {
  return turbo_json_path_result_get(result, index);
}

inline const Value* turbo_jpath_result_get(const turbo_json_jpath_result_t* result, std::size_t index) {
  return turbo_json_path_result_get(result, index);
}

inline void turbo_jpath_result_free(turbo_json_jpath_result_t* result) {
  delete result;
}

inline void turbo_json_path_result_free(turbo_json_path_result_t* result) {
  delete result;
}

inline const char* turbo_json_path_error() {
  return ::turbo_json_path_error();
}

inline Value* turbo_json_create_object() {
  return new Value(Value::object());
}

inline Value* turbo_json_create_array() {
  return new Value(Value::array());
}

inline Value* turbo_json_create_string(const char* str) {
  return new Value(str == nullptr ? std::string() : std::string_view(str));
}

inline Value* turbo_json_create_string_n(const char* str, std::size_t len) {
  if (str == nullptr) {
    return new Value(std::string());
  }
  return new Value(std::string(str, len));
}

inline Value* turbo_json_create_number(double num) {
  return new Value(num);
}

inline Value* turbo_json_create_int64(std::int64_t num) {
  return new Value(num);
}

inline Value* turbo_json_create_uint64(std::uint64_t num) {
  return new Value(num);
}

inline Value* turbo_json_create_bool(bool val) {
  return new Value(val);
}

inline Value* turbo_json_create_null() {
  return new Value();
}

inline void turbo_json_object_add(Value* obj, const char* key, Value* val) {
  if (obj == nullptr || key == nullptr || val == nullptr) {
    delete val;
    return;
  }
  try {
    (*obj)[key] = std::move(*val);
  } catch (...) {
  }
  delete val;
}

inline bool turbo_json_object_add_checked(Value* obj, const char* key, Value* val) {
  if (obj == nullptr || key == nullptr || val == nullptr) {
    delete val;
    return false;
  }
  try {
    if (!obj->is_object()) {
      delete val;
      return false;
    }
    (*obj)[key] = std::move(*val);
    delete val;
    return true;
  } catch (...) {
    delete val;
    return false;
  }
}

inline void turbo_json_array_add(Value* arr, Value* val) {
  if (arr == nullptr || val == nullptr) {
    delete val;
    return;
  }
  try {
    arr->push_back(std::move(*val));
  } catch (...) {
  }
  delete val;
}

inline bool turbo_json_array_add_checked(Value* arr, Value* val) {
  if (arr == nullptr || val == nullptr) {
    delete val;
    return false;
  }
  try {
    arr->push_back(std::move(*val));
    delete val;
    return true;
  } catch (...) {
    delete val;
    return false;
  }
}

inline void turbo_json_object_set_string(Value* obj, const char* key, const char* val) {
  if (obj == nullptr || key == nullptr) {
    return;
  }
  try {
    (*obj)[key] = Value(val == nullptr ? std::string() : std::string_view(val));
  } catch (...) {
  }
}

inline void turbo_json_object_set_number(Value* obj, const char* key, double val) {
  if (obj == nullptr || key == nullptr) {
    return;
  }
  try {
    (*obj)[key] = Value(val);
  } catch (...) {
  }
}

inline void turbo_json_object_set_bool(Value* obj, const char* key, bool val) {
  if (obj == nullptr || key == nullptr) {
    return;
  }
  try {
    (*obj)[key] = Value(val);
  } catch (...) {
  }
}

inline void turbo_json_object_set_null(Value* obj, const char* key) {
  if (obj == nullptr || key == nullptr) {
    return;
  }
  try {
    (*obj)[key] = Value();
  } catch (...) {
  }
}

}  // namespace compat

using namespace compat;

using Json = Value;

inline std::istream& operator>>(std::istream& stream, Value& value) {
  value = Value::parse(stream);
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream, const Value& value) {
  stream << value.dump();
  return stream;
}

}  // namespace turbo::json
 
