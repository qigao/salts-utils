#ifndef TBE_SCHEMA_ENUM_H
#define TBE_SCHEMA_ENUM_H

#include "node_tree.h"
#include "data_bind_schema_error.h"

/* Normalize and validate the unpublished parser tree; never a caller-owned root. */
int schema_validate_enums(Node *root, DataBindSchemaError *error);

#endif
