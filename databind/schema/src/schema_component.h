#ifndef DATABIND_SCHEMA_COMPONENT_H
#define DATABIND_SCHEMA_COMPONENT_H

#include "node_tree.h"
#include "tbe_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validate and normalize transport/artifact-neutral Component capability
 * composition after Service semantic validation.
 *
 * The current grammar admits Service references only. Future capability kinds
 * (for example Channel) extend the same canonical capabilities[] list.
 */
int schema_validate_components(Node *root, tbe_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_SCHEMA_COMPONENT_H */
