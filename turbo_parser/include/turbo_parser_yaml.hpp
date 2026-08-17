#pragma once

#include <turbo_parser.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace turbo::yaml {

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

namespace compat {

namespace detail {

class Document {
 public:
  Document() = default;
  explicit Document(::turbo_yaml_doc_t* native) : native_(native) {}
  ~Document() { Reset(); }

  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;
  Document(Document&& other) noexcept : native_(other.native_) { other.native_ = nullptr; }
  Document& operator=(Document&& other) noexcept {
    if (this != &other) {
      Reset();
      native_ = other.native_;
      other.native_ = nullptr;
    }
    return *this;
  }

  void Reset(::turbo_yaml_doc_t* native = nullptr) noexcept {
    if (native_ != nullptr) {
      ::turbo_free_yaml(&native_);
    }
    native_ = native;
  }

  [[nodiscard]] ::turbo_yaml_doc_t* get() const noexcept { return native_; }
  explicit operator bool() const noexcept { return native_ != nullptr; }

 private:
  ::turbo_yaml_doc_t* native_ = nullptr;
};

class SaxParser {
 public:
  SaxParser() = default;
  explicit SaxParser(::turbo_yaml_sax_parser_t* native) : native_(native) {}
  ~SaxParser() { Reset(); }

  SaxParser(const SaxParser&) = delete;
  SaxParser& operator=(const SaxParser&) = delete;
  SaxParser(SaxParser&& other) noexcept : native_(other.native_) { other.native_ = nullptr; }
  SaxParser& operator=(SaxParser&& other) noexcept {
    if (this != &other) {
      Reset();
      native_ = other.native_;
      other.native_ = nullptr;
    }
    return *this;
  }

  void Reset(::turbo_yaml_sax_parser_t* native = nullptr) noexcept {
    if (native_ != nullptr) {
      ::turbo_yaml_sax_parser_destroy(native_);
    }
    native_ = native;
  }

  [[nodiscard]] ::turbo_yaml_sax_parser_t* get() const noexcept { return native_; }

 private:
  ::turbo_yaml_sax_parser_t* native_ = nullptr;
};

struct PathResultState {
  std::vector<const ::turbo_yaml_node_t*> nodes;
  std::string error;
  std::size_t error_pos = 0;
};

inline bool collect_path_result_nodes(const ::turbo_yaml_path_result_t* source,
                                     PathResultState& target) {
  try {
    if (source == nullptr) {
      return false;
    }
    const std::size_t count = ::turbo_yaml_path_result_size(source);
    target.nodes.clear();
    target.nodes.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const ::turbo_yaml_node_t* node = ::turbo_yaml_path_result_get(source, index);
      if (node == nullptr) {
        target.nodes.clear();
        target.error.clear();
        target.error_pos = 0;
        return false;
      }
      target.nodes.push_back(node);
    }
    const char* message = ::turbo_yaml_path_result_error(source);
    target.error = message == nullptr ? std::string{} : message;
    target.error_pos = ::turbo_yaml_path_result_error_pos(source);
    return true;
  } catch (...) {
    target.nodes.clear();
    target.error.clear();
    target.error_pos = 0;
    return false;
  }
}

}  // namespace detail

using turbo_yaml_doc_t = detail::Document;
using turbo_yaml_sax_parser_t = detail::SaxParser;
using turbo_yaml_path_result_t = detail::PathResultState;
using turbo_yaml_ypath_result_t = turbo_yaml_path_result_t;

inline std::size_t turbo_yaml_path_result_size(const turbo_yaml_path_result_t* result);
inline const ::turbo_yaml_node_t* turbo_yaml_path_result_get(const turbo_yaml_path_result_t* result,
                                                           std::size_t index);
inline const char* turbo_yaml_path_result_error(const turbo_yaml_path_result_t* result);
inline std::size_t turbo_yaml_path_result_error_pos(const turbo_yaml_path_result_t* result);
inline void turbo_yaml_path_result_free(turbo_yaml_path_result_t* result);

inline int turbo_parse_yaml(const std::uint8_t* data, std::size_t len, turbo_yaml_doc_t** out) {
  if (out == nullptr || data == nullptr) {
    return -1;
  }
  *out = nullptr;
  ::turbo_yaml_doc_t* native = nullptr;
  const int result = ::turbo_parse_yaml(data, len, &native);
  if (result != 0 || native == nullptr) {
    if (native != nullptr) {
      ::turbo_free_yaml(&native);
    }
    return result;
  }
  try {
    *out = new turbo_yaml_doc_t(native);
  } catch (...) {
    ::turbo_free_yaml(&native);
    return -1;
  }
  return 0;
}

inline int turbo_parse_yaml_ex(const std::uint8_t* data, std::size_t len, turbo_yaml_doc_t** out,
                              turbo_yaml_error_t* error) {
  if (out == nullptr || data == nullptr) {
    return -1;
  }
  *out = nullptr;
  ::turbo_yaml_doc_t* native = nullptr;
  const int result = ::turbo_parse_yaml_ex(data, len, &native, error);
  if (result != 0 || native == nullptr) {
    if (native != nullptr) {
      ::turbo_free_yaml(&native);
    }
    return result;
  }
  try {
    *out = new turbo_yaml_doc_t(native);
  } catch (...) {
    ::turbo_free_yaml(&native);
    return -1;
  }
  return 0;
}

inline int turbo_parse_yaml_sax(const std::uint8_t* data, std::size_t len,
                               const ::turbo_yaml_sax_handler_t* handler, void* ctx) {
  if (data == nullptr) {
    return -1;
  }
  return ::turbo_parse_yaml_sax(data, len, handler, ctx) == 0 ? 0 : -1;
}

inline turbo_yaml_sax_parser_t* turbo_yaml_sax_parser_create(
    const ::turbo_yaml_sax_handler_t* handler, void* ctx) {
  auto* state = new turbo_yaml_sax_parser_t();
  state->Reset(::turbo_yaml_sax_parser_create(handler, ctx));
  if (state->get() == nullptr) {
    delete state;
    return nullptr;
  }
  return state;
}

inline int turbo_yaml_sax_parser_feed(turbo_yaml_sax_parser_t* parser, const char* data,
                                     std::size_t len) {
  if (parser == nullptr || parser->get() == nullptr || data == nullptr) {
    return -1;
  }
  return ::turbo_yaml_sax_parser_feed(parser->get(), data, len);
}

inline int turbo_yaml_sax_parser_finish(turbo_yaml_sax_parser_t* parser) {
  if (parser == nullptr || parser->get() == nullptr) {
    return -1;
  }
  return ::turbo_yaml_sax_parser_finish(parser->get());
}

inline const char* turbo_yaml_sax_parser_error(const turbo_yaml_sax_parser_t* parser) {
  if (parser == nullptr || parser->get() == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_sax_parser_error(parser->get());
}

inline void turbo_yaml_sax_parser_destroy(turbo_yaml_sax_parser_t* parser) {
  if (parser == nullptr) {
    return;
  }
  parser->Reset();
  delete parser;
}

inline void turbo_free_yaml(turbo_yaml_doc_t** out) {
  if (out == nullptr || *out == nullptr) {
    return;
  }
  (*out)->Reset();
  delete *out;
  *out = nullptr;
}

inline const ::turbo_yaml_node_t* turbo_yaml_root(const turbo_yaml_doc_t* doc) {
  if (doc == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_root(doc->get());
}

inline ::turbo_yaml_node_type_t turbo_yaml_node_type(const ::turbo_yaml_node_t* node) {
  return ::turbo_yaml_node_type(node);
}

inline ::turbo_yaml_scalar_kind_t turbo_yaml_scalar_kind(const turbo_yaml_doc_t* doc,
                                                       const ::turbo_yaml_node_t* node) {
  if (doc == nullptr || node == nullptr) {
    return TURBO_YAML_SCALAR_NULL;
  }
  return ::turbo_yaml_scalar_kind(doc->get(), node);
}

inline char* turbo_yaml_scalar_dup(const turbo_yaml_doc_t* doc, const ::turbo_yaml_node_t* node) {
  if (doc == nullptr || node == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_scalar_dup(doc->get(), node);
}

inline std::size_t turbo_yaml_sequence_size(const ::turbo_yaml_node_t* node) {
  return ::turbo_yaml_sequence_size(node);
}

inline const ::turbo_yaml_node_t* turbo_yaml_sequence_get(const ::turbo_yaml_node_t* node,
                                                        std::size_t index) {
  return ::turbo_yaml_sequence_get(node, index);
}

inline std::size_t turbo_yaml_mapping_size(const ::turbo_yaml_node_t* node) {
  return ::turbo_yaml_mapping_size(node);
}

inline const ::turbo_yaml_node_t* turbo_yaml_mapping_key(const ::turbo_yaml_node_t* node,
                                                        std::size_t index) {
  return ::turbo_yaml_mapping_key(node, index);
}

inline const ::turbo_yaml_node_t* turbo_yaml_mapping_value(const ::turbo_yaml_node_t* node,
                                                          std::size_t index) {
  return ::turbo_yaml_mapping_value(node, index);
}

inline const ::turbo_yaml_node_t* turbo_yaml_mapping_get(const turbo_yaml_doc_t* doc,
                                                        const ::turbo_yaml_node_t* node,
                                                        const char* key) {
  if (doc == nullptr || node == nullptr || key == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_mapping_get(doc->get(), node, key);
}

inline bool turbo_yaml_mapping_contains(const turbo_yaml_doc_t* doc, const ::turbo_yaml_node_t* node,
                                      const char* key) {
  if (doc == nullptr || node == nullptr || key == nullptr) {
    return false;
  }
  return ::turbo_yaml_mapping_contains(doc->get(), node, key);
}

inline bool turbo_yaml_node_location(const ::turbo_yaml_node_t* node, ::turbo_yaml_location_t* location) {
  return ::turbo_yaml_node_location(node, location);
}

inline const ::turbo_yaml_node_t* turbo_yaml_alias_target(const ::turbo_yaml_node_t* node) {
  return ::turbo_yaml_alias_target(node);
}

inline turbo_yaml_path_result_t* turbo_yaml_path_query(const turbo_yaml_doc_t* doc,
                                                      const ::turbo_yaml_node_t* context,
                                                      const char* expr) {
  if (doc == nullptr || expr == nullptr) {
    return nullptr;
  }
  ::turbo_yaml_path_result_t* native_result =
      ::turbo_yaml_path_query(doc->get(), context, expr);
  if (native_result == nullptr) {
    return nullptr;
  }
  auto* result = new (std::nothrow) turbo_yaml_path_result_t();
  if (result == nullptr) {
    ::turbo_yaml_path_result_free(native_result);
    return nullptr;
  }
  if (!detail::collect_path_result_nodes(native_result, *result)) {
    delete result;
    ::turbo_yaml_path_result_free(native_result);
    return nullptr;
  }
  ::turbo_yaml_path_result_free(native_result);
  return result;
}

inline turbo_yaml_path_result_t* turbo_yaml_path_query_ex(
    const turbo_yaml_doc_t* doc, const ::turbo_yaml_node_t* context, const char* expr,
    const ::turbo_query_limits_t* limits, ::turbo_query_diagnostic_t* diagnostic) {
  if (doc == nullptr || expr == nullptr) {
    return nullptr;
  }
  ::turbo_yaml_path_result_t* native_result =
      ::turbo_yaml_path_query_ex(doc->get(), context, expr, limits, diagnostic);
  if (native_result == nullptr) {
    return nullptr;
  }
  auto* result = new (std::nothrow) turbo_yaml_path_result_t();
  if (result == nullptr) {
    ::turbo_yaml_path_result_free(native_result);
    return nullptr;
  }
  if (!detail::collect_path_result_nodes(native_result, *result)) {
    delete result;
    ::turbo_yaml_path_result_free(native_result);
    return nullptr;
  }
  ::turbo_yaml_path_result_free(native_result);
  return result;
}

inline turbo_yaml_ypath_result_t* turbo_ypath_query(const turbo_yaml_doc_t* doc,
                                                    const ::turbo_yaml_node_t* context,
                                                    const char* expr) {
  return turbo_yaml_path_query(doc, context, expr);
}

inline turbo_yaml_ypath_result_t* turbo_ypath_query_ex(
    const turbo_yaml_doc_t* doc, const ::turbo_yaml_node_t* context, const char* expr,
    const ::turbo_query_limits_t* limits, ::turbo_query_diagnostic_t* diagnostic) {
  return turbo_yaml_path_query_ex(doc, context, expr, limits, diagnostic);
}

inline std::size_t turbo_ypath_result_size(const turbo_yaml_ypath_result_t* result) {
  return turbo_yaml_path_result_size(result);
}

inline const ::turbo_yaml_node_t* turbo_ypath_result_get(const turbo_yaml_ypath_result_t* result,
                                                       std::size_t index) {
  return turbo_yaml_path_result_get(result, index);
}

inline const char* turbo_ypath_result_error(const turbo_yaml_ypath_result_t* result) {
  return turbo_yaml_path_result_error(result);
}

inline std::size_t turbo_ypath_result_error_pos(const turbo_yaml_ypath_result_t* result) {
  return turbo_yaml_path_result_error_pos(result);
}

inline void turbo_ypath_result_free(turbo_yaml_ypath_result_t* result) {
  turbo_yaml_path_result_free(result);
}

inline std::size_t turbo_yaml_path_result_size(const turbo_yaml_path_result_t* result) {
  return result == nullptr ? 0 : result->nodes.size();
}

inline const ::turbo_yaml_node_t* turbo_yaml_path_result_get(const turbo_yaml_path_result_t* result,
                                                           std::size_t index) {
  return result == nullptr || index >= result->nodes.size() ? nullptr : result->nodes[index];
}

inline const char* turbo_yaml_path_result_error(const turbo_yaml_path_result_t* result) {
  return result == nullptr || result->error.empty() ? nullptr : result->error.c_str();
}

inline std::size_t turbo_yaml_path_result_error_pos(const turbo_yaml_path_result_t* result) {
  return result == nullptr ? 0 : result->error_pos;
}

inline void turbo_yaml_path_result_free(turbo_yaml_path_result_t* result) {
  delete result;
}

inline char* turbo_yaml_emit(const turbo_yaml_doc_t* doc, std::size_t* out_len) {
  if (doc == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_emit(doc->get(), out_len);
}

inline char* turbo_yaml_serialize(const turbo_yaml_doc_t* doc, std::size_t* out_len) {
  if (doc == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_serialize(doc->get(), out_len);
}

inline char* turbo_yaml_emit_node(const turbo_yaml_doc_t* doc, const ::turbo_yaml_node_t* node,
                                 std::size_t* out_len) {
  if (doc == nullptr || node == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_emit_node(doc->get(), node, out_len);
}

inline void turbo_yaml_string_free(char* str) { ::turbo_yaml_string_free(str); }

inline void turbo_yaml_serialize_free(char* str) { ::turbo_yaml_serialize_free(str); }

inline int turbo_yaml_write(const turbo_yaml_doc_t* doc, ::turbo_write_fn write, void* user) {
  if (doc == nullptr || write == nullptr) {
    return -1;
  }
  return ::turbo_yaml_write(doc->get(), write, user);
}

inline turbo_yaml_doc_t* turbo_yaml_from_json(const json_value_t* value) {
  ::turbo_yaml_doc_t* native = ::turbo_yaml_from_json(value);
  if (native == nullptr) {
    return nullptr;
  }
  try {
    return new turbo_yaml_doc_t(native);
  } catch (...) {
    ::turbo_free_yaml(&native);
    return nullptr;
  }
}

inline ::json_value_t* turbo_yaml_to_json(const turbo_yaml_doc_t* doc) {
  if (doc == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_to_json(doc->get());
}

inline ::json_value_t* turbo_yaml_node_to_json(const turbo_yaml_doc_t* doc,
                                              const ::turbo_yaml_node_t* node) {
  if (doc == nullptr || node == nullptr) {
    return nullptr;
  }
  return ::turbo_yaml_node_to_json(doc->get(), node);
}

}  // namespace compat

using namespace compat;
 

}  // namespace turbo::yaml
