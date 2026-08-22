#ifndef TURBO_PARSER_XML_H
#define TURBO_PARSER_XML_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XML Parser (cxml) */
typedef struct _cx_doc_node turbo_xml_doc_t;
typedef struct _cx_elem_node turbo_xml_node_t;
typedef void turbo_xml_xpath_node_t;
typedef struct turbo_xml_sax_parser_s turbo_xml_sax_parser_t;
typedef enum {
  TURBO_XML_NODE_UNKNOWN = 0,
  TURBO_XML_NODE_TEXT,
  TURBO_XML_NODE_ELEMENT,
  TURBO_XML_NODE_COMMENT,
  TURBO_XML_NODE_ATTRIBUTE,
  TURBO_XML_NODE_ROOT,
  TURBO_XML_NODE_PI,
  TURBO_XML_NODE_NAMESPACE,
  TURBO_XML_NODE_XML_HEADER,
  TURBO_XML_NODE_DTD
} turbo_xml_node_type_t;
typedef struct turbo_xml_list_node_s {
  void *item;
  struct turbo_xml_list_node_s *next;
} turbo_xml_list_node_t;
typedef struct turbo_xml_list_s {
  int len;
  turbo_xml_list_node_t *head;
  turbo_xml_list_node_t *tail;
} turbo_xml_list_t;

typedef struct turbo_xml_sax_handler_s {
  int (*on_start_document)(void *ctx);
  int (*on_end_document)(void *ctx);
  int (*on_element_start)(void *ctx, const char *name, size_t name_len);
  int (*on_attribute)(void *ctx, const char *name, size_t name_len, const char *value,
                      size_t value_len);
  int (*on_element_end)(void *ctx, const char *name, size_t name_len);
  int (*on_text)(void *ctx, const char *text, size_t text_len);
  int (*on_comment)(void *ctx, const char *text, size_t text_len);
  int (*on_cdata)(void *ctx, const char *text, size_t text_len);
  int (*on_processing_instruction)(void *ctx, const char *target, size_t target_len,
                                   const char *data, size_t data_len);
  int (*on_doctype)(void *ctx, const char *text, size_t text_len);
} turbo_xml_sax_handler_t;

#define turbo_xml_for(_node, __list)                                                               \
  void *_node = NULL;                                                                              \
  for (turbo_xml_list_node_t *__00prev00##_node = NULL, *__00current00##_node = (__list)->head;    \
       (_node = __00current00##_node ? __00current00##_node->item : NULL,                          \
       __00current00##_node != NULL);                                                              \
       __00prev00##_node = __00current00##_node,                                                   \
                             __00current00##_node =                                                \
                                 (((void)__00prev00##_node), __00current00##_node->next))

/**
 * @brief Parse XML data.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (turbo_xml_doc_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
TURBO_PARSER_API int turbo_parse_xml(const uint8_t *data, size_t len, turbo_xml_doc_t **out);

/**
 * @brief Parse one complete XML document with SAX callbacks.
 * @param data Input buffer.
 *
 * @param len Buffer length.
 * @param handler Callback table.
 * @param ctx User context passed to
 * callbacks.
 * @return 0 on success, -1 on parse or callback failure.
 */
TURBO_PARSER_API int turbo_parse_xml_sax(const uint8_t *data, size_t len,
                                  const turbo_xml_sax_handler_t *handler, void *ctx);

/* Incremental XML SAX parser. Call feed() with any chunk size, then finish() once at EOF.
 *
 * Callback pointers are valid only for the duration of the callback. Returning non-zero
 * from a
 * callback stops parsing. Text and attribute values are raw XML slices. */
TURBO_PARSER_API turbo_xml_sax_parser_t *
turbo_xml_sax_parser_create(const turbo_xml_sax_handler_t *handler, void *ctx);
TURBO_PARSER_API int turbo_xml_sax_parser_feed(turbo_xml_sax_parser_t *parser, const char *data,
                                        size_t len);
TURBO_PARSER_API int turbo_xml_sax_parser_finish(turbo_xml_sax_parser_t *parser);
TURBO_PARSER_API const char *turbo_xml_sax_parser_error(const turbo_xml_sax_parser_t *parser);
TURBO_PARSER_API void turbo_xml_sax_parser_destroy(turbo_xml_sax_parser_t *parser);

/**
 * @brief Free XML data and set pointer to NULL.
 * @param out Address of the pointer
 * (turbo_xml_doc_t **) to free.
 */
TURBO_PARSER_API void turbo_free_xml(turbo_xml_doc_t **out);
TURBO_PARSER_API char *turbo_xml_serialize(const turbo_xml_doc_t *doc, size_t *out_len);
TURBO_PARSER_API void turbo_xml_string_free(char *str);
TURBO_PARSER_API void turbo_xml_serialize_free(char *str);
TURBO_PARSER_API int turbo_xml_write(const turbo_xml_doc_t *doc, turbo_write_fn write, void *user);
TURBO_PARSER_API turbo_xml_doc_t *turbo_xml_create_document(const char *root_name);
TURBO_PARSER_API turbo_xml_node_t *turbo_xml_add_element(void *parent, const char *name);
TURBO_PARSER_API int turbo_xml_set_text(turbo_xml_node_t *node, const char *text);

/**
 * @brief Get the root element of an XML document.
 * @param doc Pointer to the XML document.
 * @return Pointer to the root element.
 */
TURBO_PARSER_API turbo_xml_node_t *turbo_xml_root_element(const turbo_xml_doc_t *doc);

/**
 * @brief Get the name of an XML node.
 * @param node Pointer to the XML node.
 * @return Pointer to the name string.
 */
TURBO_PARSER_API const char *turbo_xml_node_name(const turbo_xml_node_t *node);

/**
 * @brief Initialize an XML node list.
 * @param list Target list.
 */
TURBO_PARSER_API void turbo_xml_list_init(turbo_xml_list_t *list);

/**
 * @brief Free an XML node list.
 * @param list Target list.
 */
TURBO_PARSER_API void turbo_xml_list_free(turbo_xml_list_t *list);

/**
 * @brief Find the first XML node matching a query.
 * @param root Search root.
 * @param query
 * Query string.
 * @return Matching node or NULL.
 */
TURBO_PARSER_API turbo_xml_node_t *turbo_xml_find(turbo_xml_node_t *root, const char *query);

/**
 * @brief Find all XML nodes matching a query.
 * @param root Search root.
 * @param query Query
 * string.
 * @param out Target list.
 */
TURBO_PARSER_API void turbo_xml_find_all(turbo_xml_node_t *root, const char *query, turbo_xml_list_t *out);

/**
 * @brief Duplicate the text content of an XML node.
 * @param node Target node.
 * @return
 * Newly allocated string or NULL.
 */
TURBO_PARSER_API char *turbo_xml_text_dup(turbo_xml_node_t *node);

/**
 * @brief Duplicate the text content of a named child element.
 * @param parent Parent node.
 *
 * @param name Child local name.
 * @return Newly allocated string. Returns an empty string if not
 * found.
 */
TURBO_PARSER_API char *turbo_xml_child_text_dup(turbo_xml_node_t *parent, const char *name);

/**
 * @brief Get text content of XML nodes matching XPath expression.
 * @param doc Pointer to the XML document.
 * @param xpath XPath expression.
 * @return Pointer to text content of first matching node, or NULL if not found.
 */
TURBO_PARSER_API const char *turbo_xml_get_text(const turbo_xml_doc_t *doc, const char *xpath);

/**
 * @brief Count XML nodes matching XPath expression.
 * @param doc Pointer to the XML document.
 * @param xpath XPath expression.
 * @return Number of matching nodes.
 */
TURBO_PARSER_API size_t turbo_xml_count(const turbo_xml_doc_t *doc, const char *xpath);

/**
 * @brief Get the first XML node matching an XPath expression.
 * @param doc Pointer to the XML
 * document.
 * @param xpath XPath expression.
 * @return First matching opaque XML node pointer, or
 * NULL.
 */
TURBO_PARSER_API turbo_xml_xpath_node_t *turbo_xml_xpath_get(const turbo_xml_doc_t *doc,
                                                      const char *xpath);

/**
 * @brief Query XML nodes matching an XPath expression.
 * @param doc Pointer to the XML
 * document.
 * @param xpath XPath expression.
 * @param out Target list. Contains non-owning opaque
 * XML node pointers.
 */
TURBO_PARSER_API void turbo_xml_xpath_query(const turbo_xml_doc_t *doc, const char *xpath,
                                      turbo_xml_list_t *out);
/** Execute XPath with copied per-call limits and append borrowed nodes to out.
 * out is initialized even on failure and must be released with
 * turbo_xml_list_free(). */
TURBO_PARSER_API turbo_query_status_t turbo_xml_xpath_query_ex(
    const turbo_xml_doc_t *doc, const char *xpath, turbo_xml_list_t *out,
    const turbo_query_limits_t *limits, turbo_query_diagnostic_t *diagnostic);

/**
 * @brief Count XML nodes matching an XPath expression.
 * @param doc Pointer to the XML
 * document.
 * @param xpath XPath expression.
 * @return Number of matching nodes.
 */
TURBO_PARSER_API size_t turbo_xml_xpath_count(const turbo_xml_doc_t *doc, const char *xpath);

/**
 * @brief Get text content of the first XML node matching an XPath expression.
 * @param doc
 * Pointer to the XML document.
 * @param xpath XPath expression.
 * @return Pointer to text content
 * of first matching node, or NULL if not found.
 */
TURBO_PARSER_API const char *turbo_xml_xpath_text(const turbo_xml_doc_t *doc, const char *xpath);

/**
 * @brief Get the type of an opaque XPath node.
 * @param node Node returned from
 * turbo_xml_xpath_get/query.
 * @return Stable TurboNet XML node type.
 */
TURBO_PARSER_API turbo_xml_node_type_t turbo_xml_xpath_node_type(const turbo_xml_xpath_node_t *node);

/**
 * @brief Get the stable string name for an opaque XPath node type.
 * @param node Node returned
 * from turbo_xml_xpath_get/query.
 * @return Type name such as "element", "text", or "attribute".

 */
TURBO_PARSER_API const char *turbo_xml_xpath_node_type_name(const turbo_xml_xpath_node_t *node);

/**
 * @brief Get the qualified name for an opaque XPath node when it has one.
 * @param node Node
 * returned from turbo_xml_xpath_get/query.
 * @return Node name, or NULL for unnamed node kinds.

 */
TURBO_PARSER_API const char *turbo_xml_xpath_node_name(const turbo_xml_xpath_node_t *node);

/**
 * @brief Get textual value for an opaque XPath node when it has one.
 * @param node Node
 * returned from turbo_xml_xpath_get/query.
 * @return Text value, or NULL when unavailable. The
 * pointer is non-owning.
 */
TURBO_PARSER_API const char *turbo_xml_xpath_node_text(const turbo_xml_xpath_node_t *node);

/**
 * @brief Serialize an opaque XPath node to XML/text.
 * @param node Node returned from
 * turbo_xml_xpath_get/query.
 * @return Newly allocated string, or NULL. Free with
 * turbo_xml_string_free().
 */
TURBO_PARSER_API char *turbo_xml_xpath_node_xml_dup(const turbo_xml_xpath_node_t *node);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_XML_H

