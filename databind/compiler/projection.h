#ifndef DATABIND_COMPILER_PROJECTION_H
#define DATABIND_COMPILER_PROJECTION_H

#include "node_tree.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Artifact/backend projections are orthogonal to source-language rendering.
 *
 * This is compiler-private architecture, not an installed runtime ABI.
 */
typedef enum databind_compiler_projection_kind {
  DATABIND_COMPILER_PROJECTION_NATIVE = 1,
  DATABIND_COMPILER_PROJECTION_HTTP,
  DATABIND_COMPILER_PROJECTION_RPC,
  DATABIND_COMPILER_PROJECTION_PLUGIN,
  DATABIND_COMPILER_PROJECTION_WASM,
  DATABIND_COMPILER_PROJECTION_OPENAPI,
  DATABIND_COMPILER_PROJECTION_MOCK
} databind_compiler_projection_kind;

typedef struct databind_compiler_projection_request {
  databind_compiler_projection_kind kind;

  /*
   * Optional backend-owned output root/path. Phase 1A does not interpret it.
   * Later CLI/CMake lowering may derive deterministic files from this value.
   */
  const char *output;

  /* Optional immutable backend configuration owned by the caller. */
  const void *config;
} databind_compiler_projection_request;

typedef int (*databind_compiler_projection_generate_fn)(
    const Node *canonical_ir,
    const databind_compiler_projection_request *request,
    void *context);

typedef struct databind_compiler_projection_backend {
  databind_compiler_projection_kind kind;
  const char *name;
  databind_compiler_projection_generate_fn generate;
  void *context;
} databind_compiler_projection_backend;

const char *databind_compiler_projection_name(
    databind_compiler_projection_kind kind);

int databind_compiler_projection_parse(
    const char *name,
    databind_compiler_projection_kind *out_kind);

/*
 * Validate a complete selected projection set before any backend executes.
 * Duplicate kinds are rejected; one component cannot request the same backend
 * twice with subtly different configuration.
 */
int databind_compiler_projection_requests_valid(
    const databind_compiler_projection_request *requests,
    size_t request_count);

/*
 * Execute selected backends against one already-parsed/normalized immutable
 * canonical IR. A backend must derive any mutable/backend-specific IR in its own
 * storage rather than annotating the shared source tree.
 *
 * Admission is all-or-nothing: every request must have exactly one matching
 * backend before the first generate callback runs.
 *
 * Returns 0 on success, non-zero on invalid selection/backend registration or
 * backend generation failure.
 */
int databind_compiler_projection_run(
    const Node *canonical_ir,
    const databind_compiler_projection_request *requests,
    size_t request_count,
    const databind_compiler_projection_backend *backends,
    size_t backend_count);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_PROJECTION_H */
