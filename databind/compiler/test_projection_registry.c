#include "projection.h"

#include "compiler_core.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

#ifndef SCHEMA_EXAMPLE_FILE
#error "SCHEMA_EXAMPLE_FILE is required"
#endif

typedef struct projection_probe {
  const Node *seen_root;
  databind_compiler_projection_kind seen_kind;
  size_t calls;
  int fail;
} projection_probe;

static int probe_generate(
    const Node *canonical_ir,
    const databind_compiler_projection_request *request,
    void *context) {
  projection_probe *probe = (projection_probe *)context;
  if (probe == NULL || request == NULL) return -1;
  probe->seen_root = canonical_ir;
  probe->seen_kind = request->kind;
  ++probe->calls;
  return probe->fail ? -1 : 0;
}

static int file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return 0;
  fclose(file);
  return 1;
}

spec("DataBind compiler projection registry") {
describe("projection identity") {
  it("parses only canonical backend names") {
    databind_compiler_projection_kind kind = 0;

    check_equal(databind_compiler_projection_parse(
                    "native", &kind), 0);
    check_equal(kind, DATABIND_COMPILER_PROJECTION_NATIVE);
    check_equal(strcmp(databind_compiler_projection_name(kind), "native"), 0);

    check_equal(databind_compiler_projection_parse(
                    "plugin", &kind), 0);
    check_equal(kind, DATABIND_COMPILER_PROJECTION_PLUGIN);
    check_equal(strcmp(databind_compiler_projection_name(kind), "plugin"), 0);

    check_equal(databind_compiler_projection_parse(
                    "wasm", &kind), 0);
    check_equal(kind, DATABIND_COMPILER_PROJECTION_WASM);

    check_equal(databind_compiler_projection_parse(
                    "PLUGIN", &kind), -1);
    check_equal(databind_compiler_projection_parse(
                    "unknown", &kind), -1);
    check_null(databind_compiler_projection_name(
        (databind_compiler_projection_kind)999));
  }

  it("rejects duplicate projection selection before generation") {
    const databind_compiler_projection_request duplicate[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "one", NULL},
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "two", NULL},
    };

    check_false(databind_compiler_projection_requests_valid(
        duplicate, sizeof(duplicate) / sizeof(duplicate[0])));
  }
}

describe("shared canonical IR") {
  it("runs multiple selected backends against one parsed root") {
    Node *root = NULL;
    char *schema_data = NULL;
    projection_probe plugin = {0};
    projection_probe wasm = {0};
    const databind_compiler_projection_request requests[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "image.plugin.c", NULL},
        {DATABIND_COMPILER_PROJECTION_WASM, "image.wasm", NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "plugin",
         probe_generate, &plugin},
        {DATABIND_COMPILER_PROJECTION_WASM, "wasm",
         probe_generate, &wasm},
    };

    check_equal(tbe_compiler_parse_schema_file(
                    SCHEMA_EXAMPLE_FILE, &root, &schema_data), 0);
    check_not_null(root);
    check_not_null(schema_data);

    check_equal(databind_compiler_projection_run(
                    root,
                    requests, sizeof(requests) / sizeof(requests[0]),
                    backends, sizeof(backends) / sizeof(backends[0])),
                0);

    check_equal(plugin.calls, (size_t)1u);
    check_equal(wasm.calls, (size_t)1u);
    check_true(plugin.seen_root == root);
    check_true(wasm.seen_root == root);
    check_true(plugin.seen_root == wasm.seen_root);
    check_equal(plugin.seen_kind, DATABIND_COMPILER_PROJECTION_PLUGIN);
    check_equal(wasm.seen_kind, DATABIND_COMPILER_PROJECTION_WASM);

    node_free(root);
    free(schema_data);
  }

  it("admits every backend before the first callback") {
    Node *root = create_node_map(NULL);
    projection_probe plugin = {0};
    const databind_compiler_projection_request requests[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, NULL, NULL},
        {DATABIND_COMPILER_PROJECTION_WASM, NULL, NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "plugin",
         probe_generate, &plugin},
    };

    check_not_null(root);
    check_equal(databind_compiler_projection_run(
                    root,
                    requests, sizeof(requests) / sizeof(requests[0]),
                    backends, sizeof(backends) / sizeof(backends[0])),
                -1);
    check_equal(plugin.calls, (size_t)0u);
    node_free(root);
  }

  it("stops after a backend generation failure") {
    Node *root = create_node_map(NULL);
    projection_probe plugin = {0};
    projection_probe wasm = {0};
    const databind_compiler_projection_request requests[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, NULL, NULL},
        {DATABIND_COMPILER_PROJECTION_WASM, NULL, NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "plugin",
         probe_generate, &plugin},
        {DATABIND_COMPILER_PROJECTION_WASM, "wasm",
         probe_generate, &wasm},
    };

    check_not_null(root);
    plugin.fail = 1;
    check_equal(databind_compiler_projection_run(
                    root,
                    requests, sizeof(requests) / sizeof(requests[0]),
                    backends, sizeof(backends) / sizeof(backends[0])),
                -1);
    check_equal(plugin.calls, (size_t)1u);
    check_equal(wasm.calls, (size_t)0u);
    node_free(root);
  }
}

describe("compiler integration") {
  it("dispatches selected projections through the one parse pipeline") {
    static const char output[] = "databind_projection_registry_output.h";
    projection_probe plugin = {0};
    projection_probe wasm = {0};
    const databind_compiler_projection_request requests[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "image.plugin.c", NULL},
        {DATABIND_COMPILER_PROJECTION_WASM, "image.wasm", NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "plugin",
         probe_generate, &plugin},
        {DATABIND_COMPILER_PROJECTION_WASM, "wasm",
         probe_generate, &wasm},
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
    check_equal(wasm.calls, (size_t)1u);
    check_not_null(plugin.seen_root);
    check_true(plugin.seen_root == wasm.seen_root);
    check_true(file_exists(output));
    (void)remove(output);
  }

  it("rejects an incomplete projection set after prerequisite output without backend callbacks") {
    static const char output[] = "databind_projection_registry_rejected.h";
    projection_probe plugin = {0};
    const databind_compiler_projection_request requests[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, NULL, NULL},
        {DATABIND_COMPILER_PROJECTION_WASM, NULL, NULL},
    };
    databind_compiler_projection_backend backends[] = {
        {DATABIND_COMPILER_PROJECTION_PLUGIN, "plugin",
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
    check_equal(tbe_compiler_run(&options), 1);
    check_equal(plugin.calls, (size_t)0u);
    check_true(file_exists(output));
    (void)remove(output);
  }
}

}
