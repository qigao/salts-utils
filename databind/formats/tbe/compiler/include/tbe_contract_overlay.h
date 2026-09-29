#ifndef DATABIND_TBE_CONTRACT_OVERLAY_H
#define DATABIND_TBE_CONTRACT_OVERLAY_H

#include "tbe_format_plan.h"

#include <idl.h>
#include <idl_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

int databind_tbe_contract_apply(Node *root, tbe_error_t *error);

/*
 * Compatibility overlay for declaration-order schemas used by source
 * generation and the dynamic DataBind runtime. This annotates semantic/wire
 * facts without admitting the layout as a canonical TBE format plan.
 * Explicit format-plan construction remains strict.
 */
int databind_tbe_contract_apply_compat(Node *root, tbe_error_t *error);

int databind_tbe_contract_parse(
    const char *text, size_t len, Node *root, tbe_error_t *error);

int databind_tbe_format_plan_build(
    const IdlContract *contract,
    const Node *wire_ir,
    databind_tbe_format_plan *out,
    tbe_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
