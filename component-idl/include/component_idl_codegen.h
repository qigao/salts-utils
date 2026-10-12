#ifndef SALTS_UTILS_COMPONENT_IDL_CANDIDATE_CODEGEN_H
#define SALTS_UTILS_COMPONENT_IDL_CANDIDATE_CODEGEN_H

#include <idl_contract.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compiler-only, no new parser or runtime; never installed in an SDK.
 * Admission checks the entire supported logical contract before rendering. */
int salts_component_idl_candidate_admit(
    const IdlContract *contract, const char *qualified_component_id,
    const char *native_record_header);

int salts_component_idl_candidate_render(
    FILE *output, const IdlContract *contract,
    const char *qualified_component_id, const char *native_record_header);


/* Separate, opt-in Component Provider INTERFACE export projection.
 * The existing IdlContract is the only semantic authority. No loader,
 * runtime state, ownership or Plugin FUNCTION ABI is created here. */
int salts_component_idl_candidate_render_provider_plugin(
    FILE *output, const IdlContract *contract,
    const char *qualified_component_id, const char *component_header);

#ifdef __cplusplus
}
#endif
#endif
