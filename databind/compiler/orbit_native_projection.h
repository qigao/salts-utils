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
 * Event. This first slice accepts ONLY a required single uint64 field in
 * both request/response and no typed errors, optional/null/owned fields.
 * Other shapes fail closed before any output publication.
 *
 * Generated fragments are protected by DATABIND_NATIVE_ENABLE_ORBIT, leaving
 * existing Native/Plugin output targets independent of Orbit packages.
 * A consumer must intentionally provide Orbit::Core and that compile flag.
 */
int databind_compiler_orbit_native_selected(const IdlContract *contract);
int databind_compiler_orbit_native_admitted(const IdlContract *contract);
int databind_compiler_orbit_native_write_header(
    FILE *file, const IdlContract *contract,
    const databind_compiler_service_native_ir *services);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_ORBIT_NATIVE_PROJECTION_H */
