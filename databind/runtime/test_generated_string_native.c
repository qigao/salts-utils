#include "generated_string_native.h"
#include "data_bind_native.h"
#include "tinytest.h"

#include <stdlib.h>

spec("generated string canonical native lifecycle") {
  it("publishes tstr through canonical CMeta without selecting Binary ownership") {
    const cmeta_data_desc *native = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    NativeStringRecord_t value = {0};
    size_t workspace_bytes = 0u;
    void *workspace = NULL;

    check_equal(NativeStringRecord_cmeta_data(&native, &error), DATA_BIND_OK);
    check_not_null(native);
    options.max_depth = 8u;
    options.max_items = 32u;
    options.max_owned_bytes = 1024u;
    check_equal(data_bind_native_probe_workspace_size(options.max_depth, &workspace_bytes),
                DATA_BIND_OK);
    workspace = malloc(workspace_bytes);
    check_not_null(workspace);

    if (native != NULL && workspace != NULL) {
      options.workspace = workspace;
      options.workspace_bytes = workspace_bytes;
      check_equal(data_bind_native_init(&options, native, &value, sizeof(value), &diagnostic),
                  DATA_BIND_OK);
      check_null(value.text);

      value.id = 7u;
      value.text = tstr_dup("canonical-native");
      check_not_null(value.text);
      if (value.text != NULL) {
        check_equal(data_bind_native_clear(&options, native, &value, sizeof(value), &diagnostic),
                    DATA_BIND_OK);
        check_equal(value.id, 0u);
        check_null(value.text);
      }
    }

    free(workspace);
  }
}
