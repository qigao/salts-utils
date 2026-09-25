#ifndef DATABIND_COMPILER_DATABASE_SCHEMA_H
#define DATABIND_COMPILER_DATABASE_SCHEMA_H

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

enum {
  TBE_DATABASE_SCHEMA_DIAGNOSTIC_DIALECT_CAPACITY = 16,
  TBE_DATABASE_SCHEMA_DIAGNOSTIC_NAME_CAPACITY = 128,
  TBE_DATABASE_SCHEMA_DIAGNOSTIC_CONTEXT_CAPACITY = 256
};

/**
 * Bounded, caller-owned failure detail for database schema validation.
 * All text members are NUL-terminated when returned by
 * tbe_database_schema_build().
 */
typedef struct tbe_database_schema_diagnostic_s {
  char dialect[TBE_DATABASE_SCHEMA_DIAGNOSTIC_DIALECT_CAPACITY];
  char message_name[TBE_DATABASE_SCHEMA_DIAGNOSTIC_NAME_CAPACITY];
  char field_name[TBE_DATABASE_SCHEMA_DIAGNOSTIC_NAME_CAPACITY];
  char context[TBE_DATABASE_SCHEMA_DIAGNOSTIC_CONTEXT_CAPACITY];
} tbe_database_schema_diagnostic_t;

/**
 * Builds a normalized, independently owned database schema Node tree.
 * The caller owns the returned tree and releases it with
 * tbe_database_schema_destroy().
 */
tbe_database_schema_status_t tbe_database_schema_build(
    const Node *schema_root, tbe_database_dialect_t dialect, Node **out_database_ir,
    tbe_database_schema_diagnostic_t *out_diagnostic);

/** Safely releases a database IR tree; NULL is accepted. */
void tbe_database_schema_destroy(Node *database_ir);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_DATABASE_SCHEMA_H */
