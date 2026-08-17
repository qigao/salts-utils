#pragma once

#include <turbo_parser.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace turbo::xml {

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
  explicit Document(::turbo_xml_doc_t* native) : native_(native) {}
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

  void Reset(::turbo_xml_doc_t* native = nullptr) noexcept {
    if (native_ != nullptr) {
      ::turbo_free_xml(&native_);
    }
    native_ = native;
  }

  [[nodiscard]] ::turbo_xml_doc_t* get() const noexcept { return native_; }
  explicit operator bool() const noexcept { return native_ != nullptr; }

 private:
  ::turbo_xml_doc_t* native_ = nullptr;
};

class SaxParser {
 public:
  SaxParser() = default;
  explicit SaxParser(::turbo_xml_sax_parser_t* native) : native_(native) {}
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

  void Reset(::turbo_xml_sax_parser_t* native = nullptr) noexcept {
    if (native_ != nullptr) {
      ::turbo_xml_sax_parser_destroy(native_);
    }
    native_ = native;
  }

  [[nodiscard]] ::turbo_xml_sax_parser_t* get() const noexcept { return native_; }

 private:
  ::turbo_xml_sax_parser_t* native_ = nullptr;
};

struct NodeListState {
  std::vector<const void*> items;
};

inline bool collect_xml_list(const ::turbo_xml_list_t& source, NodeListState& target) {
  target.items.clear();
  if (source.len > 0) {
    target.items.reserve(static_cast<std::size_t>(source.len));
  }
  for (const auto* current = source.head; current != nullptr; current = current->next) {
    target.items.push_back(current->item);
  }
  return true;
}

}  // namespace detail

using turbo_xml_doc_t = detail::Document;
using turbo_xml_sax_parser_t = detail::SaxParser;
using turbo_xml_list_t = detail::NodeListState;
using turbo_xpath_list_t = turbo_xml_list_t;
using turbo_xpath_node_t = turbo_xml_xpath_node_t;

inline int turbo_parse_xml(const std::uint8_t* data, std::size_t len, turbo_xml_doc_t** out) {
  if (out == nullptr || data == nullptr) {
    return -1;
  }
  *out = nullptr;
  ::turbo_xml_doc_t* native = nullptr;
  const int result = ::turbo_parse_xml(data, len, &native);
  if (result != 0 || native == nullptr) {
    if (native != nullptr) {
      ::turbo_free_xml(&native);
    }
    return result;
  }
  try {
    *out = new turbo_xml_doc_t(native);
  } catch (...) {
    ::turbo_free_xml(&native);
    return -1;
  }
  return 0;
}

inline int turbo_parse_xml_sax(const std::uint8_t* data, std::size_t len,
                               const ::turbo_xml_sax_handler_t* handler, void* ctx) {
  if (data == nullptr) {
    return -1;
  }
  return ::turbo_parse_xml_sax(data, len, handler, ctx) == 0 ? 0 : -1;
}

inline turbo_xml_sax_parser_t* turbo_xml_sax_parser_create(
    const ::turbo_xml_sax_handler_t* handler, void* ctx) {
  auto* state = new turbo_xml_sax_parser_t();
  state->Reset(::turbo_xml_sax_parser_create(handler, ctx));
  if (state->get() == nullptr) {
    delete state;
    return nullptr;
  }
  return state;
}

inline int turbo_xml_sax_parser_feed(turbo_xml_sax_parser_t* parser, const char* data, std::size_t len) {
  if (parser == nullptr || parser->get() == nullptr || data == nullptr) {
    return -1;
  }
  return ::turbo_xml_sax_parser_feed(parser->get(), data, len);
}

inline int turbo_xml_sax_parser_finish(turbo_xml_sax_parser_t* parser) {
  if (parser == nullptr || parser->get() == nullptr) {
    return -1;
  }
  return ::turbo_xml_sax_parser_finish(parser->get());
}

inline const char* turbo_xml_sax_parser_error(const turbo_xml_sax_parser_t* parser) {
  if (parser == nullptr || parser->get() == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_sax_parser_error(parser->get());
}

inline void turbo_xml_sax_parser_destroy(turbo_xml_sax_parser_t* parser) {
  if (parser == nullptr) {
    return;
  }
  parser->Reset();
  delete parser;
}

inline void turbo_free_xml(turbo_xml_doc_t** out) {
  if (out == nullptr || *out == nullptr) {
    return;
  }
  (*out)->Reset();
  delete *out;
  *out = nullptr;
}

inline char* turbo_xml_serialize(const turbo_xml_doc_t* doc, std::size_t* out_len) {
  if (doc == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_serialize(doc->get(), out_len);
}

inline void turbo_xml_string_free(char* str) { ::turbo_xml_string_free(str); }
inline void turbo_xml_serialize_free(char* str) { ::turbo_xml_serialize_free(str); }

inline int turbo_xml_write(const turbo_xml_doc_t* doc, ::turbo_write_fn write, void* user) {
  if (doc == nullptr || write == nullptr) {
    return -1;
  }
  return ::turbo_xml_write(doc->get(), write, user);
}

inline turbo_xml_doc_t* turbo_xml_create_document(const char* root_name) {
  ::turbo_xml_doc_t* native = ::turbo_xml_create_document(root_name);
  if (native == nullptr) {
    return nullptr;
  }
  try {
    return new turbo_xml_doc_t(native);
  } catch (...) {
    ::turbo_free_xml(&native);
    return nullptr;
  }
}

inline ::turbo_xml_node_t* turbo_xml_add_element(void* parent, const char* name) {
  if (parent == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_add_element(parent, name);
}

inline int turbo_xml_set_text(::turbo_xml_node_t* node, const char* text) {
  if (node == nullptr) {
    return -1;
  }
  return ::turbo_xml_set_text(node, text);
}

inline ::turbo_xml_node_t* turbo_xml_root_element(const turbo_xml_doc_t* doc) {
  if (doc == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_root_element(doc->get());
}

inline const char* turbo_xml_node_name(const ::turbo_xml_node_t* node) {
  return ::turbo_xml_node_name(node);
}

inline void turbo_xml_list_init(turbo_xml_list_t* list) {
  if (list != nullptr) {
    list->items.clear();
  }
}

inline void turbo_xpath_list_init(turbo_xpath_list_t* list) {
  turbo_xml_list_init(list);
}

inline void turbo_xml_list_free(turbo_xml_list_t* list) {
  if (list != nullptr) {
    list->items.clear();
  }
}

inline void turbo_xpath_list_free(turbo_xpath_list_t* list) {
  turbo_xml_list_free(list);
}

inline std::size_t turbo_xml_list_size(const turbo_xml_list_t* list) {
  return list == nullptr ? 0 : list->items.size();
}

inline std::size_t turbo_xpath_list_size(const turbo_xpath_list_t* list) {
  return turbo_xml_list_size(list);
}

inline const ::turbo_xml_node_t* turbo_xml_list_node(const turbo_xml_list_t* list, std::size_t index) {
  return (list == nullptr || index >= list->items.size())
             ? nullptr
             : static_cast<const ::turbo_xml_node_t*>(list->items[index]);
}

inline const ::turbo_xml_xpath_node_t* turbo_xml_list_xpath_node(const turbo_xml_list_t* list,
                                                                std::size_t index) {
  return (list == nullptr || index >= list->items.size())
             ? nullptr
             : static_cast<const ::turbo_xml_xpath_node_t*>(list->items[index]);
}

inline const turbo_xpath_node_t* turbo_xpath_list_node(const turbo_xpath_list_t* list, std::size_t index) {
  return turbo_xml_list_xpath_node(list, index);
}

inline ::turbo_xml_node_t* turbo_xml_find(::turbo_xml_node_t* root, const char* query) {
  if (root == nullptr || query == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_find(root, query);
}

inline void turbo_xml_find_all(::turbo_xml_node_t* root, const char* query, turbo_xml_list_t* out) {
  if (root == nullptr || query == nullptr || out == nullptr) {
    return;
  }
  ::turbo_xml_list_t native{};
  ::turbo_xml_list_init(&native);
  ::turbo_xml_find_all(root, query, &native);
  detail::collect_xml_list(native, *out);
  ::turbo_xml_list_free(&native);
}

inline char* turbo_xml_text_dup(::turbo_xml_node_t* node) {
  if (node == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_text_dup(node);
}

inline char* turbo_xml_child_text_dup(::turbo_xml_node_t* parent, const char* name) {
  if (parent == nullptr || name == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_child_text_dup(parent, name);
}

inline const char* turbo_xml_get_text(const turbo_xml_doc_t* doc, const char* xpath) {
  if (doc == nullptr || xpath == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_get_text(doc->get(), xpath);
}

inline std::size_t turbo_xml_count(const turbo_xml_doc_t* doc, const char* xpath) {
  if (doc == nullptr || xpath == nullptr) {
    return 0;
  }
  return ::turbo_xml_count(doc->get(), xpath);
}

inline ::turbo_xml_xpath_node_t* turbo_xml_xpath_get(const turbo_xml_doc_t* doc, const char* xpath) {
  if (doc == nullptr || xpath == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_xpath_get(doc->get(), xpath);
}

inline void turbo_xml_xpath_query(const turbo_xml_doc_t* doc, const char* xpath,
                                 turbo_xml_list_t* out) {
  if (doc == nullptr || xpath == nullptr || out == nullptr) {
    return;
  }
  ::turbo_xml_list_t native{};
  ::turbo_xml_list_init(&native);
  ::turbo_xml_xpath_query(doc->get(), xpath, &native);
  detail::collect_xml_list(native, *out);
  ::turbo_xml_list_free(&native);
}

inline ::turbo_query_status_t turbo_xml_xpath_query_ex(
    const turbo_xml_doc_t* doc, const char* xpath, turbo_xml_list_t* out,
    const ::turbo_query_limits_t* limits, ::turbo_query_diagnostic_t* diagnostic) {
  if (doc == nullptr || xpath == nullptr || out == nullptr) {
    if (diagnostic != nullptr) {
      *diagnostic = TURBO_QUERY_DIAGNOSTIC_INIT;
      diagnostic->status = TURBO_QUERY_INVALID_ARGUMENT;
    }
    return TURBO_QUERY_INVALID_ARGUMENT;
  }
  ::turbo_xml_list_t native{};
  ::turbo_xml_list_init(&native);
  const ::turbo_query_status_t status =
      ::turbo_xml_xpath_query_ex(doc->get(), xpath, &native, limits, diagnostic);
  detail::collect_xml_list(native, *out);
  ::turbo_xml_list_free(&native);
  return status;
}

inline std::size_t turbo_xml_xpath_count(const turbo_xml_doc_t* doc, const char* xpath) {
  if (doc == nullptr || xpath == nullptr) {
    return 0;
  }
  return ::turbo_xml_xpath_count(doc->get(), xpath);
}

inline const char* turbo_xml_xpath_text(const turbo_xml_doc_t* doc, const char* xpath) {
  if (doc == nullptr || xpath == nullptr) {
    return nullptr;
  }
  return ::turbo_xml_xpath_text(doc->get(), xpath);
}

inline void turbo_xpath_query(const turbo_xml_doc_t* doc, const char* xpath, turbo_xpath_list_t* out) {
  turbo_xml_xpath_query(doc, xpath, out);
}

inline turbo_query_status_t turbo_xpath_query_ex(
    const turbo_xml_doc_t* doc, const char* xpath, turbo_xpath_list_t* out,
    const ::turbo_query_limits_t* limits, ::turbo_query_diagnostic_t* diagnostic) {
  return turbo_xml_xpath_query_ex(doc, xpath, out, limits, diagnostic);
}

inline std::size_t turbo_xpath_count(const turbo_xml_doc_t* doc, const char* xpath) {
  return turbo_xml_xpath_count(doc, xpath);
}

inline const char* turbo_xpath_text(const turbo_xml_doc_t* doc, const char* xpath) {
  return turbo_xml_xpath_text(doc, xpath);
}

inline turbo_xpath_node_t* turbo_xpath_get(const turbo_xml_doc_t* doc, const char* xpath) {
  return turbo_xml_xpath_get(doc, xpath);
}

inline turbo_xml_node_type_t turbo_xpath_node_type(const turbo_xpath_node_t* node) {
  return turbo_xml_xpath_node_type(node);
}

inline const char* turbo_xpath_node_type_name(const turbo_xpath_node_t* node) {
  return turbo_xml_xpath_node_type_name(node);
}

inline const char* turbo_xpath_node_name(const turbo_xpath_node_t* node) {
  return turbo_xml_xpath_node_name(node);
}

inline const char* turbo_xpath_node_text(const turbo_xpath_node_t* node) {
  return turbo_xml_xpath_node_text(node);
}

inline char* turbo_xpath_node_xml_dup(const turbo_xpath_node_t* node) {
  return turbo_xml_xpath_node_xml_dup(node);
}

inline ::turbo_xml_node_type_t turbo_xml_xpath_node_type(const ::turbo_xml_xpath_node_t* node) {
  return ::turbo_xml_xpath_node_type(node);
}

inline const char* turbo_xml_xpath_node_type_name(const ::turbo_xml_xpath_node_t* node) {
  return ::turbo_xml_xpath_node_type_name(node);
}

inline const char* turbo_xml_xpath_node_name(const ::turbo_xml_xpath_node_t* node) {
  return ::turbo_xml_xpath_node_name(node);
}

inline const char* turbo_xml_xpath_node_text(const ::turbo_xml_xpath_node_t* node) {
  return ::turbo_xml_xpath_node_text(node);
}

inline char* turbo_xml_xpath_node_xml_dup(const ::turbo_xml_xpath_node_t* node) {
  return ::turbo_xml_xpath_node_xml_dup(node);
}

}  // namespace compat

using namespace compat;
 
}  // namespace turbo::xml
