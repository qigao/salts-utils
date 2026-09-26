#ifndef DATABIND_COMPILER_PROJECTION_H
#define DATABIND_COMPILER_PROJECTION_H

#include "node_tree.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compiler generation is selected along orthogonal semantic axes.
 *
 * Artifact kinds describe produced runtime/tooling artifacts.
 * Transport kinds describe delivery mappings over a selected FormatPlan.
 * They are deliberately separate enums so HTTP/RPC cannot become artifact
 * identities and PLUGIN/WASM cannot become transport identities.
 *
 * This is compiler-private architecture, not an installed runtime ABI.
 */
typedef enum databind_compiler_projection_axis {
  DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT = 1,
  DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT = 2
} databind_compiler_projection_axis;

typedef enum databind_compiler_artifact_kind {
  DATABIND_COMPILER_ARTIFACT_NATIVE = 1,
  DATABIND_COMPILER_ARTIFACT_PLUGIN,
  DATABIND_COMPILER_ARTIFACT_WASM,
  DATABIND_COMPILER_ARTIFACT_OPENAPI,
  DATABIND_COMPILER_ARTIFACT_MOCK
} databind_compiler_artifact_kind;

typedef enum databind_compiler_transport_kind {
  DATABIND_COMPILER_TRANSPORT_HTTP = 1,
  DATABIND_COMPILER_TRANSPORT_RPC,
  DATABIND_COMPILER_TRANSPORT_SOCKET,
  DATABIND_COMPILER_TRANSPORT_FLOWMQ,
  DATABIND_COMPILER_TRANSPORT_MQTT,
  DATABIND_COMPILER_TRANSPORT_WEBSOCKET
} databind_compiler_transport_kind;

typedef struct databind_compiler_projection_id {
  databind_compiler_projection_axis axis;
  uint32_t kind;
} databind_compiler_projection_id;

typedef struct databind_compiler_projection_request {
  databind_compiler_projection_id id;

  /*
   * Optional backend-owned output root/path. The dispatcher does not interpret
   * it; the selected transport/artifact generator owns the path contract.
   */
  const char *output;

  /* Optional immutable generator configuration owned by the caller. */
  const void *config;
} databind_compiler_projection_request;

typedef int (*databind_compiler_projection_generate_fn)(
    const Node *canonical_ir,
    const databind_compiler_projection_request *request,
    void *context);

typedef struct databind_compiler_projection_backend {
  databind_compiler_projection_id id;
  const char *name;
  databind_compiler_projection_generate_fn generate;
  void *context;
} databind_compiler_projection_backend;

const char *databind_compiler_artifact_name(
    databind_compiler_artifact_kind kind);

int databind_compiler_artifact_parse(
    const char *name,
    databind_compiler_artifact_kind *out_kind);

const char *databind_compiler_transport_name(
    databind_compiler_transport_kind kind);

int databind_compiler_transport_parse(
    const char *name,
    databind_compiler_transport_kind *out_kind);

const char *databind_compiler_projection_id_name(
    databind_compiler_projection_id id);

int databind_compiler_projection_id_equal(
    databind_compiler_projection_id left,
    databind_compiler_projection_id right);

/*
 * Validate a complete selected generation set before any backend executes.
 * Duplicate IDs are rejected across both axes.
 */
int databind_compiler_projection_requests_valid(
    const databind_compiler_projection_request *requests,
    size_t request_count);

/*
 * Execute selected artifact/transport generators against one already-parsed
 * immutable canonical IR.
 *
 * Admission is all-or-nothing: every request must have exactly one matching
 * backend before the first generate callback runs. The dispatcher never creates
 * a format x transport Cartesian product; each request is one typed axis ID.
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
