#ifndef DATA_BIND_BINARY_ENDIAN_H
#define DATA_BIND_BINARY_ENDIAN_H

#include <stdint.h>

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  #define DATA_BIND_BINARY_IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#elif defined(_WIN32) || defined(_MSC_VER)
  #define DATA_BIND_BINARY_IS_LITTLE_ENDIAN 1
#elif defined(__LITTLE_ENDIAN__)
  #define DATA_BIND_BINARY_IS_LITTLE_ENDIAN 1
#elif defined(__BIG_ENDIAN__)
  #define DATA_BIND_BINARY_IS_LITTLE_ENDIAN 0
#else
  #define DATA_BIND_BINARY_IS_LITTLE_ENDIAN 1
#endif

#if defined(_MSC_VER) && !defined(__clang__)
  #include <intrin.h>
  #define DATA_BIND_BINARY_BSWAP16(x) _byteswap_ushort(x)
  #define DATA_BIND_BINARY_BSWAP32(x) _byteswap_ulong(x)
  #define DATA_BIND_BINARY_BSWAP64(x) _byteswap_uint64(x)
#else
  #define DATA_BIND_BINARY_BSWAP16(x) __builtin_bswap16(x)
  #define DATA_BIND_BINARY_BSWAP32(x) __builtin_bswap32(x)
  #define DATA_BIND_BINARY_BSWAP64(x) __builtin_bswap64(x)
#endif

#if DATA_BIND_BINARY_IS_LITTLE_ENDIAN
  #define data_bind_binary_le16toh(x) (uint16_t)(x)
  #define data_bind_binary_le32toh(x) (uint32_t)(x)
  #define data_bind_binary_le64toh(x) (uint64_t)(x)
  #define data_bind_binary_htole16(x) (uint16_t)(x)
  #define data_bind_binary_htole32(x) (uint32_t)(x)
  #define data_bind_binary_htole64(x) (uint64_t)(x)
#else
  #define data_bind_binary_le16toh(x) DATA_BIND_BINARY_BSWAP16((uint16_t)(x))
  #define data_bind_binary_le32toh(x) DATA_BIND_BINARY_BSWAP32((uint32_t)(x))
  #define data_bind_binary_le64toh(x) DATA_BIND_BINARY_BSWAP64((uint64_t)(x))
  #define data_bind_binary_htole16(x) DATA_BIND_BINARY_BSWAP16((uint16_t)(x))
  #define data_bind_binary_htole32(x) DATA_BIND_BINARY_BSWAP32((uint32_t)(x))
  #define data_bind_binary_htole64(x) DATA_BIND_BINARY_BSWAP64((uint64_t)(x))
#endif

#if DATA_BIND_BINARY_IS_LITTLE_ENDIAN
  #define data_bind_binary_be16toh(x) DATA_BIND_BINARY_BSWAP16((uint16_t)(x))
  #define data_bind_binary_be32toh(x) DATA_BIND_BINARY_BSWAP32((uint32_t)(x))
  #define data_bind_binary_be64toh(x) DATA_BIND_BINARY_BSWAP64((uint64_t)(x))
  #define data_bind_binary_htobe16(x) DATA_BIND_BINARY_BSWAP16((uint16_t)(x))
  #define data_bind_binary_htobe32(x) DATA_BIND_BINARY_BSWAP32((uint32_t)(x))
  #define data_bind_binary_htobe64(x) DATA_BIND_BINARY_BSWAP64((uint64_t)(x))
#else
  #define data_bind_binary_be16toh(x) (uint16_t)(x)
  #define data_bind_binary_be32toh(x) (uint32_t)(x)
  #define data_bind_binary_be64toh(x) (uint64_t)(x)
  #define data_bind_binary_htobe16(x) (uint16_t)(x)
  #define data_bind_binary_htobe32(x) (uint32_t)(x)
  #define data_bind_binary_htobe64(x) (uint64_t)(x)
#endif

#endif /* DATA_BIND_BINARY_ENDIAN_H */
