#ifndef TURBO_PARSER_TOML_H
#define TURBO_PARSER_TOML_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TOML Parser */
typedef struct toml_table_t turbo_toml_t;
typedef struct toml_array_t turbo_toml_array_t;

typedef struct {
  char kind;
  int year, month, day;
  int hour, minute, second, millisec;
  int tz;
} turbo_toml_timestamp_t;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4201)
#endif
typedef struct {
  bool ok;
  union {
    struct {
      char *s;
      int sl;
    };
    turbo_toml_timestamp_t ts;
    bool b;
    int64_t i;
    double d;
  } u;
} turbo_toml_value_t;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

/**
 * @brief Parse TOML formatted data.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (turbo_toml_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
CXX_C_API int turbo_parse_toml(const uint8_t *data, size_t len, void *out);

/**
 * @brief Free TOML data and set pointer to NULL.
 * @param out Address of the pointer (turbo_toml_t **) to free.
 */
CXX_C_API void turbo_free_toml(void *out);

/**
 * @brief Get the number of entries in a TOML table.
 * @param table Pointer to TOML table.
 * @return Number of entries.
 */
CXX_C_API int turbo_toml_len(const turbo_toml_t *table);

/**
 * @brief Get the key name at a specific index in a TOML table.
 * @param table Pointer to TOML table.
 * @param index Entry index.
 * @param keylen Optional pointer to store key string length.
 * @return Key name string.
 */
CXX_C_API const char *turbo_toml_key(const turbo_toml_t *table, int index, int *keylen);

/**
 * @brief Get a string value from a TOML table by key.
 * @param table Pointer to TOML table.
 * @param key Entry key.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_string(const turbo_toml_t *table, const char *key);

/**
 * @brief Get a boolean value from a TOML table by key.
 * @param table Pointer to TOML table.
 * @param key Entry key.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_bool(const turbo_toml_t *table, const char *key);

/**
 * @brief Get an integer value from a TOML table by key.
 * @param table Pointer to TOML table.
 * @param key Entry key.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_int(const turbo_toml_t *table, const char *key);

/**
 * @brief Get a floating-point value from a TOML table by key.
 * @param table Pointer to TOML table.
 * @param key Entry key.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_double(const turbo_toml_t *table, const char *key);

/**
 * @brief Get a timestamp value from a TOML table by key.
 * @param table Pointer to TOML table.
 * @param key Entry key.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_timestamp(const turbo_toml_t *table, const char *key);

/**
 * @brief Get a sub-array from a TOML table by key.
 * @param table Pointer to TOML table.
 * @param key Entry key.
 * @return Pointer to TOML array if found, NULL otherwise.
 */
CXX_C_API turbo_toml_array_t *turbo_toml_array(const turbo_toml_t *table, const char *key);

/**
 * @brief Get a nested table from a TOML table by key.
 * @param table Pointer to TOML table.
 * @param key Entry key.
 * @return Pointer to TOML table if found, NULL otherwise.
 */
CXX_C_API turbo_toml_t *turbo_toml_table(const turbo_toml_t *table, const char *key);

/**
 * @brief Get the number of elements in a TOML array.
 * @param array Pointer to TOML array.
 * @return Element count.
 */
CXX_C_API int turbo_toml_array_len(const turbo_toml_array_t *array);

/**
 * @brief Get a string value from a TOML array by index.
 * @param array Pointer to TOML array.
 * @param idx Element index.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_array_string(const turbo_toml_array_t *array, int idx);

/**
 * @brief Get a boolean value from a TOML array by index.
 * @param array Pointer to TOML array.
 * @param idx Element index.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_array_bool(const turbo_toml_array_t *array, int idx);

/**
 * @brief Get an integer value from a TOML array by index.
 * @param array Pointer to TOML array.
 * @param idx Element index.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_array_int(const turbo_toml_array_t *array, int idx);

/**
 * @brief Get a floating-point value from a TOML array by index.
 * @param array Pointer to TOML array.
 * @param idx Element index.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_array_double(const turbo_toml_array_t *array, int idx);

/**
 * @brief Get a timestamp value from a TOML array by index.
 * @param array Pointer to TOML array.
 * @param idx Element index.
 * @return Result value structure.
 */
CXX_C_API turbo_toml_value_t turbo_toml_array_timestamp(const turbo_toml_array_t *array, int idx);

/**
 * @brief Get a nested array from a TOML array by index.
 * @param array Pointer to TOML array.
 * @param idx Element index.
 * @return Pointer to TOML array if found, NULL otherwise.
 */
CXX_C_API turbo_toml_array_t *turbo_toml_array_array(const turbo_toml_array_t *array, int idx);

/**
 * @brief Get a nested table from a TOML array by index.
 * @param array Pointer to TOML array.
 * @param idx Element index.
 * @return Pointer to TOML table if found, NULL otherwise.
 */
CXX_C_API turbo_toml_t *turbo_toml_array_table(const turbo_toml_array_t *array, int idx);

#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_TOML_H

