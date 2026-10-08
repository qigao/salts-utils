#include "projection.h"

#include "compiler_core.h"
#include "native_service_projection.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

#ifndef SCHEMA_NATIVE_SERVICE_FILE
#error "SCHEMA_NATIVE_SERVICE_FILE is required"
#endif

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

typedef struct staged_projection_probe {
  const char *final_path;
  const char *content;
  size_t calls;
  int fail_after_write;
} staged_projection_probe;

static int staged_projection_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context) {
  staged_projection_probe *probe = (staged_projection_probe *)context;
  FILE *file;
  size_t length;
  if (input == NULL || input->contract == NULL || request == NULL ||
      request->output == NULL || probe == NULL ||
      probe->content == NULL || probe->final_path == NULL ||
      strcmp(probe->final_path, request->output) == 0)
    return -1; /* Require an actual coordinator-owned staging pathname. */
  ++probe->calls;
  length = strlen(probe->content);
  file = fopen(request->output, "wb");
  if (file == NULL) return -1;
  {
    int wrote = fwrite(probe->content, 1u, length, file) == length;
    int closed = fclose(file) == 0;
    if (!wrote || !closed) return -1;
  }
  return probe->fail_after_write ? -1 : 0;
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

  it("commits main C header with all explicitly stage-safe single-output projections") {
    static const char header[] = "projection_txn_success.h";
    static const char source[] = "projection_txn_success.c";
    static const char plugin_output[] = "projection_txn_success.plugin.c";
    static const char http_output[] = "projection_txn_success.http.h";
    staged_projection_probe plugin = {plugin_output, "published-plugin", 0u, 0};
    staged_projection_probe http = {http_output, "published-http", 0u, 0};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), plugin_output, NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), http_output, NULL},
    };
    const databind_compiler_projection_backend backends[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         staged_projection_generate, &plugin,
         DATABIND_COMPILER_OUTPUT_STAGED_SINGLE},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         staged_projection_generate, &http,
         DATABIND_COMPILER_OUTPUT_STAGED_SINGLE},
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_EXAMPLE_FILE,
        .output_path = header,
        .source_output_path = source,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = requests,
        .projection_count = 2u,
        .projection_backends = backends,
        .projection_backend_count = 2u,
    };
    (void)remove(header);
    (void)remove(source);
    (void)remove(plugin_output);
    (void)remove(http_output);
    check_true(write_sentinel(header, "old-header"));
    check_true(write_sentinel(source, "old-source"));
    check_true(write_sentinel(plugin_output, "old-plugin"));
    check_true(databind_compiler_projection_all_staged_single(
        requests, 2u, backends, 2u));
    check_equal(tbe_compiler_run(&options), 0);
    check_true(file_matches(plugin_output, "published-plugin"));
    check_true(file_matches(http_output, "published-http"));
    check_true(file_exists(header));
    check_false(file_matches(header, "old-header"));
    check_true(file_exists(source));
    check_false(file_matches(source, "old-source"));
    {
      FILE *file = fopen(source, "rb");
      char first_line[128] = {0};
      check_not_null(file);
      if (file != NULL) {
        check_not_null(fgets(first_line, sizeof(first_line), file));
        check_equal(fclose(file), 0);
        check_not_null(strstr(
            first_line, "#include \"projection_txn_success.h\""));
      }
    }
    check_equal(plugin.calls, (size_t)1u);
    check_equal(http.calls, (size_t)1u);
    (void)remove(header);
    (void)remove(source);
    (void)remove(plugin_output);
    (void)remove(http_output);
  }

  it("rolls back all selected stage-safe outputs when a later generator fails") {
    static const char header[] = "projection_txn_failure.h";
    static const char source[] = "projection_txn_failure.c";
    static const char plugin_output[] = "projection_txn_failure.plugin.c";
    static const char http_output[] = "projection_txn_failure.http.h";
    staged_projection_probe plugin = {plugin_output, "new-plugin", 0u, 0};
    staged_projection_probe http = {http_output, "partial-http", 0u, 1};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), plugin_output, NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), http_output, NULL},
    };
    const databind_compiler_projection_backend backends[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         staged_projection_generate, &plugin,
         DATABIND_COMPILER_OUTPUT_STAGED_SINGLE},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         staged_projection_generate, &http,
         DATABIND_COMPILER_OUTPUT_STAGED_SINGLE},
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_EXAMPLE_FILE,
        .output_path = header,
        .source_output_path = source,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = requests,
        .projection_count = 2u,
        .projection_backends = backends,
        .projection_backend_count = 2u,
    };
    (void)remove(header);
    (void)remove(source);
    (void)remove(plugin_output);
    (void)remove(http_output);
    check_true(write_sentinel(header, "old-header"));
    check_true(write_sentinel(source, "old-source"));
    check_true(write_sentinel(plugin_output, "old-plugin"));
    check_true(write_sentinel(http_output, "old-http"));
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(plugin.calls, (size_t)1u);
    check_equal(http.calls, (size_t)1u);
    check_true(file_matches(header, "old-header"));
    check_true(file_matches(source, "old-source"));
    check_true(file_matches(plugin_output, "old-plugin"));
    check_true(file_matches(http_output, "old-http"));

    (void)remove(header);
    (void)remove(source);
    (void)remove(plugin_output);
    (void)remove(http_output);
    /* Failed generators must also leave previously absent outputs absent. */
    check_equal(tbe_compiler_run(&options), 1);
    check_false(file_exists(header));
    check_false(file_exists(source));
    check_false(file_exists(plugin_output));
    check_false(file_exists(http_output));
    (void)remove(header);
    (void)remove(source);
    (void)remove(plugin_output);
    (void)remove(http_output);
  }

  it("rolls back native service header/source when a later staged backend fails") {
    static const char primary[] = "projection_native_txn.h";
    static const char service_source[] = "projection_native_txn.service.c";
    static const char service_header[] = "projection_native_txn.service.h";
    static const char transport_output[] = "projection_native_txn.http.h";
    databind_compiler_native_service_config native_config = {
        .native_header = primary, .header_output = service_header};
    staged_projection_probe http = {transport_output, "partial-http", 0u, 1};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_NATIVE), service_source,
         &native_config},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), transport_output,
         NULL},
    };
    const databind_compiler_projection_backend backends[] = {
        DATABIND_COMPILER_NATIVE_SERVICE_BACKEND,
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         staged_projection_generate, &http,
         DATABIND_COMPILER_OUTPUT_STAGED_SINGLE},
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_NATIVE_SERVICE_FILE,
        .output_path = primary,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = requests,
        .projection_count = 2u,
        .projection_backends = backends,
        .projection_backend_count = 2u,
    };
    (void)remove(primary);
    (void)remove(service_source);
    (void)remove(service_header);
    (void)remove(transport_output);
    check_true(write_sentinel(primary, "old-primary"));
    check_true(write_sentinel(service_source, "old-native-source"));
    check_true(write_sentinel(service_header, "old-native-header"));
    check_true(write_sentinel(transport_output, "old-http"));
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(http.calls, (size_t)1u);
    check_true(file_matches(primary, "old-primary"));
    check_true(file_matches(service_source, "old-native-source"));
    check_true(file_matches(service_header, "old-native-header"));
    check_true(file_matches(transport_output, "old-http"));

    (void)remove(primary);
    (void)remove(service_source);
    (void)remove(service_header);
    (void)remove(transport_output);
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(http.calls, (size_t)2u);
    check_false(file_exists(primary));
    check_false(file_exists(service_source));
    check_false(file_exists(service_header));
    check_false(file_exists(transport_output));

    /* The same manifest must commit all outputs when every backend succeeds. */
    http.fail_after_write = 0;
    check_equal(tbe_compiler_run(&options), 0);
    check_equal(http.calls, (size_t)3u);
    check_true(file_exists(primary));
    check_true(file_exists(service_source));
    check_true(file_exists(service_header));
    check_true(file_matches(transport_output, "partial-http"));
    (void)remove(primary);
    (void)remove(service_source);
    (void)remove(service_header);
    (void)remove(transport_output);
  }

  it("rejects native secondary collision with primary before generation") {
    static const char primary[] = "projection_native_collision.h";
    static const char source[] = "projection_native_collision.service.c";
    databind_compiler_native_service_config config = {
        .native_header = primary, .header_output = primary};
    const databind_compiler_projection_request request = {
        ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_NATIVE), source, &config};
    const databind_compiler_projection_backend backend =
        DATABIND_COMPILER_NATIVE_SERVICE_BACKEND;
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_NATIVE_SERVICE_FILE,
        .output_path = primary,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = &request,
        .projection_count = 1u,
        .projection_backends = &backend,
        .projection_backend_count = 1u,
    };
    (void)remove(primary);
    (void)remove(source);
    check_true(write_sentinel(primary, "original-primary"));
    check_equal(tbe_compiler_run(&options), 1);
    check_true(file_matches(primary, "original-primary"));
    check_false(file_exists(source));
    (void)remove(primary);
  }

  it("rejects native header collision with selected HTTP output before publication") {
    static const char primary[] = "projection_native_cross_collision.h";
    static const char service_source[] = "projection_native_cross_collision.service.c";
    static const char shared_output[] = "projection_native_cross_collision.http.h";
    databind_compiler_native_service_config config = {
        .native_header = primary, .header_output = shared_output};
    staged_projection_probe http = {shared_output, "new-http", 0u, 0};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_NATIVE), service_source,
         &config},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), shared_output,
         NULL},
    };
    const databind_compiler_projection_backend backends[] = {
        DATABIND_COMPILER_NATIVE_SERVICE_BACKEND,
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         staged_projection_generate, &http,
         DATABIND_COMPILER_OUTPUT_STAGED_SINGLE},
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_NATIVE_SERVICE_FILE,
        .output_path = primary,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = requests,
        .projection_count = 2u,
        .projection_backends = backends,
        .projection_backend_count = 2u,
    };
    (void)remove(primary);
    (void)remove(service_source);
    (void)remove(shared_output);
    check_true(write_sentinel(primary, "old-primary"));
    check_true(write_sentinel(shared_output, "old-shared"));
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(http.calls, (size_t)0u);
    check_true(file_matches(primary, "old-primary"));
    check_true(file_matches(shared_output, "old-shared"));
    check_false(file_exists(service_source));
    (void)remove(primary);
    (void)remove(service_source);
    (void)remove(shared_output);
  }

  it("rejects mixed self-publishing Native Service selections without touching output") {
    static const char primary[] = "projection_native_unsupported.h";
    static const char source[] = "projection_native_unsupported.service.c";
    static const char header[] = "projection_native_unsupported.service.h";
    static const char secondary[] = "projection_native_unsupported.http.h";
    databind_compiler_native_service_config config = {
        .native_header = primary, .header_output = header};
    staged_projection_probe http = {secondary, "unexpected", 0u, 0};
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_NATIVE), source, &config},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), secondary, NULL},
    };
    const databind_compiler_projection_backend backends[] = {
        DATABIND_COMPILER_NATIVE_SERVICE_BACKEND,
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         staged_projection_generate, &http,
         DATABIND_COMPILER_OUTPUT_SELF_PUBLISHED},
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_NATIVE_SERVICE_FILE,
        .output_path = primary,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = requests,
        .projection_count = 2u,
        .projection_backends = backends,
        .projection_backend_count = 2u,
    };
    (void)remove(primary);
    (void)remove(source);
    (void)remove(header);
    (void)remove(secondary);
    check_true(write_sentinel(primary, "original"));
    check_equal(tbe_compiler_run(&options), 1);
    check_true(file_matches(primary, "original"));
    check_false(file_exists(source));
    check_false(file_exists(header));
    check_false(file_exists(secondary));
    check_equal(http.calls, (size_t)0u);
    (void)remove(primary);
  }

  it("rejects self-publishing backend before Native Service without invoking it") {
    static const char primary[] = "projection_native_order.h";
    static const char service_source[] = "projection_native_order.service.c";
    static const char service_header[] = "projection_native_order.service.h";
    static const char transport_output[] = "projection_native_order.http.h";
    databind_compiler_native_service_config config = {
        .native_header = primary, .header_output = service_header};
    staged_projection_probe http = {transport_output, "unexpected", 0u, 0};
    const databind_compiler_projection_request requests[] = {
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), transport_output, NULL},
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_NATIVE), service_source, &config},
    };
    const databind_compiler_projection_backend backends[] = {
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         staged_projection_generate, &http,
         DATABIND_COMPILER_OUTPUT_SELF_PUBLISHED},
        DATABIND_COMPILER_NATIVE_SERVICE_BACKEND,
    };
    tbe_compiler_options_t options = {
        .schema_path = SCHEMA_NATIVE_SERVICE_FILE,
        .output_path = primary,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
        .projection_requests = requests,
        .projection_count = 2u,
        .projection_backends = backends,
        .projection_backend_count = 2u,
    };
    (void)remove(primary);
    (void)remove(service_source);
    (void)remove(service_header);
    (void)remove(transport_output);
    check_true(write_sentinel(primary, "original-primary"));
    check_equal(tbe_compiler_run(&options), 1);
    check_true(file_matches(primary, "original-primary"));
    check_false(file_exists(service_source));
    check_false(file_exists(service_header));
    check_false(file_exists(transport_output));
    check_equal(http.calls, (size_t)0u);
    (void)remove(primary);
  }

  it("rejects incomplete or dishonest selected staging capability sets") {
    const databind_compiler_projection_request requests[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "first.c", NULL},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "second.h", NULL},
    };
    staged_projection_probe plugin = {"first.c", "data", 0u, 0};
    staged_projection_probe http = {"second.h", "data", 0u, 0};
    databind_compiler_projection_backend backends[] = {
        {ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN), "plugin",
         staged_projection_generate, &plugin,
         DATABIND_COMPILER_OUTPUT_STAGED_SINGLE},
        {TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP), "http",
         staged_projection_generate, &http,
         DATABIND_COMPILER_OUTPUT_SELF_PUBLISHED},
    };
    check_false(databind_compiler_projection_all_staged_single(
        requests, 2u, backends, 2u));
    backends[1].output_policy = DATABIND_COMPILER_OUTPUT_STAGED_SINGLE;
    check_true(databind_compiler_projection_all_staged_single(
        requests, 2u, backends, 2u));
    backends[1].output_policy = (databind_compiler_output_policy)13;
    check_false(databind_compiler_projection_selection_valid(
        requests, 2u, backends, 2u));
    check_false(databind_compiler_projection_all_staged_single(
        requests, 2u, backends, 2u));
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

  it("admits source-only artifact requests without Binary layout") {
    static const char schema_path[] = "databind_projection_independent.schema";
    static const char output[] = "databind_projection_independent.ts";
    static const char schema[] =
        "message Packet { string label; uint32 sequence; }";
    projection_probe plugin = {0};
    databind_compiler_projection_request request = {
        ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN),
        "packet.plugin", NULL
    };
    databind_compiler_projection_backend backend = {
        ARTIFACT_ID(DATABIND_COMPILER_ARTIFACT_PLUGIN),
        "plugin", probe_generate, &plugin
    };
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_TS,
        .projection_requests = &request,
        .projection_count = 1u,
        .projection_backends = &backend,
        .projection_backend_count = 1u,
    };
    (void)remove(schema_path);
    (void)remove(output);
    check_true(write_sentinel(schema_path, schema));
    check_equal(tbe_compiler_run(&options), 0);
    check_equal(plugin.calls, (size_t)1u);
    check_true(file_exists(output));
    (void)remove(schema_path);
    (void)remove(output);
  }

  it("rejects an explicitly selected Binary transport before rendering source") {
    static const char schema_path[] = "databind_projection_wire_reject.schema";
    static const char output[] = "databind_projection_wire_reject.ts";
    static const char schema[] =
        "message Packet { string label; uint32 sequence; }";
    static const char sentinel[] = "existing-ts-source";
    projection_probe http = {0};
    databind_compiler_projection_request request = {
        TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP),
        "packet.http", NULL
    };
    databind_compiler_projection_backend backend = {
        TRANSPORT_ID(DATABIND_COMPILER_TRANSPORT_HTTP),
        "http", probe_generate, &http
    };
    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .output_path = output,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_TS,
        .projection_requests = &request,
        .projection_count = 1u,
        .projection_backends = &backend,
        .projection_backend_count = 1u,
    };
    (void)remove(schema_path);
    (void)remove(output);
    check_true(write_sentinel(schema_path, schema));
    check_true(write_sentinel(output, sentinel));
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(http.calls, (size_t)0u);
    check_true(file_matches(output, sentinel));
    (void)remove(output);
    check_equal(tbe_compiler_run(&options), 1);
    check_false(file_exists(output));
    check_equal(http.calls, (size_t)0u);
    (void)remove(schema_path);
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
