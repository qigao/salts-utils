#ifndef DATABIND_COMPILER_SERVICE_NATIVE_H
#define DATABIND_COMPILER_SERVICE_NATIVE_H

#include "node_tree.h"

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical compiler-private lowering of one DataBind Service operation to the
 * ordinary native C function shape shared by PLUGIN/WASM/native backends.
 */
typedef struct databind_compiler_service_native_presence {
  char *field_name;
  unsigned bit;
} databind_compiler_service_native_presence;

typedef struct databind_compiler_service_native_error {
  char *type_name;
  char *type_identity;
  unsigned kind_value;
} databind_compiler_service_native_error;


typedef struct databind_compiler_service_native_operation {
  char *schema_name;
  char *service_name;
  char *operation_name;

  char *qualified_service;
  char *qualified_operation;
  char *symbol;

  char *request_type;
  char *response_type;
  char *request_type_identity;
  char *response_type_identity;

  databind_compiler_service_native_presence *request_presence;
  size_t request_presence_count;
  databind_compiler_service_native_presence *response_presence;
  size_t response_presence_count;

  /* Ordered typed errors from the DataBind throws list. */
  databind_compiler_service_native_error *errors;
  size_t error_count;
} databind_compiler_service_native_operation;

typedef struct databind_compiler_service_native_ir {
  databind_compiler_service_native_operation *operations;
  size_t operation_count;
} databind_compiler_service_native_ir;

int databind_compiler_service_native_build(
    const Node *canonical_ir,
    databind_compiler_service_native_ir *out);

typedef int (*databind_compiler_service_native_select_fn)(
    void *context, const char *service_name);

/*
 * Build the same canonical native lowering for a selected Service subset.
 *
 * The selector is a control-plane filter over canonical Service identity.
 * Unselected Services are not lowered and therefore cannot make the selected
 * artifact fail due to backend-irrelevant native shapes.
 *
 * A NULL selector preserves the all-Service behavior.
 */
int databind_compiler_service_native_build_selected(
    const Node *canonical_ir,
    databind_compiler_service_native_select_fn select_service,
    void *select_context,
    databind_compiler_service_native_ir *out);


void databind_compiler_service_native_destroy(
    databind_compiler_service_native_ir *ir);

/* Business-facing ordinary C prototype only. */
int databind_compiler_service_native_emit_prototype(
    FILE *file,
    const databind_compiler_service_native_operation *operation);

/*
 * Emit canonical CMeta object-pointer descriptors plus FunctionMeta/FunctionAbi
 * into the current C translation unit.
 *
 * The emitted metadata symbols are:
 *   <symbol>__function_meta
 *   <symbol>__function_abi_meta
 * and are suitable for direct address-taking by publication backends.
 */
int databind_compiler_service_native_emit_reflection(
    FILE *file,
    const databind_compiler_service_native_operation *operation,
    int emit_accessors);

/*
 * Emit the generated host-side DataBind native-binding initializer. The caller
 * owns request/response/service structs for at least as long as any compiled
 * BindingPlan retains them.
 */
int databind_compiler_service_native_emit_binding(
    FILE *file,
    const databind_compiler_service_native_operation *operation);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_SERVICE_NATIVE_H */
