#include <salts/plugin.h>

#include "plugin_loader_internal.h"

#include <vstr.h>

#include <limits.h>
#include <stdlib.h>

typedef struct salts_plugin_registry_slot {
    salts_plugin_library library;
    const salts_plugin_manifest *manifest;
    uint32_t generation;
    bool occupied;
} salts_plugin_registry_slot;

typedef struct salts_plugin_registry_impl {
    salts_plugin_registry_slot *slots;
    size_t capacity;
    size_t count;
} salts_plugin_registry_impl;

static bool bounded_cstr_valid(const char *value, size_t max_bytes) {
    size_t index;

    if (value == NULL || max_bytes == 0u)
        return false;

    for (index = 0u; index <= max_bytes; ++index) {
        if (value[index] == '\0')
            return index != 0u;
    }
    return false;
}

static bool bounded_utf8_path_valid(const char *path) {
    size_t length;

    if (path == NULL)
        return false;

    for (length = 0u; length <= SALTS_PLUGIN_PATH_MAX; ++length) {
        if (path[length] == '\0') {
            if (length == 0u)
                return false;
            return vstr_utf8_valid(vstr_from_buf(path, length)) != 0;
        }
    }
    return false;
}

static bool plugin_id_equal(const char *left, const char *right) {
    size_t index;

    if (!bounded_cstr_valid(left, SALTS_PLUGIN_ID_MAX) ||
        !bounded_cstr_valid(right, SALTS_PLUGIN_ID_MAX))
        return false;

    for (index = 0u; index <= SALTS_PLUGIN_ID_MAX; ++index) {
        if (left[index] != right[index])
            return false;
        if (left[index] == '\0')
            return true;
    }
    return false;
}

static bool manifest_has_lifecycle(const salts_plugin_manifest *manifest) {
    return manifest != NULL &&
           (manifest->start != NULL ||
            manifest->request_stop != NULL ||
            manifest->is_quiescent != NULL ||
            manifest->destroy != NULL);
}

static uint32_t next_generation(uint32_t current) {
    uint32_t next = current + 1u;
    return next == 0u ? 1u : next;
}

static salts_plugin_status close_rejected_library(
    salts_plugin_library *library,
    salts_plugin_status rejection) {
    salts_plugin_status close_status = salts_plugin_platform_close(library);
    return close_status == SALTS_PLUGIN_OK ? rejection : close_status;
}

static salts_plugin_registry_impl *registry_impl(
    const salts_plugin_registry *registry) {
    return registry == NULL
        ? NULL
        : (salts_plugin_registry_impl *)registry->impl;
}

salts_plugin_status salts_plugin_registry_init(
    salts_plugin_registry *registry,
    const salts_plugin_registry_config *config) {
    salts_plugin_registry_impl *impl;

    if (registry == NULL || config == NULL || registry->impl != NULL ||
        config->capacity == 0u)
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    if (config->capacity > (size_t)UINT32_MAX ||
        config->capacity > SIZE_MAX / sizeof(salts_plugin_registry_slot))
        return SALTS_PLUGIN_CAPACITY_EXCEEDED;

    impl = (salts_plugin_registry_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return SALTS_PLUGIN_ALLOCATION_FAILED;

    impl->slots = (salts_plugin_registry_slot *)calloc(
        config->capacity, sizeof(*impl->slots));
    if (impl->slots == NULL) {
        free(impl);
        return SALTS_PLUGIN_ALLOCATION_FAILED;
    }

    impl->capacity = config->capacity;
    for (size_t index = 0u; index < impl->capacity; ++index)
        impl->slots[index].generation = 1u;

    registry->impl = impl;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_registry_load(
    salts_plugin_registry *registry,
    const char *path,
    salts_plugin_ref *out_ref) {
    salts_plugin_registry_impl *impl;
    salts_plugin_registry_slot *slot = NULL;
    salts_plugin_library library = {0};
    salts_plugin_query_fn query = NULL;
    const salts_plugin_manifest *manifest;
    salts_plugin_status status;
    size_t slot_index = 0u;

    if (out_ref == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_ref = (salts_plugin_ref){0};

    impl = registry_impl(registry);
    if (impl == NULL || !bounded_utf8_path_valid(path))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    if (impl->count >= impl->capacity)
        return SALTS_PLUGIN_CAPACITY_EXCEEDED;

    for (slot_index = 0u; slot_index < impl->capacity; ++slot_index) {
        if (!impl->slots[slot_index].occupied) {
            slot = &impl->slots[slot_index];
            break;
        }
    }
    if (slot == NULL)
        return SALTS_PLUGIN_CAPACITY_EXCEEDED;

    status = salts_plugin_platform_open(path, &library, &query);
    if (status != SALTS_PLUGIN_OK)
        return status;

    manifest = query(SALTS_PLUGIN_ABI_VERSION);
    if (manifest == NULL)
        return close_rejected_library(
            &library, SALTS_PLUGIN_QUERY_REJECTED);

    status = salts_plugin_manifest_validate(
        manifest, SALTS_PLUGIN_ABI_VERSION);
    if (status != SALTS_PLUGIN_OK)
        return close_rejected_library(&library, status);

    /*
     * #129 owns loading only. Lifecycle-bearing plugins stay rejected until
     * #130 supplies start/stop/quiescent-unload orchestration.
     */
    if (manifest_has_lifecycle(manifest))
        return close_rejected_library(
            &library, SALTS_PLUGIN_LIFECYCLE_UNSUPPORTED);

    for (size_t index = 0u; index < impl->capacity; ++index) {
        const salts_plugin_registry_slot *existing = &impl->slots[index];
        if (existing->occupied &&
            plugin_id_equal(existing->manifest->plugin_id,
                            manifest->plugin_id))
            return close_rejected_library(
                &library, SALTS_PLUGIN_DUPLICATE_PLUGIN_ID);
    }

    slot->library = library;
    slot->manifest = manifest;
    slot->occupied = true;
    ++impl->count;

    out_ref->slot = (uint32_t)(slot_index + 1u);
    out_ref->generation = slot->generation;
    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_registry_find(
    const salts_plugin_registry *registry,
    const char *plugin_id,
    salts_plugin_ref *out_ref) {
    salts_plugin_registry_impl *impl = registry_impl(registry);

    if (out_ref == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_ref = (salts_plugin_ref){0};

    if (impl == NULL ||
        !bounded_cstr_valid(plugin_id, SALTS_PLUGIN_ID_MAX))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    for (size_t index = 0u; index < impl->capacity; ++index) {
        const salts_plugin_registry_slot *slot = &impl->slots[index];
        if (slot->occupied &&
            plugin_id_equal(slot->manifest->plugin_id, plugin_id)) {
            out_ref->slot = (uint32_t)(index + 1u);
            out_ref->generation = slot->generation;
            return SALTS_PLUGIN_OK;
        }
    }

    return SALTS_PLUGIN_UNKNOWN_PLUGIN;
}

salts_plugin_status salts_plugin_registry_manifest(
    const salts_plugin_registry *registry,
    salts_plugin_ref ref,
    const salts_plugin_manifest **out_manifest) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    salts_plugin_registry_slot *slot;
    size_t index;

    if (out_manifest == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_manifest = NULL;

    if (impl == NULL || !salts_plugin_ref_valid(ref))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    index = (size_t)ref.slot - 1u;
    if (index >= impl->capacity)
        return SALTS_PLUGIN_STALE;

    slot = &impl->slots[index];
    if (!slot->occupied || slot->generation != ref.generation)
        return SALTS_PLUGIN_STALE;

    *out_manifest = slot->manifest;
    return SALTS_PLUGIN_OK;
}

size_t salts_plugin_registry_count(const salts_plugin_registry *registry) {
    salts_plugin_registry_impl *impl = registry_impl(registry);
    return impl == NULL ? 0u : impl->count;
}

salts_plugin_status salts_plugin_registry_destroy(
    salts_plugin_registry *registry) {
    salts_plugin_registry_impl *impl;
    bool close_failed = false;

    if (registry == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    if (registry->impl == NULL)
        return SALTS_PLUGIN_OK;

    impl = (salts_plugin_registry_impl *)registry->impl;
    for (size_t index = 0u; index < impl->capacity; ++index) {
        salts_plugin_registry_slot *slot = &impl->slots[index];
        salts_plugin_status status;

        if (!slot->occupied)
            continue;

        status = salts_plugin_platform_close(&slot->library);
        if (status != SALTS_PLUGIN_OK) {
            close_failed = true;
            continue;
        }

        slot->manifest = NULL;
        slot->occupied = false;
        slot->generation = next_generation(slot->generation);
        --impl->count;
    }

    if (close_failed)
        return SALTS_PLUGIN_UNLOAD_FAILED;

    free(impl->slots);
    free(impl);
    registry->impl = NULL;
    return SALTS_PLUGIN_OK;
}
