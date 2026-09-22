#include <salts/plugin.h>

static bool bounded_string_valid(const char *value, size_t max_bytes) {
    size_t index;
    if (value == NULL || max_bytes == 0u) return false;
    for (index = 0u; index <= max_bytes; ++index) {
        if (value[index] == '\0') return index != 0u;
    }
    return false;
}

static bool bounded_string_equal(const char *left, const char *right,
                                 size_t max_bytes) {
    size_t index;
    if (left == NULL || right == NULL || max_bytes == 0u) return false;
    for (index = 0u; index <= max_bytes; ++index) {
        if (left[index] != right[index]) return false;
        if (left[index] == '\0') return index != 0u;
    }
    return false;
}

static bool interface_method_valid(const cmeta_interface_method_desc *method) {
    return method != NULL &&
           bounded_string_valid(method->name, SALTS_PLUGIN_INTERFACE_TOKEN_MAX) &&
           bounded_string_valid(method->return_type,
                                SALTS_PLUGIN_INTERFACE_TOKEN_MAX) &&
           method->arity <= 4u;
}

bool salts_plugin_interface_desc_valid(const cmeta_interface_desc *desc) {
    size_t index;
    size_t other;

    if (desc == NULL ||
        !bounded_string_valid(desc->name, SALTS_PLUGIN_CONTRACT_ID_MAX) ||
        desc->method_count > SALTS_PLUGIN_MAX_INTERFACE_METHODS ||
        (desc->method_count != 0u && desc->methods == NULL))
        return false;

    for (index = 0u; index < desc->method_count; ++index) {
        if (!interface_method_valid(&desc->methods[index])) return false;
        for (other = 0u; other < index; ++other) {
            if (bounded_string_equal(desc->methods[index].name,
                                     desc->methods[other].name,
                                     SALTS_PLUGIN_INTERFACE_TOKEN_MAX))
                return false;
        }
    }
    return true;
}

bool salts_plugin_interface_desc_equal(const cmeta_interface_desc *left,
                                       const cmeta_interface_desc *right) {
    size_t index;

    if (!salts_plugin_interface_desc_valid(left) ||
        !salts_plugin_interface_desc_valid(right) ||
        left->method_count != right->method_count ||
        !bounded_string_equal(left->name, right->name,
                              SALTS_PLUGIN_CONTRACT_ID_MAX))
        return false;

    for (index = 0u; index < left->method_count; ++index) {
        const cmeta_interface_method_desc *a = &left->methods[index];
        const cmeta_interface_method_desc *b = &right->methods[index];
        if (a->arity != b->arity ||
            !bounded_string_equal(a->name, b->name,
                                  SALTS_PLUGIN_INTERFACE_TOKEN_MAX) ||
            !bounded_string_equal(a->return_type, b->return_type,
                                  SALTS_PLUGIN_INTERFACE_TOKEN_MAX))
            return false;
    }
    return true;
}

bool salts_plugin_callable_contract_equal(const cmeta_callable *left,
                                          const cmeta_callable *right) {
    cmeta_callable bound_left;
    cmeta_callable bound_right;

    if (left == NULL || right == NULL ||
        !cmeta_callable_bind(*left, &bound_left) ||
        !cmeta_callable_bind(*right, &bound_right) ||
        !cmeta_callable_contract_valid(bound_left) ||
        !cmeta_callable_contract_valid(bound_right))
        return false;

    /*
     * Dispatch mode, target address and capture storage are implementation
     * representation. Plugin semantic compatibility is the CMeta signature
     * plus declared effects/properties under contract_id/version.
     */
    return bound_left.meta.sig == bound_right.meta.sig &&
           bound_left.meta.effects == bound_right.meta.effects &&
           bound_left.meta.properties == bound_right.meta.properties;
}

static salts_plugin_status validate_export(const salts_plugin_export *entry) {
    cmeta_callable bound;

    if (entry == NULL ||
        entry->struct_size < SALTS_PLUGIN_EXPORT_V1_SIZE)
        return SALTS_PLUGIN_INVALID_MANIFEST;
    if (entry->abi_version != SALTS_PLUGIN_EXPORT_ABI_VERSION)
        return SALTS_PLUGIN_UNSUPPORTED_ABI;
    if (!bounded_string_valid(entry->export_id, SALTS_PLUGIN_EXPORT_ID_MAX) ||
        !bounded_string_valid(entry->contract_id,
                              SALTS_PLUGIN_CONTRACT_ID_MAX) ||
        entry->contract_version == 0u)
        return SALTS_PLUGIN_INVALID_MANIFEST;

    switch (entry->kind) {
    case SALTS_PLUGIN_EXPORT_INTERFACE:
        if (entry->interface_value == NULL || entry->callable != NULL ||
            !salts_plugin_interface_desc_valid(entry->interface_desc))
            return SALTS_PLUGIN_INVALID_MANIFEST;
        break;
    case SALTS_PLUGIN_EXPORT_CALLABLE:
        if (entry->interface_desc != NULL || entry->interface_value != NULL ||
            entry->callable == NULL ||
            !cmeta_callable_bind(*entry->callable, &bound) ||
            !cmeta_callable_contract_valid(bound))
            return SALTS_PLUGIN_INVALID_MANIFEST;
        break;
    default:
        return SALTS_PLUGIN_INVALID_MANIFEST;
    }

    return SALTS_PLUGIN_OK;
}

bool salts_plugin_export_contract_equal(const salts_plugin_export *left,
                                        const salts_plugin_export *right) {
    if (validate_export(left) != SALTS_PLUGIN_OK ||
        validate_export(right) != SALTS_PLUGIN_OK ||
        left->kind != right->kind ||
        left->contract_version != right->contract_version ||
        !bounded_string_equal(left->contract_id, right->contract_id,
                              SALTS_PLUGIN_CONTRACT_ID_MAX))
        return false;

    if (left->kind == SALTS_PLUGIN_EXPORT_INTERFACE)
        return salts_plugin_interface_desc_equal(left->interface_desc,
                                                 right->interface_desc);

    return salts_plugin_callable_contract_equal(left->callable,
                                                right->callable);
}

bool salts_plugin_export_has_capabilities(const salts_plugin_export *entry,
                                          uint64_t required) {
    return entry != NULL && (entry->capabilities & required) == required;
}

static bool contract_key_equal(const salts_plugin_export *entry,
                               const char *contract_id,
                               uint32_t contract_version) {
    return entry != NULL && contract_version != 0u &&
           bounded_string_valid(contract_id, SALTS_PLUGIN_CONTRACT_ID_MAX) &&
           entry->contract_version == contract_version &&
           bounded_string_equal(entry->contract_id, contract_id,
                                SALTS_PLUGIN_CONTRACT_ID_MAX);
}

salts_plugin_status salts_plugin_export_require_interface(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_interface_desc *expected_interface) {
    salts_plugin_status status = validate_export(entry);

    if (status != SALTS_PLUGIN_OK) return status;
    if (contract_version == 0u ||
        !bounded_string_valid(contract_id, SALTS_PLUGIN_CONTRACT_ID_MAX) ||
        !salts_plugin_interface_desc_valid(expected_interface))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    if (entry->kind != SALTS_PLUGIN_EXPORT_INTERFACE ||
        !contract_key_equal(entry, contract_id, contract_version) ||
        !salts_plugin_export_has_capabilities(entry, required_capabilities) ||
        !salts_plugin_interface_desc_equal(entry->interface_desc,
                                           expected_interface))
        return SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;

    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_export_require_callable(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_callable *expected_callable) {
    cmeta_callable bound;
    salts_plugin_status status = validate_export(entry);

    if (status != SALTS_PLUGIN_OK) return status;
    if (contract_version == 0u ||
        !bounded_string_valid(contract_id, SALTS_PLUGIN_CONTRACT_ID_MAX) ||
        expected_callable == NULL ||
        !cmeta_callable_bind(*expected_callable, &bound) ||
        !cmeta_callable_contract_valid(bound))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    if (entry->kind != SALTS_PLUGIN_EXPORT_CALLABLE ||
        !contract_key_equal(entry, contract_id, contract_version) ||
        !salts_plugin_export_has_capabilities(entry, required_capabilities) ||
        !salts_plugin_callable_contract_equal(entry->callable,
                                              expected_callable))
        return SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;

    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_manifest_validate(
    const salts_plugin_manifest *manifest,
    uint32_t host_abi) {
    size_t index;
    size_t other;
    bool has_lifecycle;

    if (manifest == NULL || host_abi == 0u)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    if (host_abi != SALTS_PLUGIN_ABI_VERSION)
        return SALTS_PLUGIN_UNSUPPORTED_ABI;
    if (manifest->struct_size < SALTS_PLUGIN_MANIFEST_V1_SIZE)
        return SALTS_PLUGIN_INVALID_MANIFEST;
    if (manifest->abi_version != host_abi)
        return SALTS_PLUGIN_UNSUPPORTED_ABI;
    if (!bounded_string_valid(manifest->plugin_id, SALTS_PLUGIN_ID_MAX))
        return SALTS_PLUGIN_INVALID_MANIFEST;
    if (manifest->export_count > SALTS_PLUGIN_MAX_EXPORTS)
        return SALTS_PLUGIN_CAPACITY_EXCEEDED;
    if (manifest->export_count != 0u && manifest->exports == NULL)
        return SALTS_PLUGIN_INVALID_MANIFEST;

    has_lifecycle = manifest->start != NULL ||
                    manifest->request_stop != NULL ||
                    manifest->is_quiescent != NULL ||
                    manifest->destroy != NULL;
    if (has_lifecycle && manifest->self == NULL)
        return SALTS_PLUGIN_INVALID_MANIFEST;

    for (index = 0u; index < manifest->export_count; ++index) {
        salts_plugin_status status = validate_export(&manifest->exports[index]);
        if (status != SALTS_PLUGIN_OK) return status;

        for (other = 0u; other < index; ++other) {
            if (bounded_string_equal(manifest->exports[index].export_id,
                                     manifest->exports[other].export_id,
                                     SALTS_PLUGIN_EXPORT_ID_MAX))
                return SALTS_PLUGIN_DUPLICATE_EXPORT;
        }
    }

    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_manifest_find_export(
    const salts_plugin_manifest *manifest,
    const char *export_id,
    const salts_plugin_export **out_export) {
    size_t index;
    salts_plugin_status status;

    if (out_export == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_export = NULL;

    if (!bounded_string_valid(export_id, SALTS_PLUGIN_EXPORT_ID_MAX))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    status = salts_plugin_manifest_validate(manifest,
                                            SALTS_PLUGIN_ABI_VERSION);
    if (status != SALTS_PLUGIN_OK) return status;

    for (index = 0u; index < manifest->export_count; ++index) {
        if (bounded_string_equal(manifest->exports[index].export_id,
                                 export_id, SALTS_PLUGIN_EXPORT_ID_MAX)) {
            *out_export = &manifest->exports[index];
            return SALTS_PLUGIN_OK;
        }
    }

    return SALTS_PLUGIN_UNKNOWN_EXPORT;
}

const char *salts_plugin_status_string(salts_plugin_status status) {
    switch (status) {
    case SALTS_PLUGIN_OK: return "ok";
    case SALTS_PLUGIN_INVALID_ARGUMENT: return "invalid_argument";
    case SALTS_PLUGIN_INVALID_MANIFEST: return "invalid_manifest";
    case SALTS_PLUGIN_UNSUPPORTED_ABI: return "unsupported_abi";
    case SALTS_PLUGIN_DUPLICATE_PLUGIN_ID: return "duplicate_plugin_id";
    case SALTS_PLUGIN_DUPLICATE_EXPORT: return "duplicate_export";
    case SALTS_PLUGIN_UNKNOWN_EXPORT: return "unknown_export";
    case SALTS_PLUGIN_INCOMPATIBLE_CONTRACT: return "incompatible_contract";
    case SALTS_PLUGIN_CAPACITY_EXCEEDED: return "capacity_exceeded";
    case SALTS_PLUGIN_ALLOCATION_FAILED: return "allocation_failed";
    case SALTS_PLUGIN_LOAD_FAILED: return "load_failed";
    case SALTS_PLUGIN_QUERY_MISSING: return "query_missing";
    case SALTS_PLUGIN_QUERY_REJECTED: return "query_rejected";
    case SALTS_PLUGIN_UNKNOWN_PLUGIN: return "unknown_plugin";
    case SALTS_PLUGIN_STALE: return "stale";
    case SALTS_PLUGIN_LIFECYCLE_UNSUPPORTED: return "lifecycle_unsupported";
    case SALTS_PLUGIN_UNLOAD_FAILED: return "unload_failed";
    default: return "unknown";
    }
}
