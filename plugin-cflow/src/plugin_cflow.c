#include <salts/plugin_cflow.h>

#include <string.h>

typedef struct plugin_cflow_function_capture {
    void *context;
    salts_plugin_function_invoke_fn invoke;
} plugin_cflow_function_capture;

static void plugin_cflow_zero_publisher_handle(
    salts_plugin_cflow_publisher_handle *handle) {
    if (handle != NULL)
        memset(handle, 0, sizeof(*handle));
}

static void plugin_cflow_zero_function_handle(
    salts_plugin_cflow_function_handle *handle) {
    if (handle != NULL)
        memset(handle, 0, sizeof(*handle));
}

static bool plugin_cflow_function_invoke(
    const cmeta_callable *self,
    void *out,
    const void *const *args) {
    plugin_cflow_function_capture capture;
    void *params[1];

    if (self == NULL || args == NULL || args[0] == NULL ||
        self->capture_size != sizeof(capture))
        return false;

    memcpy(&capture, self->capture.bytes, sizeof(capture));
    if (capture.invoke == NULL)
        return false;

    params[0] = (void *)args[0];
    return capture.invoke(capture.context, out, params, 1u);
}

static cmeta_sig plugin_cflow_find_unary_signature(
    const cmeta_function_desc *function) {
    const cmeta_param_desc *param;
    cmeta_fn candidate = {0};

    if (function == NULL || function->param_count != 1u ||
        function->return_type == NULL)
        return CMETA_SIG_INVALID;

    param = cmeta_function_param(function, 0u);
    if (param == NULL || param->type == NULL)
        return CMETA_SIG_INVALID;

    for (candidate.sig = (cmeta_sig)(CMETA_SIG_INVALID + 1);
         candidate.sig < CMETA_SIG_COUNT;
         candidate.sig = (cmeta_sig)(candidate.sig + 1)) {
        const cmeta_sig_desc *signature = cmeta_fn_signature(candidate);
        if (signature != NULL &&
            signature->protocol == CMETA_FN_PROTOCOL_VALUE &&
            signature->param_count == 1u &&
            cmeta_type_equal(signature->params[0], param->type) &&
            cmeta_type_equal(signature->return_type, function->return_type))
            return candidate.sig;
    }
    return CMETA_SIG_INVALID;
}

static bool plugin_cflow_make_function_callable(
    const salts_plugin_function_export *function,
    cmeta_callable *out) {
    plugin_cflow_function_capture capture;
    cmeta_sig sig;

    if (function == NULL || out == NULL ||
        function->desc == NULL || function->invoke == NULL)
        return false;

    sig = plugin_cflow_find_unary_signature(function->desc);
    if (sig == CMETA_SIG_INVALID)
        return false;

    memset(out, 0, sizeof(*out));
    out->meta.sig = sig;
    out->meta.effects = function->desc->effects;
    out->meta.properties = function->desc->properties;
    out->invoke = plugin_cflow_function_invoke;
    out->dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;

    capture.context = function->context;
    capture.invoke = function->invoke;
    if (sizeof(capture) > CMETA_CAPTURE_INLINE)
        return false;
    out->capture_size = sizeof(capture);
    memcpy(out->capture.bytes, &capture, sizeof(capture));
    return true;
}

static salts_plugin_status plugin_cflow_release_lease(
    salts_plugin_registry *registry,
    salts_plugin_lease *lease,
    salts_plugin_status status) {
    if (registry != NULL && lease != NULL &&
        salts_plugin_lease_valid(*lease)) {
        salts_plugin_status release_status =
            salts_plugin_registry_release(registry, lease);
        if (release_status != SALTS_PLUGIN_OK)
            return release_status;
    }
    return status;
}

static salts_plugin_status plugin_cflow_release_failed_acquire(
    salts_plugin_registry *registry,
    salts_plugin_lease *lease,
    cflow_publisher *publisher,
    salts_plugin_status status) {
    if (publisher != NULL && cflow_publisher_valid(publisher)) {
        cflow_publisher_cancel(publisher);
        cflow_publisher_destroy(publisher);
        memset(publisher, 0, sizeof(*publisher));
    }
    return plugin_cflow_release_lease(registry, lease, status);
}

salts_plugin_status salts_plugin_cflow_function_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    cflow_op op,
    salts_plugin_cflow_function_handle *out_handle,
    cflow_function_projection_status *projection_status) {
    salts_plugin_lease lease = {0};
    const salts_plugin_manifest *manifest = NULL;
    const salts_plugin_export *entry = NULL;
    cmeta_callable callable = {0};
    cflow_function_projection projection = {0};
    cflow_function_projection_status admitted;
    salts_plugin_status status;

    if (out_handle == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    plugin_cflow_zero_function_handle(out_handle);

    if (registry == NULL || !salts_plugin_ref_valid(plugin) ||
        export_id == NULL || contract_id == NULL || contract_version == 0u)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    status = salts_plugin_registry_acquire(
        registry, plugin, &lease, &manifest);
    if (status != SALTS_PLUGIN_OK)
        return status;

    status = salts_plugin_manifest_find_export(
        manifest, export_id, &entry);
    if (status != SALTS_PLUGIN_OK)
        return plugin_cflow_release_lease(registry, &lease, status);

    status = salts_plugin_export_require_function(
        entry, contract_id, contract_version, required_capabilities);
    if (status != SALTS_PLUGIN_OK)
        return plugin_cflow_release_lease(registry, &lease, status);

    if (!plugin_cflow_make_function_callable(&entry->value.function, &callable)) {
        if (projection_status != NULL)
            *projection_status = CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;
        return plugin_cflow_release_lease(
            registry, &lease, SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
    }

    admitted = cflow_function_projection_admit(
        entry->value.function.desc,
        entry->value.function.abi,
        callable,
        op,
        &projection);
    if (projection_status != NULL)
        *projection_status = admitted;
    if (admitted != CFLOW_FUNCTION_PROJECTION_OK)
        return plugin_cflow_release_lease(
            registry, &lease, SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);

    out_handle->registry = registry;
    out_handle->lease = lease;
    out_handle->projection = projection;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_cflow_function_release(
    salts_plugin_cflow_function_handle *handle) {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    salts_plugin_status status;

    if (handle == NULL || handle->registry == NULL ||
        !salts_plugin_lease_valid(handle->lease))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    registry = handle->registry;
    lease = handle->lease;
    status = salts_plugin_registry_release(registry, &lease);
    if (status == SALTS_PLUGIN_OK)
        plugin_cflow_zero_function_handle(handle);
    return status;
}

salts_plugin_status salts_plugin_cflow_publisher_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_type_desc *expected_output_type,
    salts_plugin_cflow_publisher_handle *out_handle) {
    salts_plugin_lease lease = {0};
    const salts_plugin_manifest *manifest = NULL;
    const salts_plugin_export *entry = NULL;
    salts_plugin_cflow_publisher_provider *provider;
    cflow_publisher publisher = {0};
    const cmeta_type_desc *output_type;
    salts_plugin_status status;

    if (out_handle == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    plugin_cflow_zero_publisher_handle(out_handle);

    if (registry == NULL || !salts_plugin_ref_valid(plugin) ||
        export_id == NULL || contract_id == NULL ||
        contract_version == 0u ||
        (expected_output_type != NULL &&
         !cmeta_type_desc_valid(expected_output_type)))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    status = salts_plugin_registry_acquire(
        registry, plugin, &lease, &manifest);
    if (status != SALTS_PLUGIN_OK)
        return status;

    status = salts_plugin_manifest_find_export(
        manifest, export_id, &entry);
    if (status != SALTS_PLUGIN_OK)
        return plugin_cflow_release_failed_acquire(
            registry, &lease, &publisher, status);

    status = salts_plugin_export_require_interface(
        entry, contract_id, contract_version,
        required_capabilities,
        salts_plugin_cflow_publisher_provider_interface());
    if (status != SALTS_PLUGIN_OK)
        return plugin_cflow_release_failed_acquire(
            registry, &lease, &publisher, status);

    provider =
        (salts_plugin_cflow_publisher_provider *)
            entry->value.interface.value;
    if (!salts_plugin_cflow_publisher_provider_valid(provider))
        return plugin_cflow_release_failed_acquire(
            registry, &lease, &publisher,
            SALTS_PLUGIN_INVALID_MANIFEST);

    if (!salts_plugin_cflow_publisher_provider_open(
            provider, &publisher) ||
        !cflow_publisher_valid(&publisher))
        return plugin_cflow_release_failed_acquire(
            registry, &lease, &publisher,
            SALTS_PLUGIN_INVALID_STATE);

    output_type = cflow_publisher_output_type(&publisher);
    if (!cmeta_type_desc_valid(output_type) ||
        (expected_output_type != NULL &&
         !cmeta_type_equal(output_type, expected_output_type)))
        return plugin_cflow_release_failed_acquire(
            registry, &lease, &publisher,
            SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);

    out_handle->registry = registry;
    out_handle->lease = lease;
    out_handle->publisher = publisher;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_cflow_publisher_release(
    salts_plugin_cflow_publisher_handle *handle) {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    salts_plugin_status status;

    if (handle == NULL || handle->registry == NULL ||
        !salts_plugin_lease_valid(handle->lease))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    registry = handle->registry;
    lease = handle->lease;

    if (cflow_publisher_valid(&handle->publisher)) {
        cflow_publisher_cancel(&handle->publisher);
        cflow_publisher_destroy(&handle->publisher);
        memset(&handle->publisher, 0, sizeof(handle->publisher));
    }

    status = salts_plugin_registry_release(registry, &lease);
    if (status == SALTS_PLUGIN_OK)
        plugin_cflow_zero_publisher_handle(handle);
    else {
        handle->publisher = (cflow_publisher){0};
    }
    return status;
}
