#define TINYTEST_NO_MAIN
#include "tinytest.h"
#include "schema_projection.h"
#include "native_service_projection.h"
#include "plugin_projection.h"
#include "wasm_projection.h"
#include "cmeta_fs.h"
#include "json_parser.h"
#include "fmt.h"
#include <string.h>

static IdlContract *contract;
static databind_compiler_projection_config config;
static cmeta_fs_buf_t source;
static tstr schema;
static char *serialized;
static json_value_t *round_trip;
static char error[256];

static int parse(const char *text) {
  IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
  idl_contract_destroy(contract);
  contract = NULL;
  return idl_contract_parse(text, strlen(text), &contract, &diagnostic) ? 0 : -1;
}

static int lower(unsigned transports) {
  databind_compiler_projection_config_dispose(&config);
  return databind_compiler_schema_projection_build(
      contract, transports, &config, error, sizeof(error));
}

spec("Schema application projection annotations") {
  it("rejects legacy spellings and misplaced short annotations") {
    check_equal(parse("message User { uint32 id; } "
        "[app_inject(\"repo\", \"Repo\", \"repo.h\")] service Users { "
        "[http(\"POST\", \"/users\")] Get: User -> User; }"), IDL_OK);
    check_false(databind_compiler_native_injection_valid(idl_contract_find_service(contract, "Users")));
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
    check_equal(parse("message User { uint32 id; } service Users { "
        "[http(\"POST\", \"/users\"), app_http(\"POST\", \"/old\")] Get: User -> User; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
    check_equal(parse("[inject(\"repo\", \"Repo\", \"repo.h\")] message User { uint32 id; } "
        "service Users { [app_rpc(\"get\")] Get: User -> User; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_RPC), 0);
    check_equal(parse("message User { uint32 id; } service Users { "
        "[http(\"POST\"), app_rpc(\"get\")] Get: User -> User; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_RPC), 0);
  }

  it("rejects injected services in publication ABIs without a native receiver") {
    check_equal(parse("schema Injection [version(1)]; component Host { service Api; } "
        "message User { uint32 id; } [inject(\"repo\", \"Repo\", \"repo.h\")] "
        "service Api { Get: User -> User; }"), IDL_OK);
    check_not_null(idl_contract_find_component(contract, "Injection.Host"));
    databind_compiler_projection_input input = {.contract = contract};
    databind_compiler_native_service_config native = {"di-record.h", "di-native.h", 0};
    databind_compiler_projection_request request = {
        {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT, DATABIND_COMPILER_ARTIFACT_NATIVE}, "di-native.c", &native};
    check_not_equal(databind_compiler_native_service_generate(&input, &request, NULL), 0);
    check_not_equal(databind_compiler_native_service_render_staged(&input, &request, "di-native.h.stage", "di-native.c.stage"), 0);
    databind_compiler_plugin_config plugin = {.plugin_version_major = 1u,
        .component_id = "Injection.Host", .native_header = "di-record.h", .service_header_output = "di-native.h",
        .client_header_output = "di-client.h", .client_source_output = "di-client.c", .binary_presentation = 1};
    request.id.kind = DATABIND_COMPILER_ARTIFACT_PLUGIN; request.config = &plugin;
    check_not_equal(databind_compiler_plugin_generate(&input, &request, NULL), 0);
    databind_compiler_wasm_config wasm = {.component_id = "Injection.Host", .native_header = "di-record.h",
        .core_module_path = WASM_CORE_FIXTURE_FILE, .host_header_output = "di-native.h",
        .host_source_output = "di-client.c", .guest_header_output = "di-client.h",
        .symbol_prefix = "di", .binary_presentation = 1};
    request.id.kind = DATABIND_COMPILER_ARTIFACT_WASM; request.config = &wasm;
    check_not_equal(databind_compiler_wasm_generate(&input, &request, NULL), 0);
    check_not_equal(cmeta_fs_access("di-native.c", SALTS_FS_ACCESS_EXISTS), 0);
    check_not_equal(cmeta_fs_access("di-native.h", SALTS_FS_ACCESS_EXISTS), 0);
  }

  it("rejects injected typed-error signatures before publication") {
    check_equal(parse("message User { uint32 id; } message Failure { uint32 code; } "
        "[inject(\"repo\", \"Repo\", \"repo.h\")] service Api { "
        "[http(\"POST\", \"/users\")] Get: User -> User throws Failure; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
  }

  it("preserves service-level injection metadata without changing the route JSON") {
    check_equal(parse("message User { uint32 id; } "
        "[inject(\"repo\", \"UserRepository\", \"repo.h\"), inject(\"clock\", \"Clock\", \"clock.h\")] "
        "service Users { [http(\"POST\", \"/users\")] Get: User -> User; }"), IDL_OK);
    const IdlService *service = idl_contract_find_service(contract, "Users");
    check_not_null(service);
    check_equal(idl_annotation_count(service->annotations, service->annotation_count, "inject"), (size_t)2u);
    const IdlAnnotation *annotation = idl_annotation_find(service->annotations, service->annotation_count, "inject", 1u);
    check_equal(annotation->arguments[1], "Clock");
    check_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
    check_equal(config.http.operation_count, (size_t)1u);
  }

  it("rejects malformed, duplicated or misplaced dependency declarations") {
    static const char *annotations[] = {
      "inject", "inject(\"repo\", \"Repo\")",
      "inject(\"repo-name\", \"Repo\", \"repo.h\")",
      "inject(\"repo\", \"Repo*\", \"repo.h\")",
      "inject(\"repo\", \"Repo\", \"\")",
      "inject(\"repo\", \"Repo\", \"bad header.h\")",
      "inject(\"repo\", \"Repo\", \"repo.h\"), inject(\"repo\", \"Clock\", \"clock.h\")",
      "inject(\"repo\", \"Repo\", \"repo.h\"), inject(\"other\", \"Repo\", \"repo.h\")",
      "http(\"POST\", \"/users\")"
    };
    for (size_t i = 0u; i < sizeof(annotations) / sizeof(annotations[0]); ++i) {
      tstr_free(schema);
      schema = tstr_format("message User {{ uint32 id; }} [{}] service Users {{ "
          "[http(\"POST\", \"/users\")] Get: User -> User; }}", annotations[i]);
      check_not_null(schema);
      check_equal(parse(schema), IDL_OK);
      check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
    }
    check_equal(parse("message User { uint32 id; } service Users { "
        "[inject(\"repo\", \"Repo\", \"repo.h\"), http(\"POST\", \"/users\")] Get: User -> User; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
  }

  it("bounds service requirements at sixteen") {
    schema = tstr_from_v(vstr_from_cstr("inject(\"d0\", \"I0\", \"interfaces.h\")"));
    check_not_null(schema);
    for (size_t i = 1u; i < 17u; ++i) {
      tstr next = tstr_format("{}, inject(\"d{}\", \"I{}\", \"interfaces.h\")", schema, i, i);
      check_not_null(next);
      tstr_free(schema); schema = next;
      if (i < 15u) continue;
      tstr input = tstr_format("message User {{ uint32 id; }} [{}] service Users {{ "
          "[http(\"POST\", \"/users\")] Get: User -> User; }}", schema);
      check_not_null(input);
      int parsed = parse(input);
      tstr_free(input);
      check_equal(parsed, IDL_OK);
      if (i == 15u) check_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
      else check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
    }
  }

  after_each() {
    databind_compiler_projection_config_dispose(&config);
    idl_contract_destroy(contract);
    contract = NULL;
    cmeta_fs_buf_free(&source);
    tstr_free(schema);
    schema = NULL;
    json_serialize_free(serialized);
    serialized = NULL;
    json_free(round_trip);
    round_trip = NULL;
    if (cmeta_fs_access(SCHEMA_PROJECTION_OUTPUT, SALTS_FS_ACCESS_EXISTS) == 0)
      (void)cmeta_fs_unlink(SCHEMA_PROJECTION_OUTPUT);
  }

  it("owns JSON configuration after the parsed multi-service contract is destroyed") {
    check_equal(cmeta_fs_read_file(SCHEMA_PROJECTION_INPUT, &source), 0);
    check_equal(parse(source.base), IDL_OK);
    check_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP | DATABIND_SCHEMA_PROJECTION_RPC), 0);
    idl_contract_destroy(contract);
    contract = NULL;
    check_equal(config.http.operation_count, (size_t)3);
    check_equal(config.rpc.operation_count, (size_t)3);
    check_equal(config.http_operations[0].service_name, "Users");
    check_equal(config.http_operations[0].route, "/users/{id}");
    check_equal(config.http_fields[0].schema_field, "id");
    check_equal(config.http_operations[1].success_status, 201);
    check_equal(config.http_operations[1].egress_format, DATA_BIND_FORMAT_XML);
    check_equal(config.rpc_operations[2].wire_method, "system.health");
  }

  it("exports valid JSON consumed by the existing external configuration reader") {
    check_equal(databind_compiler_schema_projection_export(
        SCHEMA_PROJECTION_INPUT, SCHEMA_PROJECTION_OUTPUT, "rpc,http", error, sizeof(error)), 0);
    check_equal(databind_compiler_projection_config_load(
        SCHEMA_PROJECTION_OUTPUT, &config, error, sizeof(error)), 0);
    check_equal(config.http.operation_count, (size_t)3);
    check_equal(config.rpc.operation_count, (size_t)3);
    check_equal(config.http_operations[1].egress_format, DATA_BIND_FORMAT_XML);
    databind_compiler_projection_config_dispose(&config);
    /* A failed subsequent export must preserve the previously published file. */
    check_not_equal(databind_compiler_schema_projection_export(
        SCHEMA_PROJECTION_INPUT, SCHEMA_PROJECTION_OUTPUT, "socket", error, sizeof(error)), 0);
    check_not_equal(databind_compiler_schema_projection_export(
        SCHEMA_PROJECTION_INVALID, SCHEMA_PROJECTION_OUTPUT, "http,rpc", error, sizeof(error)), 0);
    check_equal(databind_compiler_projection_config_load(
        SCHEMA_PROJECTION_OUTPUT, &config, error, sizeof(error)), 0);
    check_equal(config.rpc_operations[0].wire_method, "users.get");
  }

  it("round trips escaped annotation strings through JSON serialization") {
    check_equal(parse("message User { uint32 id; } "
        "service Users { [app_rpc(\"users.\\method\tname\")] Get: User -> User; }"), IDL_OK);
    check_equal(lower(DATABIND_SCHEMA_PROJECTION_RPC), 0);
    size_t length = 0;
    serialized = json_serialize_pretty(config.json_root, &length);
    check_not_null(serialized);
    round_trip = json_parse(serialized, length);
    check_not_null(round_trip);
    json_value_t *operations = json_object_get(json_object_get(round_trip, "rpc"), "operations");
    const char *method = json_string(json_object_get(json_array_get(operations, 0), "wire_method"));
    check_equal(method, config.rpc_operations[0].wire_method);
  }

  it("retains repeated field annotations for both directions") {
    check_equal(parse("message User { uint32 id; } service Users { "
        "[http(\"GET\", \"/users/{id}\"), "
        "app_http_field(\"ingress\", \"id\", \"path\", \"id\"), "
        "app_http_field(\"egress\", \"id\", \"response_body\", \"user_id\")] "
        "Get: User -> User; }"), IDL_OK);
    check_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
    check_equal(config.http.field_count, (size_t)2);
    check_equal(config.http_fields[1].wire_name, "user_id");
    check_equal(config.http_fields[1].direction, DATABIND_COMPILER_PROJECTION_EGRESS);
  }

  it("supports selecting either transport without emitting the other") {
    check_equal(cmeta_fs_read_file(SCHEMA_PROJECTION_INPUT, &source), 0);
    check_equal(parse(source.base), IDL_OK);
    check_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
    check_true(config.has_http);
    check_false(config.has_rpc);
    check_equal(lower(DATABIND_SCHEMA_PROJECTION_RPC), 0);
    check_true(config.has_rpc);
    check_false(config.has_http);
  }

  it("rejects missing or malformed mappings instead of guessing public routes") {
    static const char *annotations[] = {
      "", "http", "http(\"GET\")",
      "app_htp(\"GET\", \"/users\")",
      "http(\"GET\", \"/users\"), app_policy(\"auth\")",
      "http(\"GET\", \"/users\"), app_http_policy",
      "http(\"GET\", \"/users\"), app_http_policy(\"\")",
      "http(\"GET\", \"/users\"), app_http_policy(\"a b\")",
      "http(\"GET\", \"/users\"), app_http_policy(\"auth\", \"extra\")",
      "http(\"GET\", \"/users\"), app_http_policy(\"auth\"), app_http_policy(\"auth\")",
      "http(\"GET\", \"/users\"), http(\"POST\", \"/users\")",
      "http(\"INVALID\", \"/users\")",
      "http(\"GET\", \"users\")",
      "http(\"GET\", \"/users/{id}\")",
      "http(\"GET\", \"/users\"), app_http_formats(\"yaml\", \"json\")",
      "http(\"GET\", \"/users\"), app_http_status(600)",
      "http(\"GET\", \"/users\"), app_http_status(20)",
      "http(\"GET\", \"/users\"), app_http_field(\"ingress\", \"typo\", \"query\", \"id\")"
    };
    for (size_t i = 0; i < sizeof(annotations) / sizeof(annotations[0]); ++i) {
      tstr_free(schema);
      schema = tstr_format(
          "message Request {{ uint32 id; }} service Users {{ {}{}{} Get: Request -> Request; }}",
          i != 0 ? "[" : "", annotations[i], i != 0 ? "]" : "");
      check_not_null(schema);
      check_equal(parse(schema), IDL_OK);
      check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
      check_null(config.json_root);
      check_not_equal(error[0], '\0');
    }
  }

  it("requires every selected RPC operation to have an explicit name") {
    check_equal(parse("message User { uint32 id; } "
        "service Users { [http(\"GET\", \"/users\")] Get: User -> User; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP | DATABIND_SCHEMA_PROJECTION_RPC), 0);
  }

  it("admits sixteen unique policies and rejects the seventeenth") {
    schema = tstr_from_v(vstr_from_cstr("http(\"POST\", \"/echo\")"));
    check_not_null(schema);
    for (size_t i = 0u; i < 17u; ++i) {
      tstr next = tstr_format("{}, app_http_policy(\"p{}\")", schema, i);
      check_not_null(next);
      tstr_free(schema); schema = next;
      if (i < 15u) continue;
      tstr input = tstr_format("message Request {{ uint32 id; }} service Api {{ [{}] Echo: Request -> Request; }}", schema);
      check_not_null(input);
      int parsed = parse(input);
      tstr_free(input);
      check_equal(parsed, IDL_OK);
      if (i == 15u) check_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
      else check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
    }
  }

  it("rejects annotations on records instead of silently ignoring them") {
    check_equal(parse("[http(\"GET\", \"/users\")] message User { uint32 id; } "
        "service Users { [app_rpc(\"get\")] Get: User -> User; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_RPC), 0);
  }

  it("rejects duplicate RPC names across services") {
    check_equal(parse("message User { uint32 id; } "
        "service Users { [app_rpc(\"get\")] Get: User -> User; } "
        "service System { [app_rpc(\"get\")] Get: User -> User; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_RPC), 0);
  }

  it("rejects equivalent parameterized routes across services") {
    check_equal(parse("message Request { uint32 id; } "
        "service Users { [http(\"GET\", \"/users/{id}\"), "
        "app_http_field(\"ingress\", \"id\", \"path\", \"id\")] Get: Request -> Request; } "
        "service System { [http(\"GET\", \"/users/{key}\"), "
        "app_http_field(\"ingress\", \"id\", \"path\", \"key\")] Get: Request -> Request; }"), IDL_OK);
    check_not_equal(lower(DATABIND_SCHEMA_PROJECTION_HTTP), 0);
  }
}
