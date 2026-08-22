#ifndef TURBO_PARSER_TOON_H
#define TURBO_PARSER_TOON_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TOON Parser */
typedef struct toonObject turbo_toon_node_t;

typedef enum {
  TURBO_TOON_STRING = 0,
  TURBO_TOON_INT = 1,
  TURBO_TOON_BOOL = 2,
  TURBO_TOON_NULL = 3,
  TURBO_TOON_DOUBLE = 4,
  TURBO_TOON_OBJECT = 5,
  TURBO_TOON_LIST = 6
} turbo_toon_type_t;

/**
 * @brief Parse TOON (Turbo Object Notation) data.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (turbo_toon_node_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
TURBO_PARSER_API int turbo_parse_toon(const uint8_t *data, size_t len, void *out);

/**
 * @brief Free TOON data and set pointer to NULL.
 * @param out Address of the pointer (turbo_toon_node_t **) to free.
 */
TURBO_PARSER_API void turbo_free_toon(void *out);

/**
 * @brief Get the type of a TOON node.
 * @param node Pointer to the TOON node.
 * @return The node type code.
 */
TURBO_PARSER_API turbo_toon_type_t turbo_toon_type(const turbo_toon_node_t *node);

/**
 * @brief Check if a TOON node represents a null value.
 * @param node Pointer to the TOON node.
 * @return true if null, false otherwise.
 */
TURBO_PARSER_API bool turbo_toon_is_null(const turbo_toon_node_t *node);

/**
 * @brief Get boolean value from a TOON boolean node.
 * @param node Pointer to the TOON node.
 * @return The boolean value.
 */
TURBO_PARSER_API bool turbo_toon_bool(const turbo_toon_node_t *node);

/**
 * @brief Get numeric value from a TOON node.
 * @param node Pointer to the TOON node.
 * @return The numeric value as a double.
 */
TURBO_PARSER_API double turbo_toon_number(const turbo_toon_node_t *node);

/**
 * @brief Get integer value from a TOON node.
 * @param node Pointer to the TOON node.
 * @return The numeric value as an integer.
 */
TURBO_PARSER_API int turbo_toon_int(const turbo_toon_node_t *node);

/**
 * @brief Get string value from a TOON string node.
 * @param node Pointer to the TOON node.
 * @return Pointer to the string data.
 */
TURBO_PARSER_API const char *turbo_toon_string(const turbo_toon_node_t *node);

/**
 * @brief Get the length of a TOON string node.
 * @param node Pointer to the TOON node.
 * @return The length of the string in bytes.
 */
TURBO_PARSER_API size_t turbo_toon_string_len(const turbo_toon_node_t *node);

/**
 * @brief Navigate to a child node using a path string (e.g., "server.host").
 * @param root Pointer to the root TOON node.
 * @param path Path string.
 * @return Pointer to the target node if found, NULL otherwise.
 */
TURBO_PARSER_API turbo_toon_node_t *turbo_toon_get(turbo_toon_node_t *root, const char *path);

/**
 * @brief Get the number of elements in a TOON array (list) node.
 * @param arr Pointer to the TOON array node.
 * @return Number of elements.
 */
TURBO_PARSER_API size_t turbo_toon_array_size(const turbo_toon_node_t *arr);

/**
 * @brief Get an element from a TOON array node by index.
 * @param arr Pointer to the TOON array node.
 * @param index Element index.
 * @return Pointer to the element node.
 */
TURBO_PARSER_API turbo_toon_node_t *turbo_toon_array_get(const turbo_toon_node_t *arr, size_t index);

/**
 * @brief Serialize a TOON node to its string representation.
 * @param node Pointer to the TOON node.
 * @param out_len Optional pointer to store the output string length.
 * @return Pointer to the allocated string (must be freed with turbo_toon_serialize_free).
 */
TURBO_PARSER_API char *turbo_toon_serialize(const turbo_toon_node_t *node, size_t *out_len);

/**
 * @brief Free a string allocated by turbo_toon_serialize.
 * @param str Pointer to the serialized string.
 */
TURBO_PARSER_API void turbo_toon_serialize_free(char *str);

/**
 * @brief Serialize a TOON node to a JSON formatted string.
 * @param node Pointer to the TOON node.
 * @param out_len Optional pointer to store the output string length.
 * @return Pointer to the allocated string (must be freed with turbo_toon_serialize_json_free).
 */
TURBO_PARSER_API char *turbo_toon_serialize_json(const turbo_toon_node_t *node, size_t *out_len);

/**
 * @brief Free a string allocated by turbo_toon_serialize_json.
 * @param str Pointer to the JSON string.
 */
TURBO_PARSER_API void turbo_toon_serialize_json_free(char *str);

/**
 * @brief Parse a JSON string into a TOON structure.
 * @param json Input JSON string.
 * @param len JSON string length.
 * @return Pointer to the root TOON node.
 */
TURBO_PARSER_API turbo_toon_node_t *turbo_toon_from_json(const char *json, size_t len);

/**
 * @brief Convert a JSON DOM into an independently owned TOON tree.
 * @param json Borrowed JSON document; must not be mutated during conversion.
 * @param out Receives a TOON root released with turbo_free_toon().
 * @return TURBO_OK on success, otherwise a TURBO_E* error code. On failure,
 *         *out is NULL.
 */
TURBO_PARSER_API int turbo_toon_from_json_doc(const turbo_json_doc_t *json,
                                       turbo_toon_node_t **out);

/**
 * @brief Convert a TOON tree into an independently owned JSON DOM.
 * @param toon Borrowed TOON root; must not be mutated during conversion.
 * @param out Receives a JSON document released with turbo_free_json().
 * @return TURBO_OK on success, otherwise a TURBO_E* error code. On failure,
 *         *out is NULL.
 */
TURBO_PARSER_API int turbo_toon_to_json_doc(const turbo_toon_node_t *toon,
                                     turbo_json_doc_t **out);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_TOON_H

