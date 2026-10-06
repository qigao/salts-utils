#ifndef SALTS_UTILS_IDL_CONTRACT_INTERNAL_H
#define SALTS_UTILS_IDL_CONTRACT_INTERNAL_H

#include "idl_contract.h"
#include "node_tree.h"
#include "tbe_error.h"

int idl_contract_build_from_tree(
    const Node *root, IdlContract **out_contract, IdlDiagnostic *diagnostic);

/* Transfer a validated schema tree, retaining caller-owned non-schema nodes.
 * Failure consumes neither root. Success transfers parsed children to root. */
int idl_contract_publish_tree(Node *root, Node *parsed);

#endif
