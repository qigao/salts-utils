#ifndef DATABIND_SCHEMA_CHANNEL_H
#define DATABIND_SCHEMA_CHANNEL_H

#include "node_tree.h"
#include "tbe_error.h"

#ifdef __cplusplus
extern "C" {
#endif

int schema_validate_channels(Node *root, tbe_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_SCHEMA_CHANNEL_H */
