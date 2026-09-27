#ifndef DATABIND_TBE_CONTRACT_OVERLAY_H
#define DATABIND_TBE_CONTRACT_OVERLAY_H

#include <idl.h>

#ifdef __cplusplus
extern "C" {
#endif

int databind_tbe_contract_apply(Node *root, tbe_error_t *error);
int databind_tbe_contract_parse(
    const char *text, size_t len, Node *root, tbe_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
