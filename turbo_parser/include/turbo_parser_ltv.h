#ifndef TURBO_PARSER_LTV_H
#define TURBO_PARSER_LTV_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LTV Parser */
typedef struct ltv_message_s turbo_ltv_message_t;

/**
 * @brief Parse LTV (Length-Type-Value) message.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (turbo_ltv_message_t **) to store the result.
 * The parsed value borrows data and remains valid only while data is alive and
 * unchanged. turbo_free_ltv() frees the message handle, not data.
 * @return 0 on success, error code otherwise.
 */
CXX_C_API int turbo_parse_ltv(const uint8_t *data, size_t len, void *out);

/**
 * @brief Free LTV data and set pointer to NULL.
 * @param out Address of the pointer (turbo_ltv_message_t **) to free.
 */
CXX_C_API void turbo_free_ltv(void *out);

/**
 * @brief Get the type of an LTV message.
 * @param msg Pointer to the LTV message.
 * @return The message type.
 */
CXX_C_API uint8_t turbo_ltv_type(const turbo_ltv_message_t *msg);

/**
 * @brief Get a pointer to the value part of an LTV message.
 * @param msg Pointer to the LTV message.
 * @return Pointer to the value data.
 */
CXX_C_API const uint8_t *turbo_ltv_value(const turbo_ltv_message_t *msg);

/**
 * @brief Get the length of the value part of an LTV message.
 * @param msg Pointer to the LTV message.
 * @return The value length in bytes.
 */
CXX_C_API size_t turbo_ltv_value_len(const turbo_ltv_message_t *msg);

/**
 * @brief Calculate the total wire size required for an LTV message with a given value size.
 * @param value_size Number of bytes in the value part.
 * @return Total size in bytes including header, or 0 when value_size exceeds
 * the supported LTV payload limit.
 */
CXX_C_API size_t turbo_ltv_wire_size(size_t value_size);

/**
 * @brief Serialize an LTV message into a buffer.
 * @param type Message type.
 * @param value Pointer to value data.
 * @param value_size Length of value data.
 * @param out Output buffer.
 * @param out_len Maximum output buffer size.
 * @return Number of bytes written, or 0 on invalid input, oversized value, or
 * insufficient capacity. No partial message is written on failure.
 */
CXX_C_API size_t turbo_ltv_build(uint8_t type, const uint8_t *value, size_t value_size,
                                 uint8_t *out, size_t out_len);

/**
 * @brief Peek into a buffer to determine the total size of an LTV message.
 * @param data Input buffer.
 * @param len Available buffer length.
 * @param out_length Pointer to store the detected total message size.
 * @param out_header Pointer to store the detected header size.
 * @return 0 on success, error code otherwise.
 */
CXX_C_API int turbo_ltv_peek_size(const uint8_t *data, size_t len, uint32_t *out_length,
                                  size_t *out_header);

/* LTV Streaming */
typedef struct ltv_stream_s turbo_ltv_stream_t;

/**
 * @brief Create an LTV streaming parser.
 * @param buffer_size Size of the internal reassembly buffer.
 * @return Pointer to the new LTV stream parser.
 */
CXX_C_API turbo_ltv_stream_t *turbo_ltv_stream_create(size_t buffer_size);

/**
 * @brief Destroy an LTV streaming parser and free its resources.
 * @param stream Pointer to the stream parser to destroy.
 */
CXX_C_API void turbo_ltv_stream_destroy(turbo_ltv_stream_t *stream);

/**
 * @brief Feed incoming data to the LTV streaming parser.
 * @param stream Pointer to the stream parser.
 * @param data New data to process.
 * @param len Length of new data.
 * @param out Pointer to store a pointer to the reassembled message when complete.
 * The returned value borrows stream storage and remains valid until the next
 * feed, reset, or destroy call on the stream.
 * @return 0 if a message was completed and stored in 'out', negative for error, positive if more
 * data is needed.
 */
CXX_C_API int turbo_ltv_stream_feed(turbo_ltv_stream_t *stream, const uint8_t *data, size_t len,
                                    void **out);

/**
 * @brief Reset the internal state of the LTV streaming parser.
 * @param stream Pointer to the stream parser.
 */
CXX_C_API void turbo_ltv_stream_reset(turbo_ltv_stream_t *stream);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_LTV_H

