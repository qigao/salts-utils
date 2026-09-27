#ifndef DATABIND_COMPILER_BINARY_READER_CODEGEN_H
#define DATABIND_COMPILER_BINARY_READER_CODEGEN_H

#include "node_tree.h"
#include "idl_contract.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return zero only when BinaryLayoutIR can be lowered to the current flat
 * fixed-scalar runtime reader plan.
 */
int databind_compiler_binary_reader_admit(
    const IdlContract *contract,
    const Node *wire_ir,
    const char *type_name);

/*
 * Emit one deterministic type-level Binary reader/provider block.
 *
 * symbol_prefix is the artifact-level C identifier prefix. Provider identity
 * is artifact + canonical message type; no transport identity is encoded.
 *
 * The emitted block is guarded so independently generated transport headers
 * may include the same type-level provider in one translation unit without
 * duplicate definitions.
 */
int databind_compiler_binary_reader_emit(
    FILE *file,
    const IdlContract *contract,
    const Node *wire_ir,
    const char *type_name,
    const char *symbol_prefix);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_BINARY_READER_CODEGEN_H */
