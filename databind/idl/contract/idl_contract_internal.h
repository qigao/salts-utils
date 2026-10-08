#ifndef SALTS_UTILS_IDL_CONTRACT_INTERNAL_H
#define SALTS_UTILS_IDL_CONTRACT_INTERNAL_H

#include "idl_contract.h"
#include "node_tree.h"
#include "tbe_error.h"

/* Shared internal diagnostic reporting and source-language semantic admission. */
void idl_contract_diagnostic_set(
    IdlDiagnostic *diagnostic, IdlStatus status,
    int line, int column, const char *message);
int idl_logical_builtin_type(const char *name, size_t length);
int idl_contract_validate_types(
    const IdlContract *contract, IdlDiagnostic *diagnostic);

int idl_contract_build_from_tree(
    const Node *root, IdlContract **out_contract, IdlDiagnostic *diagnostic);

/* Transfer a validated schema tree, retaining caller-owned non-schema nodes.
 * Failure consumes neither root. Success transfers parsed children to root. */
int idl_contract_publish_tree(Node *root, Node *parsed);

#endif
