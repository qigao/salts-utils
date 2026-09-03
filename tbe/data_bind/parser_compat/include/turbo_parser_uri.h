#ifndef TURBO_PARSER_URI_H
#define TURBO_PARSER_URI_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* URI Parser */
typedef struct uri_s uri_t;

typedef enum {
  TURBO_URI_HOST_UNKNOWN = 0,
  TURBO_URI_HOST_REGNAME,
  TURBO_URI_HOST_IPV6ADDR,
  TURBO_URI_HOST_IPV4ADDR,
  TURBO_URI_HOST_IPVFUTURE
} turbo_uri_host_type_t;

/**
 * @brief Parse a URI string.
 * @param data Input string data.
 * @param len String length.
 * @param out Address of a pointer (uri_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
TURBO_PARSER_API int turbo_parse_uri(const uint8_t *data, size_t len, void *out);

/**
 * @brief Free URI data and set pointer to NULL.
 * @param out Address of the pointer (uri_t **) to free.
 */
TURBO_PARSER_API void turbo_free_uri(void *out);

/**
 * @brief Get the scheme part of a URI (e.g., "http" or "ftp").
 * @param uri Pointer to the URI structure.
 * @return Pointer to the scheme string.
 */
TURBO_PARSER_API const char *turbo_uri_scheme(const uri_t *uri);

/**
 * @brief Get the user information part of a URI.
 * @param uri Pointer to the URI structure.
 * @return Pointer to the userinfo string.
 */
TURBO_PARSER_API const char *turbo_uri_userinfo(const uri_t *uri);

/**
 * @brief Get the host part of a URI.
 * @param uri Pointer to the URI structure.
 * @return Pointer to the host string.
 */
TURBO_PARSER_API const char *turbo_uri_host(const uri_t *uri);

/**
 * @brief Get the port number of a URI.
 * @param uri Pointer to the URI structure.
 * @return Port number, or 0 if not specified.
 */
TURBO_PARSER_API int turbo_uri_port(const uri_t *uri);

/**
 * @brief Get the path part of a URI.
 * @param uri Pointer to the URI structure.
 * @return Pointer to the path string.
 */
TURBO_PARSER_API const char *turbo_uri_path(const uri_t *uri);

/**
 * @brief Get the query string part of a URI.
 * @param uri Pointer to the URI structure.
 * @return Pointer to the query string.
 */
TURBO_PARSER_API const char *turbo_uri_query(const uri_t *uri);

/**
 * @brief Get the fragment (anchor) part of a URI.
 * @param uri Pointer to the URI structure.
 * @return Pointer to the fragment string.
 */
TURBO_PARSER_API const char *turbo_uri_fragment(const uri_t *uri);

/**
 * @brief Get the type of the host in the URI (e.g., IPv4, IPv6, or name).
 * @param uri Pointer to the URI structure.
 * @return The host type code.
 */
TURBO_PARSER_API turbo_uri_host_type_t turbo_uri_host_type(const uri_t *uri);

/**
 * @brief Verify if the parsed URI is semantically valid.
 * @param uri Pointer to the URI structure.
 * @return true if valid, false otherwise.
 */
TURBO_PARSER_API bool turbo_uri_is_valid(const uri_t *uri);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_URI_H
