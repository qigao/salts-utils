#include <salts/plugin.h>
#include <cmeta/data.h>
#include <cmeta/declared_type.h>
#include <cmeta/struct.h>
#include <cmeta/type_identity.h>
#include <tinytest.h>

#include "generic_plugin.plugin_client.h"

#include <stdio.h>
#include <string.h>

#ifndef GENERATED_GENERIC_PLUGIN_PATH
#error "GENERATED_GENERIC_PLUGIN_PATH is required"
#endif

typedef databind_plugin_client_13_GenericPlugin_16_GenericProcessor
    GenericProcessorPluginClient;

static const cmeta_declared_type *generic_field_declared(
    const cmeta_data_desc *data, const char *field_name) {
  const cmeta_data_struct_shape *shape;
  const cmeta_field_desc *field;

  if (data == NULL || field_name == NULL ||
      data->kind != CMETA_DATA_STRUCT || data->shape == NULL)
    return NULL;
  shape = (const cmeta_data_struct_shape *)data->shape;
  if (shape->layout == NULL) return NULL;
  field = cmeta_struct_find_field(shape->layout, field_name);
  return field != NULL ? field->declared_type : NULL;
}

static int generic_application_identity(
    const cmeta_declared_type *declared,
    const cmeta_generic_desc *constructor,
    cmeta_type_identity *out,
    const cmeta_type_identity **arguments,
    size_t capacity) {
  size_t index;

  if (declared == NULL || constructor == NULL || out == NULL ||
      arguments == NULL || !cmeta_declared_type_valid(declared) ||
      declared->arity == 0u || declared->arity > capacity)
    return 0;

  for (index = 0u; index < declared->arity; ++index) {
    const cmeta_type_desc *argument =
        cmeta_declared_type_argument(declared, index);
    arguments[index] = cmeta_type_identity_of(argument);
    if (arguments[index] == NULL) return 0;
  }

  *out = (cmeta_type_identity){
      CMETA_TYPE_APPLY, NULL, constructor, NULL,
      arguments, declared->arity};
  return cmeta_type_identity_valid(out);
}

static void check_semantic_data_copy(const cmeta_data_desc *provider_data) {
  cmeta_type_desc storage_copy;
  cmeta_type_identity identity_copy;
  cmeta_data_desc data_copy;
  char atom_id[256];
  char data_id[256];

  check_not_null(provider_data);
  check_not_null(provider_data != NULL ? provider_data->storage_type : NULL);
  if (provider_data == NULL || provider_data->storage_type == NULL)
    return;

  {
    const cmeta_type_identity *identity =
        cmeta_type_identity_of(provider_data->storage_type);
    check_not_null(identity);
    if (identity == NULL || identity->form != CMETA_TYPE_ATOM ||
        identity->stable_atom_id == NULL)
      return;
    check(strlen(identity->stable_atom_id) < sizeof(atom_id));
    if (strlen(identity->stable_atom_id) >= sizeof(atom_id))
      return;
    snprintf(atom_id, sizeof(atom_id), "%s", identity->stable_atom_id);
    identity_copy = *identity;
    identity_copy.stable_atom_id = atom_id;
  }

  check_not_null(provider_data->stable_id);
  if (provider_data->stable_id == NULL ||
      strlen(provider_data->stable_id) >= sizeof(data_id))
    return;
  snprintf(data_id, sizeof(data_id), "%s", provider_data->stable_id);

  storage_copy = *provider_data->storage_type;
  storage_copy.identity = &identity_copy;
  data_copy = *provider_data;
  data_copy.stable_id = data_id;
  data_copy.storage_type = &storage_copy;

  check_true(provider_data != &data_copy);
  check_true(provider_data->storage_type != &storage_copy);
  check_true(cmeta_type_identity_of(provider_data->storage_type) !=
             &identity_copy);
  check_true(cmeta_data_desc_equal(provider_data, &data_copy));
}

static void check_generic_descriptor_copy(
    const cmeta_data_desc *provider_data,
    const char *field_name) {
  const cmeta_declared_type *declared =
      generic_field_declared(provider_data, field_name);
  const cmeta_type_identity *provider_args[2] = {NULL, NULL};
  const cmeta_type_identity *peer_args[2] = {NULL, NULL};
  cmeta_type_identity peer_arg_copies[2];
  cmeta_generic_desc provider_constructor_copy;
  cmeta_generic_desc peer_constructor_copy;
  cmeta_type_identity provider_identity;
  cmeta_type_identity peer_identity;
  char provider_id[256];
  char peer_id[256];
  char peer_arg_ids[2][256];
  size_t index;

  check_not_null(declared);
  if (declared == NULL || !cmeta_declared_type_valid(declared) ||
      declared->constructor == NULL ||
      declared->constructor->stable_id == NULL ||
      declared->arity == 0u || declared->arity > 2u)
    return;

  check(strlen(declared->constructor->stable_id) < sizeof(provider_id));
  if (strlen(declared->constructor->stable_id) >= sizeof(provider_id))
    return;
  snprintf(provider_id, sizeof(provider_id), "%s",
           declared->constructor->stable_id);
  snprintf(peer_id, sizeof(peer_id), "%s",
           declared->constructor->stable_id);

  provider_constructor_copy = *declared->constructor;
  provider_constructor_copy.stable_id = provider_id;
  peer_constructor_copy = *declared->constructor;
  peer_constructor_copy.stable_id = peer_id;

  check_true(&provider_constructor_copy != &peer_constructor_copy);
  check_true(declared->constructor != &provider_constructor_copy);
  check_true(declared->constructor != &peer_constructor_copy);

  for (index = 0u; index < declared->arity; ++index) {
    const cmeta_type_desc *argument =
        cmeta_declared_type_argument(declared, index);
    const cmeta_type_identity *identity =
        cmeta_type_identity_of(argument);

    check_not_null(argument);
    check_not_null(identity);
    if (argument == NULL || identity == NULL ||
        identity->form != CMETA_TYPE_ATOM ||
        identity->stable_atom_id == NULL ||
        strlen(identity->stable_atom_id) >= sizeof(peer_arg_ids[index]))
      return;

    provider_args[index] = identity;
    snprintf(peer_arg_ids[index], sizeof(peer_arg_ids[index]), "%s",
             identity->stable_atom_id);
    peer_arg_copies[index] = *identity;
    peer_arg_copies[index].stable_atom_id = peer_arg_ids[index];
    peer_args[index] = &peer_arg_copies[index];

    check_true(provider_args[index] != peer_args[index]);
    check_true(cmeta_type_identity_equal(
        provider_args[index], peer_args[index]));
  }

  provider_identity = (cmeta_type_identity){
      CMETA_TYPE_APPLY, NULL, &provider_constructor_copy, NULL,
      provider_args, declared->arity};
  peer_identity = (cmeta_type_identity){
      CMETA_TYPE_APPLY, NULL, &peer_constructor_copy, NULL,
      peer_args, declared->arity};

  check_true(cmeta_type_identity_valid(&provider_identity));
  check_true(cmeta_type_identity_valid(&peer_identity));
  check_true(cmeta_type_identity_equal(
      &provider_identity, &peer_identity));
}

static const cmeta_data_desc *generic_field_data(
    const cmeta_data_desc *data, const char *field_name) {
  const cmeta_data_struct_shape *shape;
  size_t index;

  if (data == NULL || field_name == NULL ||
      data->kind != CMETA_DATA_STRUCT || data->shape == NULL)
    return NULL;
  shape = (const cmeta_data_struct_shape *)data->shape;
  for (index = 0u; index < shape->field_count; ++index)
    if (shape->fields[index].name != NULL &&
        strcmp(shape->fields[index].name, field_name) == 0)
      return shape->fields[index].value;
  return NULL;
}

spec("generated Plugin generic descriptor graph") {
  it("keeps provider generic metadata valid under the client lease") {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {.capacity = 2u};
    salts_plugin_ref ref = {0};
    salts_plugin_lifecycle_info info = {0};
    GenericProcessorPluginClient client =
        GENERICPLUGIN_GENERICPROCESSOR_PLUGIN_CLIENT_INIT;
    const DataBindPluginOperationBinding *operation;
    bool quiescent = true;
    GenericRequest_t request = {0};
    GenericResponse_t response = {0};
    int native_status = 123;

    check_equal(salts_plugin_registry_init(&registry, &config),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_load(
                    &registry, GENERATED_GENERIC_PLUGIN_PATH, &ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_start(&registry, ref),
                SALTS_PLUGIN_OK);

    check_equal(
        databind_plugin_client_13_GenericPlugin_16_GenericProcessor_open(
            &registry, ref, &client),
        SALTS_PLUGIN_OK);
    check_true(
        databind_plugin_client_13_GenericPlugin_16_GenericProcessor_valid(
            &client));

    operation =
        &client.databind_13_GenericPlugin_14_GenericService_7_Inspect_operation;
    check_true(data_bind_plugin_operation_binding_valid(operation));
    check_not_null(operation->request.data);
    check_not_null(operation->response.data);

    check_semantic_data_copy(operation->request.data);
    check_semantic_data_copy(operation->response.data);
    check_generic_descriptor_copy(operation->request.data, "values");
    check_generic_descriptor_copy(operation->response.data, "values");

    {
      const cmeta_data_desc *request_values =
          generic_field_data(operation->request.data, "values");
      const cmeta_data_desc *response_values =
          generic_field_data(operation->response.data, "values");
      const cmeta_data_desc *request_item;
      const cmeta_data_desc *response_key;
      const cmeta_data_desc *response_item;

      check_not_null(request_values);
      check_not_null(response_values);
      request_item = request_values != NULL
          ? cmeta_data_collection_element_data(request_values)
          : NULL;
      response_key = response_values != NULL
          ? cmeta_data_map_key_data(response_values)
          : NULL;
      response_item = response_values != NULL
          ? cmeta_data_map_value_data(response_values)
          : NULL;

      check_not_null(request_item);
      check_not_null(response_key);
      check_not_null(response_item);
      if (request_item != NULL && response_item != NULL) {
        const cmeta_data_desc *managed_name =
            generic_field_data(request_item, "name");
        cmeta_data_temp temp = {0};

        check_equal(request_item->kind, CMETA_DATA_STRUCT);
        check_equal(response_item->kind, CMETA_DATA_STRUCT);
        check_true(cmeta_data_desc_equal(request_item, response_item));
        check_semantic_data_copy(request_item);
        check_semantic_data_copy(response_item);

        check_not_null(managed_name);
        if (managed_name != NULL)
          check_equal(managed_name->kind, CMETA_DATA_STRING);
        check_true(cmeta_data_value_traits_supported(request_item));
        check_true(cmeta_data_value_traits_supported(response_item));

        /*
         * This temporary is opened and destroyed while the Plugin lease is
         * live, proving that provider-reachable managed lifecycle callbacks
         * remain callable for the complete dependent-value lifetime.
         */
        check_equal(cmeta_data_temp_open(request_item, 4096u, &temp),
                    CMETA_OK);
        cmeta_data_temp_close(&temp);
        check_null(temp.storage);
        check_null(temp.data);
      }
      if (response_key != NULL)
        check_equal(response_key->kind, CMETA_DATA_STRING);
    }

    {
      const cmeta_declared_type *request_declared =
          generic_field_declared(operation->request.data, "values");
      const cmeta_declared_type *response_declared =
          generic_field_declared(operation->response.data, "values");
      const cmeta_type_identity *request_args[2] = {NULL, NULL};
      const cmeta_type_identity *response_args[2] = {NULL, NULL};
      cmeta_type_identity request_identity;
      cmeta_type_identity response_identity;

      check_not_null(request_declared);
      check_not_null(response_declared);
      if (request_declared != NULL && response_declared != NULL &&
          request_declared->constructor != NULL &&
          response_declared->constructor != NULL) {
        check_true(generic_application_identity(
            request_declared, request_declared->constructor,
            &request_identity, request_args, 2u));
        check_true(generic_application_identity(
            response_declared, response_declared->constructor,
            &response_identity, response_args, 2u));
        check_false(cmeta_type_identity_equal(
            &request_identity, &response_identity));
      }
    }

    check_equal(salts_plugin_registry_get_lifecycle(
                    &registry, ref, &info),
                SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    check_equal(salts_plugin_registry_request_stop(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_BUSY);
    check_equal(salts_plugin_registry_poll_quiescent(
                    &registry, ref, &quiescent),
                SALTS_PLUGIN_OK);
    check_false(quiescent);

    check_equal(
        databind_plugin_client_13_GenericPlugin_16_GenericProcessor_close(
            &client),
        SALTS_PLUGIN_OK);
    check_false(
        databind_plugin_client_13_GenericPlugin_16_GenericProcessor_valid(
            &client));
    check_equal(
        client.databind_13_GenericPlugin_14_GenericService_7_Inspect_operation.size,
        (size_t)0u);
    check_null(
        client.databind_13_GenericPlugin_14_GenericService_7_Inspect_export);

    check_equal(
        databind_13_GenericPlugin_14_GenericService_7_Inspect_plugin_client_call(
            &client, &request, &response, &native_status),
        SALTS_PLUGIN_INVALID_STATE);
    check_equal(native_status, 123);

    check_equal(salts_plugin_registry_poll_quiescent(
                    &registry, ref, &quiescent),
                SALTS_PLUGIN_OK);
    check_true(quiescent);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_destroy(&registry),
                SALTS_PLUGIN_OK);
  }
}
