#ifndef DATABIND_COMPILER_SCHEMA_PROJECTION_H
#define DATABIND_COMPILER_SCHEMA_PROJECTION_H

#include "projection_config.h"

enum {
  DATABIND_SCHEMA_PROJECTION_HTTP = 1u,
  DATABIND_SCHEMA_PROJECTION_RPC = 2u
};

/* Compile build-only application annotations into the existing version-1 JSON
 * projection model. The immutable Contract is borrowed for the call; out owns
 * the complete config on success and is zero on failure. Unknown/misplaced
 * inject/http/app_* annotations and incomplete mappings fail closed. No file I/O. */
int databind_compiler_schema_projection_build(
    const IdlContract *contract, unsigned transports,
    databind_compiler_projection_config *out, char *error, size_t error_size);

/* CLI entry. Validates the complete annotated contract before publishing one
 * JSON file. The output never aliases the source; failure before publication
 * leaves an existing output intact. Uses the same compiler JSON config reader. */
int databind_compiler_schema_projection_export(
    const char *schema_path, const char *output_path, const char *transports,
    char *error, size_t error_size);

#endif
