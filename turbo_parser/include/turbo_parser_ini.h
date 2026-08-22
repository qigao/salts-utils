#ifndef TURBO_PARSER_INI_H
#define TURBO_PARSER_INI_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* INI */
typedef struct ini_s turbo_ini_t;

/**
 * @brief Parse INI data.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (turbo_ini_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
TURBO_PARSER_API int turbo_parse_ini(const uint8_t *data, size_t len, void *out);

/**
 * @brief Free INI data and set pointer to NULL.
 * @param out Address of the pointer (turbo_ini_t **) to free.
 */
TURBO_PARSER_API void turbo_free_ini(void *out);

/**
 * @brief Get string value from INI.
 * @param ini Pointer to INI document.
 * @param section Section name.
 * @param key Key name.
 * @return Value string if found, NULL otherwise.
 */
TURBO_PARSER_API const char *turbo_ini_get(const turbo_ini_t *ini, const char *section, const char *key);

/**
 * @brief Get integer value from INI.
 * @param ini Pointer to INI document.
 * @param section Section name.
 * @param key Key name.
 * @param def Default value.
 * @return Integer value.
 */
TURBO_PARSER_API int turbo_ini_get_int(const turbo_ini_t *ini, const char *section, const char *key,
                                int def);

/**
 * @brief Get boolean value from INI.
 * @param ini Pointer to INI document.
 * @param section Section name.
 * @param key Key name.
 * @param def Default value.
 * @return Boolean value.
 */
TURBO_PARSER_API bool turbo_ini_get_bool(const turbo_ini_t *ini, const char *section, const char *key,
                                  bool def);

/**
 * @brief Get double value from INI.
 * @param ini Pointer to INI document.
 * @param section Section name.
 * @param key Key name.
 * @param def Default value.
 * @return Double value.
 */
TURBO_PARSER_API double turbo_ini_get_double(const turbo_ini_t *ini, const char *section, const char *key,
                                      double def);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_INI_H

