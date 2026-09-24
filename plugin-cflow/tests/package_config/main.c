#include <salts/plugin_cflow.h>

#include <stddef.h>

static bool sample_open(void *self, cflow_publisher *out) {
    (void)self;
    (void)out;
    return false;
}

CMETA_IMPLEMENTS(
    salts_plugin_cflow_publisher_provider,
    installed_provider,
    0u,
    .open = sample_open);

int main(void) {
    int state = 0;
    salts_plugin_cflow_publisher_provider provider =
        installed_provider_as_salts_plugin_cflow_publisher_provider(&state);
    salts_plugin_cflow_publisher_handle handle = {0};

    return salts_plugin_cflow_publisher_provider_valid(&provider) &&
                   !salts_plugin_cflow_publisher_handle_valid(&handle)
               ? 0
               : 1;
}
