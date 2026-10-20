#ifndef DATABIND_COMPILER_COMPONENT_PROVIDER_CANDIDATE_H
#define DATABIND_COMPILER_COMPONENT_PROVIDER_CANDIDATE_H

#include "service_native.h"
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
    const char *provider_abi_header,
    uint32_t major,
    uint32_t minor,
    uint32_t patch);

#endif /* DATABIND_COMPILER_COMPONENT_PROVIDER_CANDIDATE_H */
