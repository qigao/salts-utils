#ifndef TBE_SCHEMA_ENUM_H
#define TBE_SCHEMA_ENUM_H

#include "node_tree.h"
#include "tbe_error.h"

/* Normalize and validate the unpublished parser tree; never a caller-owned root. */
int schema_validate_enums(Node *root, tbe_error_t *error);

#endif
