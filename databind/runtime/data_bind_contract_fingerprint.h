#ifndef DATA_BIND_CONTRACT_FINGERPRINT_H
#define DATA_BIND_CONTRACT_FINGERPRINT_H

#include "node_tree.h"
#include <stdint.h>

enum { DATA_BIND_CONTRACT_FINGERPRINT_SIZE = 32 };

int data_bind_contract_fingerprint(
    const Node *root,
    uint8_t out[DATA_BIND_CONTRACT_FINGERPRINT_SIZE]);

#endif
