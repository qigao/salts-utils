#ifndef DATA_BIND_SCHEMA_INTERNAL_H
#define DATA_BIND_SCHEMA_INTERNAL_H

#include "node_tree.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_BIND_SCHEMA_FINGERPRINT_SIZE 32U

#if !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define DATA_BIND_SCHEMA_INTERNAL __attribute__((visibility("hidden")))
#else
#define DATA_BIND_SCHEMA_INTERNAL
#endif

DATA_BIND_SCHEMA_INTERNAL int data_bind_schema_fingerprint(
    const Node *root,
    uint8_t out[DATA_BIND_SCHEMA_FINGERPRINT_SIZE]);

#undef DATA_BIND_SCHEMA_INTERNAL

#ifdef __cplusplus
}
#endif

#endif
