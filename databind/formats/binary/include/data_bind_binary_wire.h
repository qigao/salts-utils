#ifndef DATA_BIND_BINARY_WIRE_H
#define DATA_BIND_BINARY_WIRE_H

#include "data_bind_binary_endian.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(DATA_BIND_BINARY_WASM_GUEST)
#define DATA_BIND_BINARY_WIRE_MEMCPY(destination, source, size) \
    __builtin_memcpy((destination), (source), (size))
#else
#include <string.h>
#define DATA_BIND_BINARY_WIRE_MEMCPY(destination, source, size) \
    memcpy((destination), (source), (size))
#endif

typedef struct DataBindBinaryVarData {
    const uint8_t *data;
    size_t size;
} DataBindBinaryVarData;

static inline uint8_t data_bind_binary_wire_read_u8(const uint8_t *data, int is_big_endian) {
    uint8_t value = 0;
    if (!data) return 0;  // Add null check
    (void)is_big_endian;
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, data, sizeof(value));
    return value;
}

static inline int8_t data_bind_binary_wire_read_i8(const uint8_t *data, int is_big_endian) {
    int8_t value = 0;
    if (!data) return 0;  // Add null check
    (void)is_big_endian;
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, data, sizeof(value));
    return value;
}

static inline uint16_t data_bind_binary_wire_read_u16(const uint8_t *data, int is_big_endian) {
    uint16_t value = 0;
    if (!data) return 0;  // Add null check
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, data, sizeof(value));
    return is_big_endian ? data_bind_binary_be16toh(value) : data_bind_binary_le16toh(value);
}

static inline int16_t data_bind_binary_wire_read_i16(const uint8_t *data, int is_big_endian) {
    if (!data) return 0;  // Add null check
    uint16_t raw = data_bind_binary_wire_read_u16(data, is_big_endian);
    int16_t value = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, &raw, sizeof(value));
    return value;
}

static inline uint32_t data_bind_binary_wire_read_u32(const uint8_t *data, int is_big_endian) {
    uint32_t value = 0;
    if (!data) return 0;  // Add null check
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, data, sizeof(value));
    return is_big_endian ? data_bind_binary_be32toh(value) : data_bind_binary_le32toh(value);
}

static inline int32_t data_bind_binary_wire_read_i32(const uint8_t *data, int is_big_endian) {
    if (!data) return 0;  // Add null check
    uint32_t raw = data_bind_binary_wire_read_u32(data, is_big_endian);
    int32_t value = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, &raw, sizeof(value));
    return value;
}

static inline uint64_t data_bind_binary_wire_read_u64(const uint8_t *data, int is_big_endian) {
    uint64_t value = 0;
    if (!data) return 0;  // Add null check
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, data, sizeof(value));
    return is_big_endian ? data_bind_binary_be64toh(value) : data_bind_binary_le64toh(value);
}

static inline int64_t data_bind_binary_wire_read_i64(const uint8_t *data, int is_big_endian) {
    if (!data) return 0;  // Add null check
    uint64_t raw = data_bind_binary_wire_read_u64(data, is_big_endian);
    int64_t value = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, &raw, sizeof(value));
    return value;
}

static inline float data_bind_binary_wire_read_f32(const uint8_t *data, int is_big_endian) {
    if (!data) return 0.0f;  // Add null check
    uint32_t raw = data_bind_binary_wire_read_u32(data, is_big_endian);
    float value = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, &raw, sizeof(value));
    return value;
}

static inline double data_bind_binary_wire_read_f64(const uint8_t *data, int is_big_endian) {
    if (!data) return 0.0;  // Add null check
    uint64_t raw = data_bind_binary_wire_read_u64(data, is_big_endian);
    double value = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&value, &raw, sizeof(value));
    return value;
}

static inline void data_bind_binary_wire_write_u8(uint8_t *data, int is_big_endian, uint8_t value) {
    if (!data) return;  // Add null check
    (void)is_big_endian;
    DATA_BIND_BINARY_WIRE_MEMCPY(data, &value, sizeof(value));
}

static inline void data_bind_binary_wire_write_i8(uint8_t *data, int is_big_endian, int8_t value) {
    if (!data) return;  // Add null check
    (void)is_big_endian;
    DATA_BIND_BINARY_WIRE_MEMCPY(data, &value, sizeof(value));
}

static inline void data_bind_binary_wire_write_u16(uint8_t *data, int is_big_endian, uint16_t value) {
    if (!data) return;  // Add null check
    uint16_t raw = is_big_endian ? data_bind_binary_htobe16(value) : data_bind_binary_htole16(value);
    DATA_BIND_BINARY_WIRE_MEMCPY(data, &raw, sizeof(raw));
}

static inline void data_bind_binary_wire_write_i16(uint8_t *data, int is_big_endian, int16_t value) {
    if (!data) return;  // Add null check
    uint16_t raw = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&raw, &value, sizeof(raw));
    data_bind_binary_wire_write_u16(data, is_big_endian, raw);
}

static inline void data_bind_binary_wire_write_u32(uint8_t *data, int is_big_endian, uint32_t value) {
    if (!data) return;  // Add null check
    uint32_t raw = is_big_endian ? data_bind_binary_htobe32(value) : data_bind_binary_htole32(value);
    DATA_BIND_BINARY_WIRE_MEMCPY(data, &raw, sizeof(raw));
}

static inline void data_bind_binary_wire_write_i32(uint8_t *data, int is_big_endian, int32_t value) {
    if (!data) return;  // Add null check
    uint32_t raw = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&raw, &value, sizeof(raw));
    data_bind_binary_wire_write_u32(data, is_big_endian, raw);
}

static inline void data_bind_binary_wire_write_u64(uint8_t *data, int is_big_endian, uint64_t value) {
    if (!data) return;  // Add null check
    uint64_t raw = is_big_endian ? data_bind_binary_htobe64(value) : data_bind_binary_htole64(value);
    DATA_BIND_BINARY_WIRE_MEMCPY(data, &raw, sizeof(raw));
}

static inline void data_bind_binary_wire_write_i64(uint8_t *data, int is_big_endian, int64_t value) {
    if (!data) return;  // Add null check
    uint64_t raw = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&raw, &value, sizeof(raw));
    data_bind_binary_wire_write_u64(data, is_big_endian, raw);
}

static inline void data_bind_binary_wire_write_f32(uint8_t *data, int is_big_endian, float value) {
    if (!data) return;  // Add null check
    uint32_t raw = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&raw, &value, sizeof(raw));
    data_bind_binary_wire_write_u32(data, is_big_endian, raw);
}

static inline void data_bind_binary_wire_write_f64(uint8_t *data, int is_big_endian, double value) {
    if (!data) return;  // Add null check
    uint64_t raw = 0;
    DATA_BIND_BINARY_WIRE_MEMCPY(&raw, &value, sizeof(raw));
    data_bind_binary_wire_write_u64(data, is_big_endian, raw);
}

static inline bool data_bind_binary_wire_write_var_data(uint8_t *data,
                                           size_t size,
                                           int is_big_endian,
                                           const void *value_data,
                                           size_t value_size) {
    if (!data || size < sizeof(uint32_t) || value_size > (size_t)UINT32_MAX) {
        return false;
    }

    if (value_size > 0 && !value_data) {
        return false;
    }

    if (value_size > size - sizeof(uint32_t)) {
        return false;
    }

    data_bind_binary_wire_write_u32(data, is_big_endian, (uint32_t)value_size);
    if (value_size > 0) {
        DATA_BIND_BINARY_WIRE_MEMCPY(data + sizeof(uint32_t), value_data, value_size);
    }

    return true;
}

static inline bool data_bind_binary_wire_read_var_data(const uint8_t *data,
                                          size_t size,
                                          int is_big_endian,
                                          DataBindBinaryVarData *out) {
    uint32_t length = 0;

    /* Current DSL uses a uint32 length prefix for variable data fields. */

    if (!data || !out || size < sizeof(uint32_t)) {
        if (out) {
            out->data = NULL;
            out->size = 0;
        }
        return false;
    }

    length = data_bind_binary_wire_read_u32(data, is_big_endian);
    
    /* The returned payload is a borrowed view, so only the wire bounds apply. */
    if ((size_t)length > size - sizeof(uint32_t)) {
        out->data = NULL;
        out->size = 0;
        return false;
    }

    out->data = data + sizeof(uint32_t);
    out->size = (size_t)length;
    return true;
}

static inline const uint8_t *data_bind_binary_wire_var_data_end(const DataBindBinaryVarData *value) {
    if (!value || !value->data) {
        return NULL;
    }

    return value->data + value->size;
}

#undef DATA_BIND_BINARY_WIRE_MEMCPY

#endif /* DATA_BIND_BINARY_WIRE_H */
