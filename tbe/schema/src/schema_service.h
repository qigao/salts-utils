#ifndef SCHEMA_SERVICE_H
#define SCHEMA_SERVICE_H

#include "node_tree.h"
#include "tbe_error.h"

#ifdef __cplusplus
extern "C" {
#endif

int schema_validate_services(Node *root, tbe_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* SCHEMA_SERVICE_H */
