#ifndef DATA_BIND_PLUGIN_CATALOG_H
#define DATA_BIND_PLUGIN_CATALOG_H

#include "data_bind_binding_plan.h"

#include <cmeta/interface.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DATA_BIND_PLUGIN_CATALOG_ABI_VERSION = 1u };

#define DATA_BIND_PLUGIN_CATALOG_EXPORT_ID "databind.service.catalog"
#define DATA_BIND_PLUGIN_CATALOG_CONTRACT_ID "databind.service.catalog"
#define DATA_BIND_PLUGIN_CATALOG_CONTRACT_VERSION 1u

/*
 * Caller-owned snapshot of one generated Service operation.
 *
 * request/response own the resolved native binding rows in this snapshot.
 * native.request/native.response always point back to those exact rows.
 * All strings, FunctionDesc, DataDesc and typed-error metadata reachable from
 * the snapshot remain borrowed from the generated Plugin DSO and therefore
 * require the Plugin lease to stay live.
 */
typedef struct DataBindPluginOperationBinding {
  size_t size;
  uint32_t abi_version;

  const char *export_id;
  const char *service_name;
  const char *operation_name;

  DataBindNativeTypeBinding request;
  DataBindNativeTypeBinding response;
  DataBindServiceNativeBinding native;
} DataBindPluginOperationBinding;

#define DATA_BIND_PLUGIN_OPERATION_BINDING_INIT                               \
  { sizeof(DataBindPluginOperationBinding),                                   \
    DATA_BIND_PLUGIN_CATALOG_ABI_VERSION, NULL, NULL, NULL,                    \
    DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL),                            \
    DATA_BIND_NATIVE_TYPE_BINDING_INIT(NULL, NULL),                            \
    DATA_BIND_SERVICE_NATIVE_BINDING_INIT(NULL, NULL, NULL) }

static inline int data_bind_plugin_operation_binding_valid(
    const DataBindPluginOperationBinding *binding) {
  return binding != NULL &&
         binding->size >= sizeof(*binding) &&
         binding->abi_version == DATA_BIND_PLUGIN_CATALOG_ABI_VERSION &&
         binding->export_id != NULL && binding->export_id[0] != '\0' &&
         binding->service_name != NULL && binding->service_name[0] != '\0' &&
         binding->operation_name != NULL &&
         binding->operation_name[0] != '\0' &&
         binding->request.size >= sizeof(binding->request) &&
         binding->request.abi_version == DATA_BIND_NATIVE_BINDING_ABI_VERSION &&
         binding->request.idl_type_name != NULL &&
         cmeta_data_desc_valid(binding->request.data) &&
         binding->response.size >= sizeof(binding->response) &&
         binding->response.abi_version == DATA_BIND_NATIVE_BINDING_ABI_VERSION &&
         binding->response.idl_type_name != NULL &&
         cmeta_data_desc_valid(binding->response.data) &&
         binding->native.size >= sizeof(binding->native) &&
         binding->native.abi_version == DATA_BIND_BINDING_PLAN_ABI_VERSION &&
         cmeta_function_desc_valid(binding->native.function) &&
         binding->native.request == &binding->request &&
         binding->native.response == &binding->response;
}

/*
 * Generated logical-Service catalog.
 *
 * create_codec() creates one caller-owned DataBind codec for the exact schema
 * used by every operation row in this catalog.
 *
 * operation_at() resolves generated native message bindings into caller-owned
 * snapshot storage. It performs control-plane metadata resolution only; no
 * Plugin registry or dynamic schema lookup is involved.
 */
#define DATA_BIND_PLUGIN_CATALOG_METHODS(X, I)                                \
  X(I, R2, DataBindStatus, create_codec, DataBind **, out_codec,               \
    DataBindError *, error)                                                    \
  X(I, R0, size_t, operation_count, _)                                         \
  X(I, R3, DataBindStatus, operation_at, size_t, index,                         \
    DataBindPluginOperationBinding *, out, DataBindError *, error)

CMETA_INTERFACE(data_bind_plugin_catalog, DATA_BIND_PLUGIN_CATALOG_METHODS);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_PLUGIN_CATALOG_H */
