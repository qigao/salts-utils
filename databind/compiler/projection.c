#include "projection.h"

#include <string.h>

typedef struct projection_name_row {
  databind_compiler_projection_kind kind;
  const char *name;
} projection_name_row;

static const projection_name_row PROJECTION_NAMES[] = {
    {DATABIND_COMPILER_PROJECTION_NATIVE, "native"},
    {DATABIND_COMPILER_PROJECTION_HTTP, "http"},
    {DATABIND_COMPILER_PROJECTION_RPC, "rpc"},
    {DATABIND_COMPILER_PROJECTION_PLUGIN, "plugin"},
    {DATABIND_COMPILER_PROJECTION_WASM, "wasm"},
    {DATABIND_COMPILER_PROJECTION_OPENAPI, "openapi"},
    {DATABIND_COMPILER_PROJECTION_MOCK, "mock"},
};

static int projection_kind_known(databind_compiler_projection_kind kind) {
  size_t i;
  for (i = 0; i < sizeof(PROJECTION_NAMES) / sizeof(PROJECTION_NAMES[0]); ++i)
    if (PROJECTION_NAMES[i].kind == kind) return 1;
  return 0;
}

const char *databind_compiler_projection_name(
    databind_compiler_projection_kind kind) {
  size_t i;
  for (i = 0; i < sizeof(PROJECTION_NAMES) / sizeof(PROJECTION_NAMES[0]); ++i)
    if (PROJECTION_NAMES[i].kind == kind) return PROJECTION_NAMES[i].name;
  return NULL;
}

int databind_compiler_projection_parse(
    const char *name,
    databind_compiler_projection_kind *out_kind) {
  size_t i;
  if (name == NULL || name[0] == '\0' || out_kind == NULL) return -1;

  for (i = 0; i < sizeof(PROJECTION_NAMES) / sizeof(PROJECTION_NAMES[0]); ++i) {
    if (strcmp(name, PROJECTION_NAMES[i].name) == 0) {
      *out_kind = PROJECTION_NAMES[i].kind;
      return 0;
    }
  }
  return -1;
}

int databind_compiler_projection_requests_valid(
    const databind_compiler_projection_request *requests,
    size_t request_count) {
  size_t i;
  size_t j;

  if (request_count != 0u && requests == NULL) return 0;

  for (i = 0u; i < request_count; ++i) {
    if (!projection_kind_known(requests[i].kind)) return 0;
    for (j = 0u; j < i; ++j)
      if (requests[i].kind == requests[j].kind) return 0;
  }
  return 1;
}

static int backends_valid(
    const databind_compiler_projection_backend *backends,
    size_t backend_count) {
  size_t i;
  size_t j;

  if (backend_count != 0u && backends == NULL) return 0;

  for (i = 0u; i < backend_count; ++i) {
    const char *canonical_name;
    if (!projection_kind_known(backends[i].kind) ||
        backends[i].name == NULL ||
        backends[i].name[0] == '\0' ||
        backends[i].generate == NULL)
      return 0;

    canonical_name = databind_compiler_projection_name(backends[i].kind);
    if (canonical_name == NULL ||
        strcmp(backends[i].name, canonical_name) != 0)
      return 0;

    for (j = 0u; j < i; ++j)
      if (backends[i].kind == backends[j].kind) return 0;
  }
  return 1;
}

static const databind_compiler_projection_backend *find_backend(
    const databind_compiler_projection_backend *backends,
    size_t backend_count,
    databind_compiler_projection_kind kind) {
  size_t i;
  for (i = 0u; i < backend_count; ++i)
    if (backends[i].kind == kind) return &backends[i];
  return NULL;
}

int databind_compiler_projection_run(
    const Node *canonical_ir,
    const databind_compiler_projection_request *requests,
    size_t request_count,
    const databind_compiler_projection_backend *backends,
    size_t backend_count) {
  size_t i;

  if (canonical_ir == NULL ||
      !databind_compiler_projection_requests_valid(requests, request_count) ||
      !backends_valid(backends, backend_count))
    return -1;

  /*
   * Resolve the complete request set before invoking anything. A missing
   * backend must not produce a partial output set.
   */
  for (i = 0u; i < request_count; ++i)
    if (find_backend(backends, backend_count, requests[i].kind) == NULL)
      return -1;

  for (i = 0u; i < request_count; ++i) {
    const databind_compiler_projection_backend *backend =
        find_backend(backends, backend_count, requests[i].kind);
    if (backend->generate(canonical_ir, &requests[i],
                          backend->context) != 0)
      return -1;
  }
  return 0;
}
