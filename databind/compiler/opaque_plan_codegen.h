#ifndef DATABIND_COMPILER_OPAQUE_PLAN_CODEGEN_H
#define DATABIND_COMPILER_OPAQUE_PLAN_CODEGEN_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* First producer slice: canonical builtin bytes only. */
int databind_compiler_opaque_plan_admit(const char *type_name);

int databind_compiler_opaque_plan_emit(
    FILE *file,
    const char *symbol_prefix,
    size_t max_bytes);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_OPAQUE_PLAN_CODEGEN_H */
