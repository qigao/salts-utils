#include <salts/plugin.h>
#include <tinytest.h>

#include "data_bind_plugin_catalog.h"
#include "generic_graph_binding_native.h"

#include <stddef.h>
#include <string.h>

#ifndef GENERATED_DATABIND_GENERIC_PLUGIN_PATH
#error "GENERATED_DATABIND_GENERIC_PLUGIN_PATH is required"
#endif

static const cmeta_field_desc *generic_graph_struct_field(
    const cmeta_data_desc *data, const char *name) {
  const cmeta_data_struct_shape *shape;
  if (data == NULL || name == NULL || data->kind != CMETA_DATA_STRUCT ||
      data->shape == NULL)
    return NULL;
  shape = (const cmeta_data_struct_shape *)data->shape;
  return shape->layout != NULL
             ? cmeta_struct_find_field(shape->layout, name)
             : NULL;
}

static bool generic_graph_declared_equal(
    const cmeta_declared_type *left,
    const cmeta_declared_type *right) {
  const cmeta_type_identity *left_args[2] = {NULL, NULL};
  const cmeta_type_identity *right_args[2] = {NULL, NULL};
  cmeta_type_identity left_application;
  cmeta_type_identity right_application;
  size_t i;

  if (!cmeta_declared_type_valid(left) ||
      !cmeta_declared_type_valid(right) ||
      left->constructor == NULL || right->constructor == NULL ||
      left->constructor->stable_id == NULL ||
      right->constructor->stable_id == NULL ||
      left->arity != right->arity || left->arity > 2u ||
      strcmp(left->constructor->stable_id,
             right->constructor->stable_id) != 0)
    return false;

  for (i = 0u; i < left->arity; ++i) {
    const cmeta_type_desc *left_arg =
        cmeta_declared_type_argument(left, i);
    const cmeta_type_desc *right_arg =
        cmeta_declared_type_argument(right, i);
    if (!cmeta_type_equal(left_arg, right_arg))
      return false;
    left_args[i] = cmeta_type_identity_of(left_arg);
    right_args[i] = cmeta_type_identity_of(right_arg);
    if (left_args[i] == NULL || right_args[i] == NULL)
      return false;
  }

  left_application = (cmeta_type_identity){
      CMETA_TYPE_APPLY, NULL, left->constructor, NULL,
      left->arity != 0u ? left_args : NULL, left->arity};
  right_application = (cmeta_type_identity){
      CMETA_TYPE_APPLY, NULL, right->constructor, NULL,
      right->arity != 0u ? right_args : NULL, right->arity};

  return cmeta_type_identity_valid(&left_application) &&
         cmeta_type_identity_valid(&right_application) &&
         cmeta_type_identity_equal(
             &left_application, &right_application);
}

static DataBindPluginOperationBinding generic_graph_operation(
    const data_bind_plugin_catalog *catalog) {
  DataBindPluginOperationBinding result =
      DATA_BIND_PLUGIN_OPERATION_BINDING_INIT;
  size_t index;

  for (index = 0u;
       index < data_bind_plugin_catalog_operation_count(catalog);
       ++index) {
    DataBindPluginOperationBinding candidate =
        DATA_BIND_PLUGIN_OPERATION_BINDING_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;
    if (data_bind_plugin_catalog_operation_at(
            catalog, index, &candidate, &error) != DATA_BIND_OK)
      continue;
    if (candidate.export_id != NULL &&
        strcmp(candidate.export_id,
               "GenericGraph.GenericService.Qualify") == 0)
      return candidate;
  }
  return result;
}

spec("generated Plugin reflected generic graph") {
  it("keeps generic metadata valid only under the live DSO lease") {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {.capacity = 1u};
    salts_plugin_ref ref = {0};
    salts_plugin_lease lease = {0};
    salts_plugin_lifecycle_info lifecycle = {0};
    const salts_plugin_manifest *manifest = NULL;
    const salts_plugin_export *entry = NULL;
    const salts_plugin_export *catalog_entry = NULL;
    data_bind_plugin_catalog *catalog = NULL;
    DataBindPluginOperationBinding operation =
        DATA_BIND_PLUGIN_OPERATION_BINDING_INIT;
    const cmeta_data_desc *host_request = NULL;
    const cmeta_data_desc *host_response = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    bool quiescent = true;

    check_equal(salts_plugin_registry_init(&registry, &config),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_load(
                    &registry, GENERATED_DATABIND_GENERIC_PLUGIN_PATH,
                    &ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_start(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_acquire(
                    &registry, ref, &lease, &manifest),
                SALTS_PLUGIN_OK);

    check_not_null(manifest);
    check_equal(manifest->plugin_id, "GenericGraph.GenericProcessor");
    check_equal(manifest->export_count, (size_t)2u);

    check_equal(salts_plugin_manifest_find_export(
                    manifest,
                    "GenericGraph.GenericService.Qualify",
                    &entry),
                SALTS_PLUGIN_OK);
    check_not_null(entry);
    check_equal(entry->kind, SALTS_PLUGIN_EXPORT_FUNCTION);
    check_equal(salts_plugin_export_require_function(
                    entry, "GenericGraph.GenericService", 1u, 0u),
                SALTS_PLUGIN_OK);
    check_true(cmeta_function_desc_valid(entry->value.function.desc));
    check_true(cmeta_function_abi_desc_valid(entry->value.function.abi));

    check_equal(salts_plugin_manifest_find_export(
                    manifest, DATA_BIND_PLUGIN_CATALOG_EXPORT_ID,
                    &catalog_entry),
                SALTS_PLUGIN_OK);
    check_not_null(catalog_entry);
    check_equal(catalog_entry->kind, SALTS_PLUGIN_EXPORT_INTERFACE);
    check_equal(salts_plugin_export_require_interface(
                    catalog_entry,
                    DATA_BIND_PLUGIN_CATALOG_CONTRACT_ID,
                    DATA_BIND_PLUGIN_CATALOG_CONTRACT_VERSION,
                    0u,
                    data_bind_plugin_catalog_interface()),
                SALTS_PLUGIN_OK);

    catalog =
        (data_bind_plugin_catalog *)catalog_entry->value.interface.value;
    check_true(data_bind_plugin_catalog_valid(catalog));
    check_equal(data_bind_plugin_catalog_operation_count(catalog),
                (size_t)1u);

    operation = generic_graph_operation(catalog);
    check_true(data_bind_plugin_operation_binding_valid(&operation));
    check_equal(operation.export_id,
                "GenericGraph.GenericService.Qualify");
    check_true(operation.function == entry->value.function.desc);
    check_true(cmeta_function_desc_equal(
        operation.function, entry->value.function.desc));
    check_true(cmeta_data_desc_valid(operation.request.data));
    check_true(cmeta_data_desc_valid(operation.response.data));

    check_equal(GenericRequest_cmeta_data(&host_request, &error),
                DATA_BIND_OK);
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(GenericResponse_cmeta_data(&host_response, &error),
                DATA_BIND_OK);
    check_not_null(host_request);
    check_not_null(host_response);
    check_true(cmeta_data_desc_equal(
        operation.request.data, host_request));
    check_true(cmeta_data_desc_equal(
        operation.response.data, host_response));

    {
      const cmeta_param_desc *request_param =
          cmeta_function_param(entry->value.function.desc, 0u);
      const cmeta_param_desc *response_param =
          cmeta_function_param(entry->value.function.desc, 1u);
      check_not_null(request_param);
      check_not_null(response_param);
      if (request_param != NULL && response_param != NULL) {
        check_not_null(request_param->type);
        check_not_null(response_param->type);
        if (request_param->type != NULL &&
            response_param->type != NULL) {
          check_true(cmeta_type_equal(
              request_param->type->pointee,
              host_request->storage_type));
          check_true(cmeta_type_equal(
              response_param->type->pointee,
              host_response->storage_type));
        }
      }
    }

    {
      const cmeta_field_desc *provider_field =
          generic_graph_struct_field(operation.request.data, "items");
      const cmeta_field_desc *host_field =
          generic_graph_struct_field(host_request, "items");
      check_not_null(provider_field);
      check_not_null(host_field);
      if (provider_field != NULL && host_field != NULL) {
        check_not_null(provider_field->declared_type);
        check_not_null(host_field->declared_type);
        check_true(generic_graph_declared_equal(
            provider_field->declared_type,
            host_field->declared_type));
        if (provider_field->declared_type != NULL &&
            provider_field->declared_type->constructor != NULL)
          check_equal(
              provider_field->declared_type->constructor->stable_id,
              "cstl.Vec");
      }
    }

    {
      const cmeta_field_desc *provider_field =
          generic_graph_struct_field(
              operation.response.data, "by_name");
      const cmeta_field_desc *host_field =
          generic_graph_struct_field(host_response, "by_name");
      check_not_null(provider_field);
      check_not_null(host_field);
      if (provider_field != NULL && host_field != NULL) {
        check_not_null(provider_field->declared_type);
        check_not_null(host_field->declared_type);
        check_true(generic_graph_declared_equal(
            provider_field->declared_type,
            host_field->declared_type));
        if (provider_field->declared_type != NULL &&
            provider_field->declared_type->constructor != NULL)
          check_equal(
              provider_field->declared_type->constructor->stable_id,
              "cstl.Map");
      }
    }

    check_equal(salts_plugin_registry_get_lifecycle(
                    &registry, ref, &lifecycle),
                SALTS_PLUGIN_OK);
    check_equal(lifecycle.active_leases, (size_t)1u);

    /*
     * Every descriptor above is Plugin-owned borrowed storage. Stop may begin,
     * but unload remains blocked until the final lease is released.
     */
    check_equal(salts_plugin_registry_request_stop(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_BUSY);
    check_equal(salts_plugin_registry_poll_quiescent(
                    &registry, ref, &quiescent),
                SALTS_PLUGIN_OK);
    check_false(quiescent);

    check_equal(salts_plugin_registry_release(&registry, &lease),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_get_lifecycle(
                    &registry, ref, &lifecycle),
                SALTS_PLUGIN_OK);
    check_equal(lifecycle.active_leases, (size_t)0u);
    check_equal(salts_plugin_registry_poll_quiescent(
                    &registry, ref, &quiescent),
                SALTS_PLUGIN_OK);
    check_true(quiescent);

    /* No Plugin-owned descriptor/provider pointer is touched after this point. */
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_destroy(&registry),
                SALTS_PLUGIN_OK);
  }
}
