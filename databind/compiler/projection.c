#include "projection.h"

#include <string.h>

typedef struct artifact_name_row {
  databind_compiler_artifact_kind kind;
  const char *name;
} artifact_name_row;

typedef struct transport_name_row {
  databind_compiler_transport_kind kind;
  const char *name;
} transport_name_row;

static const artifact_name_row ARTIFACT_NAMES[] = {
    {DATABIND_COMPILER_ARTIFACT_NATIVE, "native"},
    {DATABIND_COMPILER_ARTIFACT_PLUGIN, "plugin"},
    {DATABIND_COMPILER_ARTIFACT_WASM, "wasm"},
    {DATABIND_COMPILER_ARTIFACT_OPENAPI, "openapi"},
    {DATABIND_COMPILER_ARTIFACT_MOCK, "mock"},
};

static const transport_name_row TRANSPORT_NAMES[] = {
    {DATABIND_COMPILER_TRANSPORT_HTTP, "http"},
    {DATABIND_COMPILER_TRANSPORT_RPC, "rpc"},
    {DATABIND_COMPILER_TRANSPORT_SOCKET, "socket"},
    {DATABIND_COMPILER_TRANSPORT_FLOWMQ, "flowmq"},
    {DATABIND_COMPILER_TRANSPORT_MQTT, "mqtt"},
    {DATABIND_COMPILER_TRANSPORT_WEBSOCKET, "websocket"},
};

static int artifact_kind_known(databind_compiler_artifact_kind kind) {
  size_t i;
  for (i = 0u; i < sizeof(ARTIFACT_NAMES) / sizeof(ARTIFACT_NAMES[0]); ++i)
    if (ARTIFACT_NAMES[i].kind == kind) return 1;
  return 0;
}

static int transport_kind_known(databind_compiler_transport_kind kind) {
  size_t i;
  for (i = 0u; i < sizeof(TRANSPORT_NAMES) / sizeof(TRANSPORT_NAMES[0]); ++i)
    if (TRANSPORT_NAMES[i].kind == kind) return 1;
  return 0;
}

const char *databind_compiler_artifact_name(
    databind_compiler_artifact_kind kind) {
  size_t i;
  for (i = 0u; i < sizeof(ARTIFACT_NAMES) / sizeof(ARTIFACT_NAMES[0]); ++i)
    if (ARTIFACT_NAMES[i].kind == kind) return ARTIFACT_NAMES[i].name;
  return NULL;
}

int databind_compiler_artifact_parse(
    const char *name,
    databind_compiler_artifact_kind *out_kind) {
  size_t i;
  if (name == NULL || name[0] == '\0' || out_kind == NULL) return -1;
  for (i = 0u; i < sizeof(ARTIFACT_NAMES) / sizeof(ARTIFACT_NAMES[0]); ++i) {
    if (strcmp(name, ARTIFACT_NAMES[i].name) == 0) {
      *out_kind = ARTIFACT_NAMES[i].kind;
      return 0;
    }
  }
  return -1;
}

const char *databind_compiler_transport_name(
    databind_compiler_transport_kind kind) {
  size_t i;
  for (i = 0u; i < sizeof(TRANSPORT_NAMES) / sizeof(TRANSPORT_NAMES[0]); ++i)
    if (TRANSPORT_NAMES[i].kind == kind) return TRANSPORT_NAMES[i].name;
  return NULL;
}

int databind_compiler_transport_parse(
    const char *name,
    databind_compiler_transport_kind *out_kind) {
  size_t i;
  if (name == NULL || name[0] == '\0' || out_kind == NULL) return -1;
  for (i = 0u; i < sizeof(TRANSPORT_NAMES) / sizeof(TRANSPORT_NAMES[0]); ++i) {
    if (strcmp(name, TRANSPORT_NAMES[i].name) == 0) {
      *out_kind = TRANSPORT_NAMES[i].kind;
      return 0;
    }
  }
  return -1;
}

const char *databind_compiler_projection_id_name(
    databind_compiler_projection_id id) {
  if (id.axis == DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT)
    return databind_compiler_artifact_name(
        (databind_compiler_artifact_kind)id.kind);
  if (id.axis == DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT)
    return databind_compiler_transport_name(
        (databind_compiler_transport_kind)id.kind);
  return NULL;
}

int databind_compiler_projection_id_equal(
    databind_compiler_projection_id left,
    databind_compiler_projection_id right) {
  return left.axis == right.axis && left.kind == right.kind;
}

static int projection_id_known(databind_compiler_projection_id id) {
  if (id.axis == DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT)
    return artifact_kind_known((databind_compiler_artifact_kind)id.kind);
  if (id.axis == DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT)
    return transport_kind_known((databind_compiler_transport_kind)id.kind);
  return 0;
}

int databind_compiler_projection_requests_valid(
    const databind_compiler_projection_request *requests,
    size_t request_count) {
  size_t i;
  size_t j;

  if (request_count != 0u && requests == NULL) return 0;

  for (i = 0u; i < request_count; ++i) {
    if (!projection_id_known(requests[i].id)) return 0;
    for (j = 0u; j < i; ++j)
      if (databind_compiler_projection_id_equal(
              requests[i].id, requests[j].id))
        return 0;
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
    if (!projection_id_known(backends[i].id) ||
        backends[i].name == NULL ||
        backends[i].name[0] == '\0' ||
        backends[i].generate == NULL)
      return 0;

    canonical_name = databind_compiler_projection_id_name(backends[i].id);
    if (canonical_name == NULL ||
        strcmp(backends[i].name, canonical_name) != 0)
      return 0;

    for (j = 0u; j < i; ++j)
      if (databind_compiler_projection_id_equal(
              backends[i].id, backends[j].id))
        return 0;
  }
  return 1;
}

static const databind_compiler_projection_backend *find_backend(
    const databind_compiler_projection_backend *backends,
    size_t backend_count,
    databind_compiler_projection_id id) {
  size_t i;
  for (i = 0u; i < backend_count; ++i)
    if (databind_compiler_projection_id_equal(backends[i].id, id))
      return &backends[i];
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

  for (i = 0u; i < request_count; ++i)
    if (find_backend(backends, backend_count, requests[i].id) == NULL)
      return -1;

  for (i = 0u; i < request_count; ++i) {
    const databind_compiler_projection_backend *backend =
        find_backend(backends, backend_count, requests[i].id);
    if (backend->generate(canonical_ir, &requests[i],
                          backend->context) != 0)
      return -1;
  }
  return 0;
}
