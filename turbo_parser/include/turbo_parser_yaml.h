#ifndef TURBO_PARSER_YAML_H
#define TURBO_PARSER_YAML_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* YAML Parser (CYAML). Documents own a copy of the parsed input. Nodes and
 * YPATH matches are
 * non-owning and remain valid until their document is freed. */
typedef struct turbo_yaml_doc_s turbo_yaml_doc_t;
typedef struct turbo_yaml_node_s turbo_yaml_node_t;
typedef struct turbo_yaml_path_result_s turbo_yaml_path_result_t;
typedef struct turbo_yaml_sax_parser_s turbo_yaml_sax_parser_t;

typedef enum {
  TURBO_YAML_NODE_NONE = 0,
  TURBO_YAML_NODE_NULL,
  TURBO_YAML_NODE_SCALAR,
  TURBO_YAML_NODE_SEQUENCE,
  TURBO_YAML_NODE_MAPPING,
  TURBO_YAML_NODE_ALIAS
} turbo_yaml_node_type_t;

typedef enum {
  TURBO_YAML_SCALAR_NULL = 0,
  TURBO_YAML_SCALAR_BOOL,
  TURBO_YAML_SCALAR_INT,
  TURBO_YAML_SCALAR_FLOAT,
  TURBO_YAML_SCALAR_STRING
} turbo_yaml_scalar_kind_t;

typedef enum {
  TURBO_YAML_ERROR_NONE = 0,
  TURBO_YAML_ERROR_INVALID_ARGUMENT,
  TURBO_YAML_ERROR_OUT_OF_MEMORY,
  TURBO_YAML_ERROR_SYNTAX,
  TURBO_YAML_ERROR_UNEXPECTED_END,
  TURBO_YAML_ERROR_INDENTATION,
  TURBO_YAML_ERROR_ESCAPE,
  TURBO_YAML_ERROR_ANCHOR,
  TURBO_YAML_ERROR_ALIAS,
  TURBO_YAML_ERROR_TAG,
  TURBO_YAML_ERROR_DUPLICATE_KEY,
  TURBO_YAML_ERROR_IO,
  TURBO_YAML_ERROR_INPUT_TOO_LARGE
} turbo_yaml_error_code_t;

typedef struct turbo_yaml_location_s {
  size_t offset;
  size_t length;
  uint32_t start_line;
  uint32_t start_column;
  uint32_t end_line;
  uint32_t end_column;
} turbo_yaml_location_t;

typedef struct turbo_yaml_error_s {
  turbo_yaml_error_code_t code;
  turbo_yaml_location_t location;
  char message[128];
} turbo_yaml_error_t;

typedef struct turbo_yaml_sax_handler_s {
  int (*on_document_start)(void *ctx);
  int (*on_document_end)(void *ctx);
  int (*on_null)(void *ctx, bool is_key);
  int (*on_scalar)(void *ctx, turbo_yaml_scalar_kind_t kind, const char *value, size_t value_len,
                   bool is_key);
  int (*on_sequence_start)(void *ctx, bool is_key);
  int (*on_sequence_end)(void *ctx, bool is_key);
  int (*on_mapping_start)(void *ctx, bool is_key);
  int (*on_mapping_end)(void *ctx, bool is_key);
  int (*on_alias)(void *ctx, const char *value, size_t value_len, bool is_key);
} turbo_yaml_sax_handler_t;

/** Parse one YAML document into an owned document handle. */
CXX_C_API int turbo_parse_yaml(const uint8_t *data, size_t len, turbo_yaml_doc_t **out);
/**
 * Parse one YAML document and copy diagnostics into caller-owned storage.
 * On failure, out is set to NULL and error contains a stable code, location,
 * and message. error may be NULL when diagnostics are not needed.
 */
CXX_C_API int turbo_parse_yaml_ex(const uint8_t *data, size_t len, turbo_yaml_doc_t **out,
                                  turbo_yaml_error_t *error);
CXX_C_API int turbo_parse_yaml_sax(const uint8_t *data, size_t len,
                                   const turbo_yaml_sax_handler_t *handler, void *ctx);
/**
 * Create a chunk-fed YAML SAX parser. feed() parses incrementally and may
 * invoke callbacks before returning. Callback string views are valid only
 * during the callback. finish() marks EOF and validates the parser state.
 */
CXX_C_API turbo_yaml_sax_parser_t *
turbo_yaml_sax_parser_create(const turbo_yaml_sax_handler_t *handler, void *ctx);
CXX_C_API int turbo_yaml_sax_parser_feed(turbo_yaml_sax_parser_t *parser, const char *data,
                                         size_t len);
CXX_C_API int turbo_yaml_sax_parser_finish(turbo_yaml_sax_parser_t *parser);
CXX_C_API const char *turbo_yaml_sax_parser_error(const turbo_yaml_sax_parser_t *parser);
CXX_C_API void turbo_yaml_sax_parser_destroy(turbo_yaml_sax_parser_t *parser);

/** Free a YAML document and set its pointer to NULL. */
CXX_C_API void turbo_free_yaml(turbo_yaml_doc_t **doc);

CXX_C_API turbo_yaml_node_t *turbo_yaml_root(const turbo_yaml_doc_t *doc);
CXX_C_API turbo_yaml_node_type_t turbo_yaml_node_type(const turbo_yaml_node_t *node);
CXX_C_API turbo_yaml_scalar_kind_t turbo_yaml_scalar_kind(const turbo_yaml_doc_t *doc,
                                                          const turbo_yaml_node_t *node);

/** Return a processed scalar string. Free it with turbo_yaml_string_free(). */
CXX_C_API char *turbo_yaml_scalar_dup(const turbo_yaml_doc_t *doc, const turbo_yaml_node_t *node);
CXX_C_API size_t turbo_yaml_sequence_size(const turbo_yaml_node_t *node);
CXX_C_API turbo_yaml_node_t *turbo_yaml_sequence_get(const turbo_yaml_node_t *node, size_t index);
CXX_C_API size_t turbo_yaml_mapping_size(const turbo_yaml_node_t *node);
CXX_C_API turbo_yaml_node_t *turbo_yaml_mapping_key(const turbo_yaml_node_t *node, size_t index);
CXX_C_API turbo_yaml_node_t *turbo_yaml_mapping_value(const turbo_yaml_node_t *node, size_t index);
/** Return a borrowed mapping value for an exact, null-terminated key. */
CXX_C_API turbo_yaml_node_t *turbo_yaml_mapping_get(const turbo_yaml_doc_t *doc,
                                                    const turbo_yaml_node_t *node, const char *key);
CXX_C_API bool turbo_yaml_mapping_contains(const turbo_yaml_doc_t *doc,
                                           const turbo_yaml_node_t *node, const char *key);

/** Copy source coordinates for a borrowed node into caller-owned storage. */
CXX_C_API bool turbo_yaml_node_location(const turbo_yaml_node_t *node,
                                        turbo_yaml_location_t *location);
/** Return the borrowed resolved target of an alias node, or NULL. */
CXX_C_API turbo_yaml_node_t *turbo_yaml_alias_target(const turbo_yaml_node_t *node);

/** Execute a YPATH expression relative to context, or the root when context is NULL. */
CXX_C_API turbo_yaml_path_result_t *turbo_yaml_path_query(const turbo_yaml_doc_t *doc,
                                                           const turbo_yaml_node_t *context,
                                                           const char *expr);
/** Execute YPath with copied per-call limits. The returned handle owns only its
 * match array; nodes remain borrowed from doc. Query errors are available both
 * in the result and in diagnostic. */
CXX_C_API turbo_yaml_path_result_t *turbo_yaml_path_query_ex(
    const turbo_yaml_doc_t *doc, const turbo_yaml_node_t *context,
    const char *expr, const turbo_query_limits_t *limits,
    turbo_query_diagnostic_t *diagnostic);
CXX_C_API size_t turbo_yaml_path_result_size(const turbo_yaml_path_result_t *result);
CXX_C_API turbo_yaml_node_t *turbo_yaml_path_result_get(const turbo_yaml_path_result_t *result,
                                                        size_t index);
CXX_C_API const char *turbo_yaml_path_result_error(const turbo_yaml_path_result_t *result);
CXX_C_API size_t turbo_yaml_path_result_error_pos(const turbo_yaml_path_result_t *result);
CXX_C_API void turbo_yaml_path_result_free(turbo_yaml_path_result_t *result);

/** Emit a document or node as YAML. Free serialized output with turbo_yaml_serialize_free(). */
CXX_C_API char *turbo_yaml_emit(const turbo_yaml_doc_t *doc, size_t *out_len);
CXX_C_API char *turbo_yaml_serialize(const turbo_yaml_doc_t *doc, size_t *out_len);
CXX_C_API char *turbo_yaml_emit_node(const turbo_yaml_doc_t *doc, const turbo_yaml_node_t *node,
                                     size_t *out_len);
CXX_C_API void turbo_yaml_string_free(char *str);
CXX_C_API void turbo_yaml_serialize_free(char *str);
CXX_C_API int turbo_yaml_write(const turbo_yaml_doc_t *doc, turbo_write_fn write, void *user);
CXX_C_API turbo_yaml_doc_t *turbo_yaml_from_json(const json_value_t *value);

/** Convert representable YAML semantics into an independently owned JSON DOM. */
CXX_C_API json_value_t *turbo_yaml_to_json(const turbo_yaml_doc_t *doc);
CXX_C_API json_value_t *turbo_yaml_node_to_json(const turbo_yaml_doc_t *doc,
                                                const turbo_yaml_node_t *node);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_YAML_H

