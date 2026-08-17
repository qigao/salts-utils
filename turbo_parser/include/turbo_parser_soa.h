#ifndef TURBO_PARSER_SOA_H
#define TURBO_PARSER_SOA_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SOA Parser */
typedef struct soa_batch_s turbo_soa_batch_t;
typedef struct soa_schema_s turbo_soa_schema_t;

/**
 * @brief Parse SOA (Struct-of-Arrays) batch data.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (turbo_soa_batch_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
CXX_C_API int turbo_parse_soa(const uint8_t *data, size_t len, void *out);

/**
 * @brief Free SOA data and set pointer to NULL.
 * @param out Address of the pointer (turbo_soa_batch_t **) to free.
 */
CXX_C_API void turbo_free_soa(void *out);

/**
 * @brief Get the number of rows (entries) in an SOA batch.
 * @param batch Pointer to the SOA batch.
 * @return Row count.
 */
CXX_C_API uint32_t turbo_soa_count(const turbo_soa_batch_t *batch);

/**
 * @brief Get the schema ID associated with an SOA batch.
 * @param batch Pointer to the SOA batch.
 * @return Schema ID.
 */
CXX_C_API uint16_t turbo_soa_schema_id(const turbo_soa_batch_t *batch);

/**
 * @brief Get the field presence mask for an SOA batch.
 * @param batch Pointer to the SOA batch.
 * @return 16-bit presence mask.
 */
CXX_C_API uint16_t turbo_soa_present_mask(const turbo_soa_batch_t *batch);

/**
 * @brief Get an 8-bit integer value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API int8_t turbo_soa_get_i8(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Get an unsigned 8-bit integer value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API uint8_t turbo_soa_get_u8(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Get a 16-bit integer value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API int16_t turbo_soa_get_i16(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Get an unsigned 16-bit integer value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API uint16_t turbo_soa_get_u16(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Get a 32-bit integer value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API int32_t turbo_soa_get_i32(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Get an unsigned 32-bit integer value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API uint32_t turbo_soa_get_u32(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Get a 64-bit integer value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API int64_t turbo_soa_get_i64(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Get an unsigned 64-bit integer value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API uint64_t turbo_soa_get_u64(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Get a double value from a specific column and row in an SOA batch.
 * @param b Pointer to the SOA batch.
 * @param col Column index.
 * @param row Row index.
 * @return The value at the specified position.
 */
CXX_C_API double turbo_soa_get_f64(const turbo_soa_batch_t *b, int col, uint32_t row);

/**
 * @brief Calculate the wire size required for an SOA batch with given schema, count, and mask.
 * @param schema Batch schema.
 * @param count Number of rows.
 * @param present_mask Field presence mask.
 * @return Required size in bytes.
 */
CXX_C_API size_t turbo_soa_wire_size(const turbo_soa_schema_t *schema, uint32_t count,
                                     uint16_t present_mask);

/**
 * @brief Build the header for an SOA batch into a buffer.
 * @param schema Batch schema.
 * @param count Number of rows.
 * @param present_mask Field presence mask.
 * @param out Output buffer.
 * @param out_len Maximum output buffer size.
 * @return Number of bytes written to the buffer.
 */
CXX_C_API size_t turbo_soa_build_header(const turbo_soa_schema_t *schema, uint32_t count,
                                        uint16_t present_mask, uint8_t *out, size_t out_len);

/**
 * @brief Get the hardware width for a given SOA data type.
 * @param type Data type code.
 * @return Width in bytes.
 */
CXX_C_API uint8_t turbo_soa_type_width(int type);

/**
 * @brief Peek into a buffer to determine the count and schema of an SOA batch.
 * @param data Input buffer.
 * @param len Available buffer length.
 * @param out_count Pointer to store the row count.
 * @param out_schema Pointer to store the schema ID.
 * @return 0 on success, error code otherwise.
 */
CXX_C_API int turbo_soa_peek_header(const uint8_t *data, size_t len, uint32_t *out_count,
                                    uint16_t *out_schema);

/**
 * @brief Get the number of columns defined by a schema.
 * @param schema Pointer to schema.
 * @return Column count.
 */
CXX_C_API int turbo_soa_schema_count(const turbo_soa_schema_t *schema);

/**
 * @brief Get the data type of a column in a schema.
 * @param schema Pointer to schema.
 * @param idx Column index.
 * @return Data type code.
 */
CXX_C_API int turbo_soa_schema_column_type(const turbo_soa_schema_t *schema, int idx);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_SOA_H

