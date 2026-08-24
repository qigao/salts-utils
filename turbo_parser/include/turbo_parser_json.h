#ifndef TURBO_PARSER_JSON_H
#define TURBO_PARSER_JSON_H
#include <turbo_parser_common.h>
#include <cserde/reader.h>

#ifdef __cplusplus
extern "C" {
#endif

/* JSON Parser */
typedef struct json_value_s json_value_t;
typedef struct json_value_s turbo_json_doc_t;
typedef struct json_path_result_s turbo_json_path_result_t;
typedef struct json_path_program_s turbo_json_path_program_t;
typedef struct turbo_json_sax_parser_s turbo_json_sax_parser_t;
typedef struct turbo_json_path_stream_s turbo_json_path_stream_t;

typedef enum {
  TURBO_JSON_NULL,
  TURBO_JSON_BOOL,
  TURBO_JSON_NUMBER,
  TURBO_JSON_STRING,
  TURBO_JSON_ARRAY,
  TURBO_JSON_OBJECT
} turbo_json_type_t;

typedef struct turbo_json_sax_handler_s {
  int (*on_null)(void *ctx);
  int (*on_bool)(void *ctx, bool val);
  int (*on_number)(void *ctx, double val);
  int (*on_string)(void *ctx, const char *val, size_t len);
  int (*on_object_start)(void *ctx);
  int (*on_object_key)(void *ctx, const char *key, size_t len);
  int (*on_object_end)(void *ctx);
  int (*on_array_start)(void *ctx);
  int (*on_array_end)(void *ctx);
} turbo_json_sax_handler_t;

/** JSON SAX callbacks with an exact, borrowed number token.
 * The number slice is valid only for the duration of on_number().
 */
typedef struct turbo_json_sax_handler_raw_s {
  int (*on_null)(void *ctx);
  int (*on_bool)(void *ctx, bool val);
  int (*on_number)(void *ctx, const char *val, size_t len);
  int (*on_string)(void *ctx, const char *val, size_t len);
  int (*on_object_start)(void *ctx);
  int (*on_object_key)(void *ctx, const char *key, size_t len);
  int (*on_object_end)(void *ctx);
  int (*on_array_start)(void *ctx);
  int (*on_array_end)(void *ctx);
} turbo_json_sax_handler_raw_t;

/** Events emitted only for subtrees selected by a streamable JSONPath program. */
typedef struct turbo_json_path_stream_handler_s {
  int (*on_match_start)(void *ctx, turbo_json_type_t type);
  int (*on_match_end)(void *ctx, turbo_json_type_t type);
  turbo_json_sax_handler_raw_t events;
} turbo_json_path_stream_handler_t;

/**
 * @brief Parse JSON data.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (json_value_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
TURBO_PARSER_API int turbo_parse_json(const uint8_t *data, size_t len, turbo_json_doc_t **out);

/**
 * @brief Create a bounded CSerde pull reader over a parsed JSON document.
 *
 * The reader borrows root and never modifies or frees it. The document must
 * remain alive and unmodified while reading. STRING tokens and object keys are
 * stable borrowed slices whose lifetime ends when root is modified or freed,
 * not when the reader is destroyed. The reader is single-threaded and emits
 * one document in object insertion order.
 *
 * JSON numbers without a decimal point or exponent emit CSERDE_SINT when
 * negative and CSERDE_UINT otherwise; decimal or exponent forms emit
 * CSERDE_FLOAT. Builder values without retained source text use the same %.17g
 * representation as turbo_json_create_number(), keeping equivalent double builder
 * paths consistent.
 *
 * @param root Borrowed JSON document; must not be NULL.
 * @param max_depth Maximum simultaneously open array/object count. Zero accepts
 * scalar roots; exceeding the bound returns CSERDE_LIMIT_EXCEEDED from the
 * reader and latches that failure.
 * @return Owned CSerde reader, or NULL for invalid input, allocation-size
 * overflow, or allocation failure. Release it with
 * turbo_json_cserde_reader_destroy().
 *
 * @note Integers outside int64_t/uint64_t and non-finite floating values return
 * CSERDE_VALUE_OUT_OF_RANGE. Invalid DOM state or number formatting failure
 * returns CSERDE_SOURCE_ERROR. Any provider failure is latched by the CSerde
 * reader core and returned by subsequent reads.
 *
 * @code
 * turbo_json_doc_t *root = NULL;
 * turbo_parse_json((const uint8_t *)"{\"id\":7}", 8, &root);
 * cserde_reader *reader = turbo_json_cserde_reader_create(root, 16);
 * cserde_token token;
 * while (cserde_reader_next(reader, &token) == CSERDE_OK) {
 *   consume_token(&token);
 * }
 * turbo_json_cserde_reader_destroy(reader);
 * turbo_free_json(&root);
 * @endcode
 */
TURBO_PARSER_API cserde_reader *
turbo_json_cserde_reader_create(const turbo_json_doc_t *root, size_t max_depth);

/**
 * @brief Destroy a JSON CSerde reader without freeing its borrowed document.
 * @param reader Reader returned by turbo_json_cserde_reader_create(); NULL is accepted.
 */
TURBO_PARSER_API void turbo_json_cserde_reader_destroy(cserde_reader *reader);

/**
 * @brief Parse one complete JSON document with SAX callbacks.
 * @param data Input buffer.
 *
 * @param len Buffer length.
 * @param handler Callback table.
 * @param ctx User context passed to
 * callbacks.
 * @return 0 on success, -1 on parse or callback failure.
 */
TURBO_PARSER_API int turbo_parse_json_sax(const uint8_t *data, size_t len,
                                   const turbo_json_sax_handler_t *handler, void *ctx);

/**
 * @brief Parse one complete JSON document while preserving exact number tokens.
 * @param data JSON input buffer; must not be NULL.
 * @param len Input length; must be non-zero.
 * @param handler Raw-number callback table; must not be NULL.
 * @param ctx User context passed unchanged to callbacks.
 * @return 0 on success, -1 on invalid input, syntax error, allocation failure,
 * or a non-zero callback result.
 * Number callbacks receive a borrowed, non-NUL-terminated slice. Other callback
 * values retain the same decoded semantics as turbo_json_sax_handler_t.
 *
 * @code
 * static int number(void *ctx, const char *text, size_t len) {
 *   return consume_exact_number(ctx, text, len);
 * }
 * turbo_json_sax_handler_raw_t h = {.on_number = number};
 * turbo_parse_json_sax_raw(data, len, &h, user);
 * @endcode
 */
TURBO_PARSER_API int turbo_parse_json_sax_raw(const uint8_t *data, size_t len,
                                       const turbo_json_sax_handler_raw_t *handler, void *ctx);

/* Incremental JSON SAX parser. Call feed() with any chunk size, then finish()
 * once at EOF.
 * Callback pointers are valid only for the duration of the
 * callback. Returning non-zero from a
 * callback stops parsing. */
TURBO_PARSER_API turbo_json_sax_parser_t *
turbo_json_sax_parser_create(const turbo_json_sax_handler_t *handler, void *ctx);
/**
 * @brief Create an incremental parser whose number callback receives exact JSON text.
 * @param handler Raw-number callback table; must not be NULL and is copied.
 * @param ctx User context passed unchanged to callbacks.
 * @return Owned parser released by turbo_json_sax_parser_destroy(), or NULL on
 * invalid arguments/allocation failure.
 */
TURBO_PARSER_API turbo_json_sax_parser_t *
turbo_json_sax_parser_create_raw(const turbo_json_sax_handler_raw_t *handler, void *ctx);
TURBO_PARSER_API int turbo_json_sax_parser_feed(turbo_json_sax_parser_t *parser, const char *data,
                                         size_t len);
TURBO_PARSER_API int turbo_json_sax_parser_finish(turbo_json_sax_parser_t *parser);
TURBO_PARSER_API const char *turbo_json_sax_parser_error(const turbo_json_sax_parser_t *parser);
TURBO_PARSER_API void turbo_json_sax_parser_destroy(turbo_json_sax_parser_t *parser);

/**
 * @brief Free JSON data and set pointer to NULL.
 * @param out Address of the pointer
 * (json_value_t **) to free.
 */
TURBO_PARSER_API void turbo_free_json(turbo_json_doc_t **out);

/**
 * @brief Get the type of a JSON value.
 * @param value Pointer to the JSON value.
 * @return The type of the JSON value.
 */
TURBO_PARSER_API turbo_json_type_t turbo_json_type(const json_value_t *value);

/**
 * @brief Check if a JSON value is null.
 * @param value Pointer to the JSON value.
 * @return true if null, false otherwise.
 */
TURBO_PARSER_API bool turbo_json_is_null(const json_value_t *value);

/**
 * @brief Get boolean value from a JSON boolean node.
 * @param value Pointer to the JSON value.
 * @return The boolean value.
 */
TURBO_PARSER_API bool turbo_json_bool(const json_value_t *value);

/**
 * @brief Get numeric value from a JSON number node.
 * @param value Pointer to the JSON value.
 * @return The numeric value as a double.
 */
TURBO_PARSER_API double turbo_json_number(const json_value_t *value);

/**
 * @brief Get the original JSON number token when available.
 * @param value JSON number node.
 * @param len Optional output length. Set to zero when no token is available.
 * @return Borrowed token text, or NULL when value is not a number.
 */
TURBO_PARSER_API const char *turbo_json_number_text(const json_value_t *value, size_t *len);

/**
 * @brief Get string value from a JSON string node.
 * @param value Pointer to the JSON value.
 * @return Pointer to the null-terminated string.
 */
TURBO_PARSER_API const char *turbo_json_string(const json_value_t *value);

/**
 * @brief Get the length of a JSON string.
 * @param value Pointer to the JSON value.
 * @return The length of the string in bytes.
 */
TURBO_PARSER_API size_t turbo_json_string_len(const json_value_t *value);

/**
 * @brief Get the number of properties in a JSON object.
 * @param obj Pointer to the JSON object.
 * @return Number of properties.
 */
TURBO_PARSER_API size_t turbo_json_object_size(const json_value_t *obj);

/**
 * @brief Get the key name of an object property by index.
 * @param obj Pointer to the JSON object.
 * @param index Index of the property.
 * @return Pointer to the key string.
 */
TURBO_PARSER_API const char *turbo_json_object_key(const json_value_t *obj, size_t index);

/**
 * @brief Get the value of an object property by index.
 * @param obj Pointer to the JSON object.
 * @param index Index of the property.
 * @return Pointer to the property value.
 */
TURBO_PARSER_API json_value_t *turbo_json_object_value(const json_value_t *obj, size_t index);

/**
 * @brief Get the value of an object property by key name.
 * @param obj Pointer to the JSON object.
 * @param key Key name to look up.
 * @return Pointer to the value if found, NULL otherwise.
 */
TURBO_PARSER_API json_value_t *turbo_json_object_get(const json_value_t *obj, const char *key);

/**
 * @brief Get the number of elements in a JSON array.
 * @param arr Pointer to the JSON array.
 * @return Number of elements.
 */
TURBO_PARSER_API size_t turbo_json_array_size(const json_value_t *arr);

/**
 * @brief Get an array element by index.
 * @param arr Pointer to the JSON array.
 * @param index Index of the element.
 * @return Pointer to the element value.
 */
TURBO_PARSER_API json_value_t *turbo_json_array_get(const json_value_t *arr, size_t index);

/**
 * @brief Convenience function to get an integer property from an object.
 * @param obj Pointer to the JSON object.
 * @param key Key name.
 * @param def Default value if key not found or not a number.
 * @return Integer value.
 */
TURBO_PARSER_API int turbo_json_get_int(const json_value_t *obj, const char *key, int def);

/**
 * @brief Convenience function to get a boolean property from an object.
 * @param obj Pointer to the JSON object.
 * @param key Key name.
 * @param def Default value if key not found or not a boolean.
 * @return Boolean value.
 */
TURBO_PARSER_API bool turbo_json_get_bool(const json_value_t *obj, const char *key, bool def);

/**
 * @brief Convenience function to get a double property from an object.
 * @param obj Pointer to the JSON object.
 * @param key Key name.
 * @param def Default value if key not found or not a number.
 * @return Double value.
 */
TURBO_PARSER_API double turbo_json_get_double(const json_value_t *obj, const char *key, double def);

/**
 * @brief Convenience function to get a string property from an object.
 * @param obj Pointer to the JSON object.
 * @param key Key name.
 * @return Pointer to string value if found, NULL otherwise.
 */
TURBO_PARSER_API const char *turbo_json_get_string(const json_value_t *obj, const char *key);

/**
 * @brief Serialize JSON structure to a string.
 * @param value Pointer to the JSON value to serialize.
 * @param out_len Optional pointer to store the output string length.
 * @return Pointer to the allocated string (must be freed with turbo_json_serialize_free).
 */
TURBO_PARSER_API char *turbo_json_serialize(const json_value_t *value, size_t *out_len);

/**
 * @brief Serialize a JSON value to a pretty-printed string with indentation.
 * @param value Pointer to the JSON value to serialize.
 * @param out_len Optional pointer to store the output string length.
 * @return Pointer to the allocated string (must be freed with turbo_json_serialize_free).
 */
TURBO_PARSER_API char *turbo_json_serialize_pretty(const json_value_t *value, size_t *out_len);

/**
 * @brief Serialize a JSON value to a pretty-printed string with CRLF line endings.
 * @param value Pointer to the JSON value to serialize.
 * @param out_len Optional pointer to store the output string length.
 * @return Pointer to the allocated string (must be freed with turbo_json_serialize_free).
 */
TURBO_PARSER_API char *turbo_json_serialize_pretty_crlf(const json_value_t *value, size_t *out_len);

/**
 * @brief Free a string allocated by turbo_json_serialize or turbo_json_serialize_pretty.
 *
 * @param str Pointer to the serialized string.
 */
TURBO_PARSER_API void turbo_json_serialize_free(char *str);
TURBO_PARSER_API int turbo_json_write(const json_value_t *value, turbo_write_fn write, void *user);

/**
 * @brief Deep-clone a JSON value tree.
 * @param value Source JSON value.
 * @return Newly
 * allocated clone, or NULL on failure.
 */
TURBO_PARSER_API json_value_t *turbo_json_clone(const json_value_t *value);

/**
 * @brief Get the first JSON value matching a JSONPath expression.
 * @param root Root JSON
 * value.
 * @param expr JSONPath expression.
 * @return First matching value, or NULL if not found
 * or invalid.
 */
TURBO_PARSER_API json_value_t *turbo_json_path_get(const json_value_t *root, const char *expr);

/**
 * @brief Query JSON values matching a JSONPath expression.
 * @param root Root JSON value.
 *
 * @param expr JSONPath expression.
 * @return Result handle containing non-owning JSON value
 * pointers.
 */
TURBO_PARSER_API turbo_json_path_result_t *turbo_json_path_query(const json_value_t *root,
                                                          const char *expr);

/** Compile an owned, reusable JSONPath program. */
TURBO_PARSER_API turbo_json_path_program_t *turbo_json_path_compile(const char *expr);
/** Compile with copied hard limits. diagnostic is caller-owned and receives an
 * owned message; NULL is allowed. Returns NULL for syntax, allocation, or
 * TURBO_QUERY_RESOURCE_LIMIT failure. */
TURBO_PARSER_API turbo_json_path_program_t *turbo_json_path_compile_ex(
    const char *expr, const turbo_query_limits_t *limits,
    turbo_query_diagnostic_t *diagnostic);

/** Execute a compiled program and return a borrowed first match. */
TURBO_PARSER_API json_value_t *turbo_json_path_get_compiled(
    const json_value_t *root, const turbo_json_path_program_t *program);
/** Execute one immutable program with its compile-time limits. The result is
 * borrowed from root; NULL means no match unless diagnostic reports failure. */
TURBO_PARSER_API json_value_t *turbo_json_path_get_compiled_ex(
    const json_value_t *root, const turbo_json_path_program_t *program,
    turbo_query_diagnostic_t *diagnostic);

/** Execute a compiled program and return an owned result handle. */
TURBO_PARSER_API turbo_json_path_result_t *turbo_json_path_query_compiled(
    const json_value_t *root, const turbo_json_path_program_t *program);
/** Execute and collect borrowed matches. Returns an owned result or NULL on a
 * diagnosed query failure. */
TURBO_PARSER_API turbo_json_path_result_t *turbo_json_path_query_compiled_ex(
    const json_value_t *root, const turbo_json_path_program_t *program,
    turbo_query_diagnostic_t *diagnostic);

/** Free a compiled program after all executions have stopped. */
TURBO_PARSER_API void turbo_json_path_program_free(turbo_json_path_program_t *program);

/** Create a no-DOM matcher for key/index/wildcard/union JSONPath programs. */
TURBO_PARSER_API turbo_json_path_stream_t *turbo_json_path_stream_create(
    const turbo_json_path_program_t *program,
    const turbo_json_path_stream_handler_t *handler, void *ctx);
TURBO_PARSER_API int turbo_json_path_stream_feed(turbo_json_path_stream_t *stream,
                                          const char *data, size_t len);
TURBO_PARSER_API int turbo_json_path_stream_finish(turbo_json_path_stream_t *stream);
TURBO_PARSER_API size_t
turbo_json_path_stream_match_count(const turbo_json_path_stream_t *stream);
TURBO_PARSER_API const char *
turbo_json_path_stream_error(const turbo_json_path_stream_t *stream);
TURBO_PARSER_API void turbo_json_path_stream_destroy(turbo_json_path_stream_t *stream);

/**
 * @brief Get number of values in a JSONPath result.
 * @param result JSONPath result handle.
 *
 * @return Match count.
 */
TURBO_PARSER_API size_t turbo_json_path_result_size(const turbo_json_path_result_t *result);

/**
 * @brief Get one value from a JSONPath result.
 * @param result JSONPath result handle.
 *
 * @param index Match index.
 * @return Matching value or NULL.
 */
TURBO_PARSER_API json_value_t *turbo_json_path_result_get(const turbo_json_path_result_t *result,
                                                   size_t index);

/**
 * @brief Free a JSONPath result handle. Does not free matched JSON values.
 * @param result
 * JSONPath result handle.
 */
TURBO_PARSER_API void turbo_json_path_result_free(turbo_json_path_result_t *result);

/**
 * @brief Get last JSONPath error.
 * @return Error string or NULL.
 */
TURBO_PARSER_API const char *turbo_json_path_error(void);

/* JSON Builder/Modifier */
/**
 * @brief Create an empty JSON object.
 * @return Pointer to the newly created JSON object.
 */
TURBO_PARSER_API json_value_t *turbo_json_create_object(void);

/**
 * @brief Create an empty JSON array.
 * @return Pointer to the newly created JSON array.
 */
TURBO_PARSER_API json_value_t *turbo_json_create_array(void);

/**
 * @brief Create a JSON string node.
 * @param str Input null-terminated string.
 * @return Pointer to the newly created JSON string node.
 */
TURBO_PARSER_API json_value_t *turbo_json_create_string(const char *str);
TURBO_PARSER_API json_value_t *turbo_json_create_string_n(const char *str, size_t len);

/**
 * @brief Create a JSON number node.
 * @param num Numeric value.
 * @return Pointer to the newly created JSON number node.
 */
TURBO_PARSER_API json_value_t *turbo_json_create_number(double num);

/** Create a JSON integer whose serialized decimal form preserves all int64 bits. */
TURBO_PARSER_API json_value_t *turbo_json_create_int64(int64_t num);

/** Create a JSON integer whose serialized decimal form preserves all uint64 bits. */
TURBO_PARSER_API json_value_t *turbo_json_create_uint64(uint64_t num);

/**
 * @brief Create a JSON boolean node.
 * @param val Boolean value.
 * @return Pointer to the newly created JSON boolean node.
 */
TURBO_PARSER_API json_value_t *turbo_json_create_bool(bool val);

/**
 * @brief Create a JSON null node.
 * @return Pointer to the newly created JSON null node.
 */
TURBO_PARSER_API json_value_t *turbo_json_create_null(void);

/**
 * @brief Add value to object (takes ownership of val).
 * @param obj Target object.
 * @param key Property key.
 * @param val Value node.
 */
TURBO_PARSER_API void turbo_json_object_add(json_value_t *obj, const char *key, json_value_t *val);
TURBO_PARSER_API bool turbo_json_object_add_checked(json_value_t *obj, const char *key, json_value_t *val);

/**
 * @brief Add value to array (takes ownership of val).
 * @param arr Target array.
 * @param val Value node.
 */
TURBO_PARSER_API void turbo_json_array_add(json_value_t *arr, json_value_t *val);
TURBO_PARSER_API bool turbo_json_array_add_checked(json_value_t *arr, json_value_t *val);

/**
 * @brief Set/Update string property in object.
 * @param obj Target object.
 * @param key Property key.
 * @param val String value.
 */
TURBO_PARSER_API void turbo_json_object_set_string(json_value_t *obj, const char *key, const char *val);

/**
 * @brief Set/Update number property in object.
 * @param obj Target object.
 * @param key Property key.
 * @param val Numeric value.
 */
TURBO_PARSER_API void turbo_json_object_set_number(json_value_t *obj, const char *key, double val);

/**
 * @brief Set/Update boolean property in object.
 * @param obj Target object.
 * @param key Property key.
 * @param val Boolean value.
 */
TURBO_PARSER_API void turbo_json_object_set_bool(json_value_t *obj, const char *key, bool val);

/**
 * @brief Set property to null in object.
 * @param obj Target object.
 * @param key Property key.
 */
TURBO_PARSER_API void turbo_json_object_set_null(json_value_t *obj, const char *key);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_JSON_H
