#ifndef DATABIND_COMPILER_COMPONENT_PROVIDER_CANDIDATE_H
#define DATABIND_COMPILER_COMPONENT_PROVIDER_CANDIDATE_H

#include "service_native.h"
#include "projection.h"
#include "idl_contract.h"

#include <stdint.h>
#include <stdio.h>

/*
 * Isolated #620 IDL -> CMeta/ComponentProvider projection, one explicitly chosen
 * Service with one to four non-throwing canonical operations. It consumes the existing canonical
 * IdlContract -> ServiceNativeIR; it owns no parser, runtime, Plugin or
 * generated native signature. The ordinary PLUGIN outputs are unchanged.
 *
 * This is an opt-in generator primitive for candidate-only DSO fixtures,
 * not a newly selectable default salts_idl_target artifact.
 *
 * A false admission must not write or publish outputs. The coordinator
 * owns staging/commit. Header/source streams and all IR text are borrowed.
 */
/* Explicit compiler-private, transaction-owned COMPONENT projection.
 * It requires a separately selected canonical PLUGIN service header and
 * consumes only the same Component/ServiceNativeIR. The resulting .component
 * source is an alternative Provider DSO publication, not linkable in the
 * same DSO as .plugin.c (both define cmeta_plugin_query). */
typedef struct databind_compiler_component_provider_config {
  const char *component_id;
  const char *native_service_header;
  const char *header_output;
  uint32_t version_major;
  uint32_t version_minor;
  uint32_t version_patch;
} databind_compiler_component_provider_config;

int databind_compiler_component_provider_render_staged(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    const char *header_stage,
    const char *source_stage);

int databind_compiler_component_provider_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context);

extern const databind_compiler_projection_backend
    DATABIND_COMPILER_COMPONENT_PROVIDER_BACKEND;

int databind_component_provider_candidate_admitted(
    const IdlComponent *component,
    const databind_compiler_service_native_ir *ir);

int databind_component_provider_candidate_render(
    FILE *header,
    FILE *source,
    const IdlComponent *component,
    const databind_compiler_service_native_ir *ir,
    const char *native_service_header,
    const char *component_header,
    uint32_t major,
    uint32_t minor,
    uint32_t patch);

#endif /* DATABIND_COMPILER_COMPONENT_PROVIDER_CANDIDATE_H */
