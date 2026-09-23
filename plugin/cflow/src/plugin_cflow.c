#include <salts/plugin_cflow.h>

typedef bool (*plugin_cflow_interface_valid_fn)(const void *provider);
typedef uint64_t (*plugin_cflow_interface_caps_fn)(const void *provider);

static bool binding_slot_empty(
    const salts_plugin_registry *registry,
    salts_plugin_lease lease,
    const void *provider) {
    return registry == NULL &&
           !salts_plugin_lease_valid(lease) &&
           provider == NULL;
}

static bool publisher_valid_erased(const void *provider) {
    return cflow_publisher_valid((const cflow_publisher *)provider);
}

static uint64_t publisher_caps_erased(const void *provider) {
    return cflow_publisher_capabilities(
        (const cflow_publisher *)provider);
}

static bool executor_valid_erased(const void *provider) {
    return cflow_executor_valid((const cflow_executor *)provider);
}

static uint64_t executor_caps_erased(const void *provider) {
    return cflow_executor_capabilities(
        (const cflow_executor *)provider);
}

static bool scheduler_valid_erased(const void *provider) {
    return cflow_scheduler_valid((const cflow_scheduler *)provider);
}

static uint64_t scheduler_caps_erased(const void *provider) {
    return cflow_scheduler_capabilities(
        (const cflow_scheduler *)provider);
}

static salts_plugin_status release_after_failure(
    salts_plugin_registry *registry,
    salts_plugin_lease *lease,
    salts_plugin_status primary) {
    salts_plugin_status released =
        salts_plugin_registry_release(registry, lease);
    return released == SALTS_PLUGIN_OK ? primary : released;
}

static salts_plugin_status acquire_interface(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    const char *contract_id,
    uint64_t required_capabilities,
    const cmeta_interface_desc *expected_interface,
    plugin_cflow_interface_valid_fn valid,
    plugin_cflow_interface_caps_fn capabilities,
    salts_plugin_lease *out_lease,
    void **out_provider) {
    salts_plugin_lease lease = {0};
    const salts_plugin_manifest *manifest = NULL;
    const salts_plugin_export *entry = NULL;
    void *provider;
    uint64_t actual_capabilities;
    salts_plugin_status status;

    if (registry == NULL || export_id == NULL ||
        contract_id == NULL || expected_interface == NULL ||
        valid == NULL || capabilities == NULL ||
        out_lease == NULL || out_provider == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    *out_lease = (salts_plugin_lease){0};
    *out_provider = NULL;

    status = salts_plugin_registry_acquire(
        registry, plugin, &lease, &manifest);
    if (status != SALTS_PLUGIN_OK)
        return status;

    status = salts_plugin_manifest_find_export(
        manifest, export_id, &entry);
    if (status != SALTS_PLUGIN_OK)
        return release_after_failure(registry, &lease, status);

    status = salts_plugin_export_require_interface(
        entry,
        contract_id,
        SALTS_PLUGIN_CFLOW_CONTRACT_VERSION,
        required_capabilities,
        expected_interface);
    if (status != SALTS_PLUGIN_OK)
        return release_after_failure(registry, &lease, status);

    provider = entry->interface_value;
    if (!valid(provider))
        return release_after_failure(
            registry, &lease, SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);

    actual_capabilities = capabilities(provider);
    if (entry->capabilities != actual_capabilities ||
        (actual_capabilities & required_capabilities) !=
            required_capabilities)
        return release_after_failure(
            registry, &lease, SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);

    *out_lease = lease;
    *out_provider = provider;
    return SALTS_PLUGIN_OK;
}

static salts_plugin_status release_binding(
    salts_plugin_registry *registry,
    salts_plugin_lease *lease,
    const void *provider) {
    if (registry == NULL || lease == NULL || provider == NULL ||
        !salts_plugin_lease_valid(*lease))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    return salts_plugin_registry_release(registry, lease);
}

salts_plugin_status salts_plugin_cflow_acquire_publisher(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    uint64_t required_capabilities,
    salts_plugin_cflow_publisher_binding *out) {
    void *provider = NULL;
    salts_plugin_status status;

    if (out == NULL ||
        !binding_slot_empty(out->registry, out->lease, out->publisher))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    status = acquire_interface(
        registry, plugin, export_id,
        SALTS_PLUGIN_CFLOW_PUBLISHER_CONTRACT_ID,
        required_capabilities,
        cflow_publisher_interface(),
        publisher_valid_erased,
        publisher_caps_erased,
        &out->lease,
        &provider);
    if (status != SALTS_PLUGIN_OK)
        return status;

    out->registry = registry;
    out->publisher = (cflow_publisher *)provider;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_cflow_release_publisher(
    salts_plugin_cflow_publisher_binding *binding) {
    if (binding == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    {
        salts_plugin_status status = release_binding(
            binding->registry, &binding->lease, binding->publisher);
        if (status == SALTS_PLUGIN_OK) {
            binding->registry = NULL;
            binding->publisher = NULL;
        }
        return status;
    }
}

salts_plugin_status salts_plugin_cflow_acquire_executor(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    uint64_t required_capabilities,
    salts_plugin_cflow_executor_binding *out) {
    void *provider = NULL;
    salts_plugin_status status;

    if (out == NULL ||
        !binding_slot_empty(out->registry, out->lease, out->executor))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    status = acquire_interface(
        registry, plugin, export_id,
        SALTS_PLUGIN_CFLOW_EXECUTOR_CONTRACT_ID,
        required_capabilities,
        cflow_executor_interface(),
        executor_valid_erased,
        executor_caps_erased,
        &out->lease,
        &provider);
    if (status != SALTS_PLUGIN_OK)
        return status;

    out->registry = registry;
    out->executor = (cflow_executor *)provider;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_cflow_release_executor(
    salts_plugin_cflow_executor_binding *binding) {
    if (binding == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    {
        salts_plugin_status status = release_binding(
            binding->registry, &binding->lease, binding->executor);
        if (status == SALTS_PLUGIN_OK) {
            binding->registry = NULL;
            binding->executor = NULL;
        }
        return status;
    }
}

salts_plugin_status salts_plugin_cflow_acquire_scheduler(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    uint64_t required_capabilities,
    salts_plugin_cflow_scheduler_binding *out) {
    void *provider = NULL;
    salts_plugin_status status;

    if (out == NULL ||
        !binding_slot_empty(out->registry, out->lease, out->scheduler))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    status = acquire_interface(
        registry, plugin, export_id,
        SALTS_PLUGIN_CFLOW_SCHEDULER_CONTRACT_ID,
        required_capabilities,
        cflow_scheduler_interface(),
        scheduler_valid_erased,
        scheduler_caps_erased,
        &out->lease,
        &provider);
    if (status != SALTS_PLUGIN_OK)
        return status;

    out->registry = registry;
    out->scheduler = (cflow_scheduler *)provider;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_cflow_release_scheduler(
    salts_plugin_cflow_scheduler_binding *binding) {
    if (binding == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    {
        salts_plugin_status status = release_binding(
            binding->registry, &binding->lease, binding->scheduler);
        if (status == SALTS_PLUGIN_OK) {
            binding->registry = NULL;
            binding->scheduler = NULL;
        }
        return status;
    }
}
