#ifndef TURBO_PARSER_TLV_H
#define TURBO_PARSER_TLV_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TLV */
typedef struct frame_s turbo_tlv_frame_t;

/**
 * @brief Parse TLV (Type-Length-Value) frame.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (turbo_tlv_frame_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
TURBO_PARSER_API int turbo_parse_tlv(const uint8_t *data, size_t len, void *out);

/**
 * @brief Free TLV data and set pointer to NULL.
 * @param out Address of the pointer (turbo_tlv_frame_t **) to free.
 */
TURBO_PARSER_API void turbo_free_tlv(void *out);

/**
 * @brief Get the message ID from a TLV frame.
 * @param frame Pointer to the TLV frame.
 * @return The message ID.
 */
TURBO_PARSER_API uint32_t turbo_tlv_msg_id(const turbo_tlv_frame_t *frame);

/**
 * @brief Get the protocol version from a TLV frame.
 * @param frame Pointer to the TLV frame.
 * @return Protocol version.
 */
TURBO_PARSER_API uint8_t turbo_tlv_version(const turbo_tlv_frame_t *frame);

/**
 * @brief Get the payload type from a TLV frame.
 * @param frame Pointer to the TLV frame.
 * @return Payload type.
 */
TURBO_PARSER_API uint8_t turbo_tlv_type(const turbo_tlv_frame_t *frame);

/**
 * @brief Get the payload size from a TLV frame.
 * @param frame Pointer to the TLV frame.
 * @return Payload size in bytes.
 */
TURBO_PARSER_API size_t turbo_tlv_payload_size(const turbo_tlv_frame_t *frame);

/**
 * @brief Get a pointer to the payload data in a TLV frame.
 * @param frame Pointer to the TLV frame.
 * @return Pointer to the payload data.
 */
TURBO_PARSER_API const char *turbo_tlv_payload(const turbo_tlv_frame_t *frame);

/**
 * @brief Get the CRC32 check value from a TLV frame.
 * @param frame Pointer to the TLV frame.
 * @return CRC32 value.
 */
TURBO_PARSER_API uint32_t turbo_tlv_crc32(const turbo_tlv_frame_t *frame);

/**
 * @brief Peek into a buffer to determine the total size of a TLV frame.
 * @param data Input buffer.
 * @param len Available buffer length.
 * @param out_size Pointer to store the detected total frame size.
 * @return 0 on success, error code if data is insufficient or invalid.
 */
TURBO_PARSER_API int turbo_tlv_peek_size(const uint8_t *data, size_t len, uint32_t *out_size);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_TLV_H

