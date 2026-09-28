#ifndef SALTS_UTILS_IDL_H
#define SALTS_UTILS_IDL_H

#include "node_tree.h"
#include "tbe_error.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse one IDL contract into the current frontend-owned contract tree.
 *
 * The returned tree may contain Data declarations (message/enum/union),
 * Interaction declarations (service/channel), and Component composition.
 * The tree is frontend representation, not a DataBind runtime ABI.
 */
int idl_parse(const char *text, size_t len, Node *root, tbe_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_UTILS_IDL_H */
