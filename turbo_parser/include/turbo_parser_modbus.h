#ifndef TURBO_PARSER_MODBUS_H
#define TURBO_PARSER_MODBUS_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Modbus Parser */
#define TURBO_MODBUS_TCP_MBAP_SIZE 7
#define TURBO_MODBUS_TCP_MIN_ADU_SIZE 8
#define TURBO_MODBUS_TCP_MAX_ADU_SIZE 260
#define TURBO_MODBUS_MAX_PDU_SIZE 253
#define TURBO_MODBUS_RTU_MIN_ADU_SIZE 4
#define TURBO_MODBUS_RTU_MAX_ADU_SIZE 256

typedef enum {
  TURBO_MODBUS_PARSE_OK = 0,
  TURBO_MODBUS_PARSE_NEED_MORE,
  TURBO_MODBUS_PARSE_INVALID_INPUT,
  TURBO_MODBUS_PARSE_INVALID_PROTOCOL,
  TURBO_MODBUS_PARSE_INVALID_LENGTH,
  TURBO_MODBUS_PARSE_CRC_MISMATCH,
  TURBO_MODBUS_PARSE_BUFFER_OVERFLOW,
} turbo_modbus_parse_result_t;

typedef enum {
  TURBO_MODBUS_TRANSPORT_TCP = 0,
  TURBO_MODBUS_TRANSPORT_RTU = 1,
} turbo_modbus_transport_t;

typedef struct {
  uint8_t function_code;
  const uint8_t *data;
  size_t data_size;
} turbo_modbus_pdu_t;

typedef struct {
  uint16_t transaction_id;
  uint16_t protocol_id;
  uint16_t length;
  uint8_t unit_id;
  turbo_modbus_pdu_t pdu;
  size_t consumed;
} turbo_modbus_tcp_adu_t;

typedef struct {
  uint8_t address;
  turbo_modbus_pdu_t pdu;
  uint16_t crc;
  size_t consumed;
} turbo_modbus_rtu_adu_t;

typedef struct {
  turbo_modbus_transport_t transport;
  union {
    turbo_modbus_tcp_adu_t tcp;
    turbo_modbus_rtu_adu_t rtu;
  } frame;
} turbo_modbus_adu_t;

CXX_C_API int turbo_modbus_tcp_peek_size(const uint8_t *data, size_t len, size_t *out_size);
CXX_C_API int turbo_modbus_tcp_read(const uint8_t *data, size_t len, turbo_modbus_tcp_adu_t *out);
CXX_C_API size_t turbo_modbus_tcp_write(const turbo_modbus_tcp_adu_t *adu, uint8_t *out,
                                        size_t out_len);

CXX_C_API uint16_t turbo_modbus_rtu_crc16(const uint8_t *data, size_t len);
CXX_C_API int turbo_modbus_rtu_read(const uint8_t *data, size_t len, turbo_modbus_rtu_adu_t *out);
CXX_C_API size_t turbo_modbus_rtu_write(const turbo_modbus_rtu_adu_t *adu, uint8_t *out,
                                        size_t out_len);

CXX_C_API int turbo_modbus_read(turbo_modbus_transport_t transport, const uint8_t *data, size_t len,
                                turbo_modbus_adu_t *out);
CXX_C_API size_t turbo_modbus_write(const turbo_modbus_adu_t *adu, uint8_t *out, size_t out_len);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_MODBUS_H

