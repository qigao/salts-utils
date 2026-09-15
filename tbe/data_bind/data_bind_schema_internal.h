#ifndef DATA_BIND_SCHEMA_INTERNAL_H
#define DATA_BIND_SCHEMA_INTERNAL_H

#include "node_tree.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATA_BIND_SCHEMA_FINGERPRINT_SIZE 32U

int data_bind_schema_fingerprint(
    const Node *root,
    uint8_t out[DATA_BIND_SCHEMA_FINGERPRINT_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
