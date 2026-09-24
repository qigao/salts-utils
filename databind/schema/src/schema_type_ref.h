#ifndef DATABIND_SCHEMA_TYPE_REF_H
#define DATABIND_SCHEMA_TYPE_REF_H

#include "node_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical DataBind schema type-reference resolution shared by interactions. */
Node *schema_type_ref_node(Node *root, const char *name);
int schema_type_ref_exists(Node *root, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_SCHEMA_TYPE_REF_H */
