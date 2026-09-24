#include <salts/plugin_cflow.h>

#include <string.h>

static void plugin_cflow_zero_handle(
    salts_plugin_cflow_publisher_handle *handle) {
    if (handle != NULL)
        memset(handle, 0, sizeof(*handle));
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
    if (registry != NULL && lease != NULL &&
        salts_plugin_lease_valid(*lease)) {
        salts_plugin_status release_status =
            salts_plugin_registry_release(registry, lease);
        if (release_status != SALTS_PLUGIN_OK)
            return release_status;
    }
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
    plugin_cflow_zero_handle(out_handle);

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
        plugin_cflow_zero_handle(handle);
    else {
        /*
         * Publisher ownership has already been discharged, but retain the
         * registry/lease token so the caller can retry lease release.
         */
        handle->publisher = (cflow_publisher){0};
    }
    return status;
}


static void plugin_cflow_zero_subscription(
    salts_plugin_cflow_subscription_handle *subscription) {
    if (subscription != NULL)
        memset(subscription, 0, sizeof(*subscription));
}

cflow_status_result salts_plugin_cflow_subscribe_with_options(
    salts_plugin_cflow_subscription_handle *out_subscription,
    salts_plugin_cflow_publisher_handle *source,
    const cflow_graph *graph,
    cflow_scheduler *scheduler,
    const cflow_subscriber *subscriber,
    const cflow_eval_options *options) {
    cflow_subscription run = {0};
    cflow_status_result result;

    if (out_subscription == NULL ||
        out_subscription->registry != NULL ||
        salts_plugin_lease_valid(out_subscription->lease) ||
        out_subscription->subscription.impl != NULL ||
        !salts_plugin_cflow_publisher_handle_valid(source) ||
        graph == NULL || scheduler == NULL)
        return (cflow_status_result){
            CFLOW_STATUS_INVALID_ARGUMENT};

    result = cflow_subscribe_with_options(
        &run, graph, &source->publisher,
        scheduler, subscriber, options);
    if (!cflow_status_result_is_ok(result)) {
        /*
         * CFlow guarantees that admission failure retains Publisher ownership
         * in source. No Plugin lease transition occurs on this path.
         */
        return result;
    }

    out_subscription->registry = source->registry;
    out_subscription->lease = source->lease;
    out_subscription->subscription = run;

    source->registry = NULL;
    source->lease = (salts_plugin_lease){0};
    source->publisher = (cflow_publisher){0};

    return result;
}

cflow_status_result salts_plugin_cflow_subscribe(
    salts_plugin_cflow_subscription_handle *out_subscription,
    salts_plugin_cflow_publisher_handle *source,
    const cflow_graph *graph,
    cflow_scheduler *scheduler,
    const cflow_subscriber *subscriber) {
    return salts_plugin_cflow_subscribe_with_options(
        out_subscription, source, graph,
        scheduler, subscriber, NULL);
}

cflow_status_result salts_plugin_cflow_subscription_request_result(
    salts_plugin_cflow_subscription_handle *subscription,
    size_t demand) {
    if (!salts_plugin_cflow_subscription_handle_valid(subscription) ||
        demand == 0u)
        return (cflow_status_result){
            CFLOW_STATUS_INVALID_ARGUMENT};

    return cflow_subscription_request_result(
        &subscription->subscription, demand);
}

void salts_plugin_cflow_subscription_cancel(
    salts_plugin_cflow_subscription_handle *subscription) {
    if (!salts_plugin_cflow_subscription_handle_valid(subscription))
        return;
    cflow_subscription_cancel(&subscription->subscription);
}

salts_plugin_status salts_plugin_cflow_subscription_close(
    salts_plugin_cflow_subscription_handle *subscription) {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    salts_plugin_status status;

    if (subscription == NULL ||
        subscription->registry == NULL ||
        !salts_plugin_lease_valid(subscription->lease))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    registry = subscription->registry;
    lease = subscription->lease;

    if (subscription->subscription.impl != NULL) {
        cflow_subscription_close(&subscription->subscription);
        subscription->subscription = (cflow_subscription){0};
    }

    status = salts_plugin_registry_release(registry, &lease);
    if (status == SALTS_PLUGIN_OK)
        plugin_cflow_zero_subscription(subscription);
    else {
        subscription->registry = registry;
        subscription->lease = lease;
        subscription->subscription = (cflow_subscription){0};
    }
    return status;
}
