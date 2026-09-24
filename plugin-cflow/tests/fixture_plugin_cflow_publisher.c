#include <salts/plugin_cflow_provider.h>

#include <stdlib.h>
#include <string.h>

typedef struct fixture_publisher_state {
    int value;
    int emitted;
    int cancelled;
} fixture_publisher_state;

typedef struct fixture_provider_state {
    int next_value;
} fixture_provider_state;

static const char *fixture_source_name(void *self) {
    (void)self;
    return "fixture-channel-source";
}

static const cmeta_type_desc *fixture_source_output_type(void *self) {
    (void)self;
    return &cmeta_type_int;
}

static cflow_step fixture_source_resume(
    void *self,
    cflow_publish_context *ctx,
    void *out_value) {
    fixture_publisher_state *state =
        (fixture_publisher_state *)self;
    (void)ctx;

    if (state == NULL || out_value == NULL)
        return (cflow_step){
            CFLOW_STEP_ERROR, {0}, "fixture publisher argument is null"};

    if (state->cancelled || state->emitted)
        return (cflow_step){CFLOW_STEP_DONE, {0}, NULL};

    *(int *)out_value = state->value;
    state->emitted = 1;
    return (cflow_step){CFLOW_STEP_VALUE_AND_DONE, {0}, NULL};
}

static void fixture_source_cancel(void *self) {
    fixture_publisher_state *state =
        (fixture_publisher_state *)self;
    if (state != NULL) state->cancelled = 1;
}

static void fixture_source_destroy(void *self) {
    free(self);
}

static void fixture_source_bind_terminal_waker(
    void *self, cflow_waker waker) {
    (void)self;
    (void)waker;
}

static cflow_publisher_terminal fixture_source_poll_terminal(
    void *self, const char **error) {
    fixture_publisher_state *state =
        (fixture_publisher_state *)self;
    if (error != NULL) *error = NULL;
    if (state == NULL)
        return CFLOW_PUBLISHER_ERROR;
    return (state->cancelled || state->emitted)
               ? CFLOW_PUBLISHER_DONE
               : CFLOW_PUBLISHER_OPEN;
}

CMETA_IMPLEMENTS(
    cflow_publisher,
    fixture_channel_source,
    CFLOW_PUBLISHER_CAP_CONSTRUCTS_VALUES,
    .name = fixture_source_name,
    .output_type = fixture_source_output_type,
    .resume = fixture_source_resume,
    .cancel = fixture_source_cancel,
    .destroy = fixture_source_destroy,
    .bind_terminal_waker = fixture_source_bind_terminal_waker,
    .poll_terminal = fixture_source_poll_terminal);

static bool fixture_provider_open(
    void *self, cflow_publisher *out) {
    fixture_provider_state *provider =
        (fixture_provider_state *)self;
    fixture_publisher_state *state;

    if (provider == NULL || out == NULL ||
        cflow_publisher_valid(out))
        return false;

    state = (fixture_publisher_state *)calloc(1u, sizeof(*state));
    if (state == NULL) return false;

    state->value = provider->next_value++;
    *out = fixture_channel_source_as_cflow_publisher(state);
    return cflow_publisher_valid(out);
}

static bool fixture_provider_fail_open(
    void *self, cflow_publisher *out) {
    (void)self;
    if (out != NULL) memset(out, 0, sizeof(*out));
    return false;
}

CMETA_IMPLEMENTS(
    salts_plugin_cflow_publisher_provider,
    fixture_publisher_provider,
    0u,
    .open = fixture_provider_open);

CMETA_IMPLEMENTS(
    salts_plugin_cflow_publisher_provider,
    fixture_failing_provider,
    0u,
    .open = fixture_provider_fail_open);

static fixture_provider_state fixture_provider_state_value = {
    10
};

static salts_plugin_cflow_publisher_provider
    fixture_provider_value = {
        &fixture_provider_state_value,
        &fixture_publisher_provider_vtable
    };

static salts_plugin_cflow_publisher_provider
    fixture_failing_provider_value = {
        &fixture_provider_state_value,
        &fixture_failing_provider_vtable
    };

static const salts_plugin_export fixture_exports[] = {
    {
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = 0u,
        .export_id = "test.channel.source.publisher",
        .contract_id = "test.channel.source",
        .value.interface = {
            .desc = &salts_plugin_cflow_publisher_provider_interface_meta,
            .value = &fixture_provider_value,
        },
    },
    {
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = 0u,
        .export_id = "test.channel.fail.publisher",
        .contract_id = "test.channel.fail",
        .value.interface = {
            .desc = &salts_plugin_cflow_publisher_provider_interface_meta,
            .value = &fixture_failing_provider_value,
        },
    },
};

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.plugin-cflow.publisher",
    .version = {1u, 0u, 0u},
    .exports = fixture_exports,
    .export_count =
        sizeof(fixture_exports) / sizeof(fixture_exports[0]),
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION
               ? &fixture_manifest
               : NULL;
}
