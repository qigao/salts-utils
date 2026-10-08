#include "projection.h"

#include "compiler_core.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

#ifndef SCHEMA_EXAMPLE_FILE
#error "SCHEMA_EXAMPLE_FILE is required"
#endif

typedef struct projection_probe {
  const IdlContract *seen_contract;
  databind_compiler_projection_id seen_id;
  size_t calls;
  int fail;
} projection_probe;

static int probe_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  projection_probe *probe = (projection_probe *)context;
  if (probe == NULL || input == NULL || input->contract == NULL ||
      request == NULL)
    return -1;
  probe->seen_contract = input->contract;
  probe->seen_id = request->id;
  ++probe->calls;
  return probe->fail ? -1 : 0;
}

static int file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return 0;
  fclose(file);
  return 1;
}

static int file_matches(const char *path, const char *expected) {
  char buffer[128];
  size_t length;
  FILE *file = fopen(path, "rb");
  if (file == NULL || expected == NULL) {
    if (file != NULL) fclose(file);
    return 0;
  }
  length = fread(buffer, 1u, sizeof(buffer) - 1u, file);
  if (ferror(file) || !feof(file)) {
    fclose(file);
    return 0;
  }
  buffer[length] = '\0';
  fclose(file);
  return strcmp(buffer, expected) == 0;
}

static int write_sentinel(const char *path, const char *sentinel) {
  size_t size = strlen(sentinel);
  FILE *file = fopen(path, "wb");
  if (file == NULL) return 0;
  if (fwrite(sentinel, 1u, size, file) != size) {
    fclose(file);
    return 0;
  }
  return fclose(file) == 0;
}

#define ARTIFACT_ID(kind_) \
  { DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT, (uint32_t)(kind_) }
#define TRANSPORT_ID(kind_) \
  { DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT, (uint32_t)(kind_) }

spec("DataBind compiler typed generation registry") {
describe("typed selection identity") {
  it("parses artifact and transport namespaces independently") {
    databind_compiler_artifact_kind artifact = 0;
    databind_compiler_transport_kind transport = 0;

    check_equal(databind_compiler_artifact_parse("native", &artifact), 0);
    check_equal(artifact, DATABIND_COMPILER_ARTIFACT_NATIVE);
    check_equal(strcmp(databind_compiler_artifact_name(artifact), "native"), 0);

    check_equal(databind_compiler_artifact_parse("plugin", &artifact), 0);
    check_equal(artifact, DATABIND_COMPILER_ARTIFACT_PLUGIN);
    check_equal(strcmp(databind_compiler_artifact_name(artifact), "plugin"), 0);

    check_equal(databind_compiler_artifact_parse("wasm", &artifact), 0);
    check_equal(artifact, DATABIND_COMPILER_ARTIFACT_WASM);

    check_equal(databind_compiler_transport_parse("http", &transport), 0);
    check_equal(transport, DATABIND_COMPILER_TRANSPORT_HTTP);
    check_equal(strcmp(databind_compiler_transport_name(transport), "http"), 0);

    check_equal(databind_compiler_transport_parse("rpc", &transport), 0);
    check_equal(transport, DATABIND_COMPILER_TRANSPORT_RPC);

    check_equal(databind_compiler_artifact_parse("http", &artifact), -1);
    check_equal(databind_compiler_transport_parse("plugin", &transport), -1);
    check_equal(databind_compiler_artifact_parse("PLUGIN", &artifact), -1);
    check_equal(databind_compiler_transport_parse("HTTP", &transport), -1);
    check_null(databind_compiler_artifact_name(
        (databind_compiler_artifact_kind)999));
    check_null(databind_compiler_transport_name(
        (databind_compiler_transport_kind)999));
  }

  it("rejects duplicate selection only within the same typed ID") {
    const databind_compiler_projection_request duplicate[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "one", NULL},
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "two", NULL},
    };
    const databind_compiler_projection_request cross_axis[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "artifact", NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "transport", NULL},
    };

    check_false(databind_compiler_projection_requests_valid(
        duplicate, sizeof(duplicate) / sizeof(duplicate[0])));
    check_true(databind_compiler_projection_requests_valid(
        cross_axis, sizeof(cross_axis) / sizeof(cross_axis[0])));
  }
}

describe("pre-render selection admission") {
  it("rejects missing, misspelled and duplicated generators without callbacks") {
    projection_probe plugin = {0};
    projection_probe http = {0};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), NULL, NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), NULL, NULL},
    };
    const databind_compiler_projection_request duplicate[] = {
        requests[0], requests[0],
    };
    const databind_compiler_projection_backend missing[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         probe_generate, &plugin},
    };
    const databind_compiler_projection_backend misspelled[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         probe_generate, &plugin},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "HTTP",
         probe_generate, &http},
    };
    const databind_compiler_projection_backend complete[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         probe_generate, &plugin},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         probe_generate, &http},
    };
    const databind_compiler_projection_backend duplicate_backends[] = {
        complete[0], complete[0],
    };
    check_false(databind_compiler_projection_selection_valid(
        requests, 2u, missing, 1u));
    check_false(databind_compiler_projection_selection_valid(
        requests, 2u, misspelled, 2u));
    check_false(databind_compiler_projection_selection_valid(
        requests, 2u, duplicate_backends, 2u));
    check_false(databind_compiler_projection_selection_valid(
        duplicate, 2u, complete, 2u));
    check_false(databind_compiler_projection_selection_valid(
        requests, 2u, complete, 0u));
    check_true(databind_compiler_projection_selection_valid(
        requests, 2u, complete, 2u));
    check_true(databind_compiler_projection_selection_valid(
        NULL, 0u, NULL, 0u));
    check_equal(plugin.calls, (size_t)0u);
    check_equal(http.calls, (size_t)0u);
  }
}

describe("shared canonical IR") {
  it("admits every typed backend before the first callback") {
    IdlContract contract = {sizeof(IdlContract), IDL_CONTRACT_ABI_VERSION};
    databind_compiler_projection_input input = {
        .contract = &contract};
    projection_probe plugin = {0};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), NULL, NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), NULL, NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         probe_generate, &plugin},
    };

    check_equal(databind_compiler_projection_run(
                    &input,
                    requests, sizeof(requests) / sizeof(requests[0]),
                    backends, sizeof(backends) / sizeof(backends[0])),
                -1);
    check_equal(plugin.calls, (size_t)0u);
  }

  it("stops after one typed generator fails") {
    IdlContract contract = {sizeof(IdlContract), IDL_CONTRACT_ABI_VERSION};
    databind_compiler_projection_input input = {
        .contract = &contract};
    projection_probe plugin = {0};
    projection_probe http = {0};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), NULL, NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), NULL, NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         probe_generate, &plugin},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         probe_generate, &http},
    };

    plugin.fail = 1;
    check_equal(databind_compiler_projection_run(
                    &input,
                    requests, sizeof(requests) / sizeof(requests[0]),
                    backends, sizeof(backends) / sizeof(backends[0])),
                -1);
    check_equal(plugin.calls, (size_t)1u);
    check_equal(http.calls, (size_t)0u);
  }
}

describe("compiler integration") {
  it("dispatches typed axes through the one parse pipeline") {
    static const char output[] = "databind_projection_registry_output.h";
    projection_probe plugin = {0};
    projection_probe http = {0};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN),
         "image.plugin.c", NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP),
         "image.http.h", NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         probe_generate, &plugin},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         probe_generate, &http},
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_EXAMPLE_FILE,
        .output_path = output,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = requests,
        .projection_count = sizeof(requests) / sizeof(requests[0]),
        .projection_backends = backends,
        .projection_backend_count = sizeof(backends) / sizeof(backends[0]),
    };

    (void)remove(output);
    check_equal(tbe_compiler_run(&options), 0);
    check_equal(plugin.calls, (size_t)1u);
    check_equal(http.calls, (size_t)1u);
    check_not_null(plugin.seen_contract);
    check_true(plugin.seen_contract == http.seen_contract);
    check_true(file_exists(output));
    (void)remove(output);
  }

  it("rejects an incomplete typed set before rendering caller outputs") {
    static const char output[] = "databind_projection_registry_rejected.h";
    static const char source[] = "databind_projection_registry_rejected.c";
    static const char sentinel[] = "existing-c-header-not-replaced";
    projection_probe plugin = {0};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), NULL, NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), NULL, NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         probe_generate, &plugin},
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_EXAMPLE_FILE,
        .output_path = output,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = requests,
        .projection_count = sizeof(requests) / sizeof(requests[0]),
        .projection_backends = backends,
        .projection_backend_count = sizeof(backends) / sizeof(backends[0]),
    };

    (void)remove(output);
    (void)remove(source);
    /* A previously valid header must remain unchanged, while the companion
     * source must not be created. No backend may be invoked. */
    options.source_output_path = source;
    check_true(write_sentinel(output, sentinel));
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(plugin.calls, (size_t)0u);
    check_true(file_matches(output, sentinel));
    check_false(file_exists(source));
    (void)remove(output);
    /* Equally reject before creating a completely new output. */
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(plugin.calls, (size_t)0u);
    check_false(file_exists(output));
    check_false(file_exists(source));
  }

  it("prevents malformed backend registry from replacing non-C source output") {
    static const char output[] = "databind_projection_source_rejected.ts";
    static const char sentinel[] = "previous-ts-declaration";
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), NULL, NULL},
    };
    projection_probe plugin = {0};
    databind_compiler_projection_backend backends[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "Plugin",
         probe_generate, &plugin},
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_EXAMPLE_FILE,
        .output_path = output,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_TS,
        .projection_requests = requests,
        .projection_count = 1u,
        .projection_backends = backends,
        .projection_backend_count = 1u,
    };
    (void)remove(output);
    check_true(write_sentinel(output, sentinel));
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(plugin.calls, (size_t)0u);
    check_true(file_matches(output, sentinel));
    (void)remove(output);
  }
}

}
