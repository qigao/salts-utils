#ifndef DATABIND_COMPILER_ORBIT_NATIVE_PROJECTION_H
#define DATABIND_COMPILER_ORBIT_NATIVE_PROJECTION_H

#include "idl_contract.h"
#include "service_native.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional Orbit Service adapter projected by the ONLY salts-idlc Native
 * generator. Not a separate parser, backend, transport or execution engine.
 *
 * A service opts in by annotating EVERY operation:
 *     [orbit(10,100)] Add: AddRequest -> AddResponse;
 *
 * IDs identify one public Orbit Service method and one existing CFlow Machine
 * Event. Scalar unary accepts one required uint64 field per side.
 * M0k.2b additionally admits exactly one managed text/bytes-only message
 * operation per Service with native_context(1) and explicit positive,
 * bounded orbit_value(request_max,response_max) annotations. Unknown,
 * optional/nullable, nested, collection, typed error, mixed mode and
 * missing-budget shapes fail closed before any output publication.
 *
 * Generated fragments are protected by DATABIND_NATIVE_ENABLE_ORBIT, leaving
 * existing Native/Plugin output targets independent of Orbit packages.
 * A consumer must intentionally provide Orbit::Core and that compile flag.
 */
int databind_compiler_orbit_native_selected(const IdlContract *contract);
int databind_compiler_orbit_native_managed_selected(const IdlContract *contract);
int databind_compiler_orbit_native_admitted(const IdlContract *contract);
int databind_compiler_orbit_native_write_header(
    FILE *file, const IdlContract *contract,
    const databind_compiler_service_native_ir *services);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_ORBIT_NATIVE_PROJECTION_H */
