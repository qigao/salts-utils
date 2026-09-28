#ifndef DATABIND_BINDING_COMPILER_SERVICE_BINDING_H
#define DATABIND_BINDING_COMPILER_SERVICE_BINDING_H

#include "message_native.h"

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct databind_binding_compiler_error {
  const char *type_name;
  unsigned kind_value;
} databind_binding_compiler_error;

typedef struct databind_binding_compiler_service {
  const char *symbol;
  databind_compiler_message_native_binding request;
  databind_compiler_message_native_binding response;
  const databind_binding_compiler_error *errors;
  size_t error_count;
} databind_binding_compiler_service;

/*
 * Emit only DataBind native binding metadata. FunctionMeta/FunctionAbi,
 * invocation adapters, transports and publication artifacts are deliberately
 * outside this compiler boundary.
 */
int databind_binding_compiler_emit_service(
    FILE *file,
    const databind_binding_compiler_service *service);

#ifdef __cplusplus
}
#endif

#endif
