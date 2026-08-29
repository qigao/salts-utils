#ifndef TBE_COMPILER_DATABASE_SCHEMA_H
#define TBE_COMPILER_DATABASE_SCHEMA_H

#include "node_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tbe_database_dialect_e {
  TBE_DATABASE_DIALECT_SQLITE = 0,
  TBE_DATABASE_DIALECT_POSTGRESQL = 1
} tbe_database_dialect_t;

typedef enum tbe_database_schema_status_e {
  TBE_DATABASE_SCHEMA_STATUS_OK = 0,
  TBE_DATABASE_SCHEMA_STATUS_INVALID_ARGUMENT = 1,
  TBE_DATABASE_SCHEMA_STATUS_UNSUPPORTED = 2,
  TBE_DATABASE_SCHEMA_STATUS_INVALID_SCHEMA = 3,
  TBE_DATABASE_SCHEMA_STATUS_OUT_OF_MEMORY = 4
} tbe_database_schema_status_t;

/**
 * Builds a normalized, independently owned database schema Node tree.
 * The caller owns the returned tree and releases it with
 * tbe_database_schema_destroy().
 */
tbe_database_schema_status_t tbe_database_schema_build(
    const Node *schema_root, tbe_database_dialect_t dialect, Node **out_database_ir);

/** Safely releases a database IR tree; NULL is accepted. */
void tbe_database_schema_destroy(Node *database_ir);

#ifdef __cplusplus
}
#endif

#endif /* TBE_COMPILER_DATABASE_SCHEMA_H */
