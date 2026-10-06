#include "idl_contract.h"
#include "tinytest.h"

#include <string.h>

static size_t balanced_map_text(char *output, unsigned levels) {
  size_t offset;
  if (levels == 0u) {
    memcpy(output, "int32", sizeof("int32") - 1u);
    return sizeof("int32") - 1u;
  }
  memcpy(output, "map<", sizeof("map<") - 1u);
  offset = sizeof("map<") - 1u;
  offset += balanced_map_text(output + offset, levels - 1u);
  output[offset++] = ',';
  offset += balanced_map_text(output + offset, levels - 1u);
  output[offset++] = '>';
  return offset;
}

spec("typed IDL contract snapshot") {
  it("enforces exact byte and node budgets without changing the previous view") {
    enum { BALANCED_MAP_LEVELS = 6u };
    char text[IDL_TYPE_REF_MAX_BYTES + 1u];
    IdlTypeRef type = {0};
    size_t length;
    const char *previous;
    memset(text, 'x', sizeof(text));
    check_true(idl_type_ref_parse(text, IDL_TYPE_REF_MAX_BYTES, &type));
    previous = type.name;
    check_false(idl_type_ref_parse(text, sizeof(text), &type));
    check_true(type.name == previous);
    /* Six binary levels contain 2^7 - 1 = 127 nodes. A unary wrapper
     * reaches 128; one more wrapper must fail before publishing a view. */
    memcpy(text, "list<", sizeof("list<") - 1u);
    length = sizeof("list<") - 1u;
    length += balanced_map_text(text + length, BALANCED_MAP_LEVELS);
    text[length++] = '>';
    check_true(idl_type_ref_parse(text, length, &type));
    memmove(text + sizeof("list<") - 1u, text, length);
    memcpy(text, "list<", sizeof("list<") - 1u);
    length += sizeof("list<") - 1u;
    text[length++] = '>';
    check_false(idl_type_ref_parse(text, length, &type));
    check_equal(type.collection_kind, IDL_COLLECTION_LIST);
  }

  it("inspects borrowed recursive syntax and rejects invalid expressions atomically") {
    static const char expression[] = "list<map<string,User>>";
    static const char *const invalid[] = {
        "", "list<>", "list<int32,string>", "map<string>",
        "map<string,list<int32>", "Result<int32,string>", "list< int32>",
        "list<int32>suffix", "map<string,,int32>"};
    IdlTypeRef type = {0};
    IdlTypeRef inner = {0};
    size_t index;
    check_true(idl_type_ref_parse(expression, sizeof(expression) - 1u, &type));
    check_equal(type.collection_kind, IDL_COLLECTION_LIST);
    check_equal(type.argument_count, (size_t)1u);
    check_true(type.arguments[0] == expression + sizeof("list<") - 1u);
    check_true(idl_type_ref_parse(
        type.arguments[0], type.argument_lengths[0], &inner));
    check_equal(inner.collection_kind, IDL_COLLECTION_MAP);
    check_equal(inner.argument_count, (size_t)2u);
    check_equal(inner.argument_lengths[0], sizeof("string") - 1u);
    check_equal(inner.argument_lengths[1], sizeof("User") - 1u);
    for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
      const char *original_name = type.name;
      check_false(idl_type_ref_parse(invalid[index], strlen(invalid[index]), &type));
      check_true(type.name == original_name);
      check_equal(type.collection_kind, IDL_COLLECTION_LIST);
    }
  }

  it("bounds recursive type depth before publishing a borrowed view") {
    char text[(sizeof("list<>") - 1u) * IDL_TYPE_REF_MAX_DEPTH + sizeof("int32")];
    IdlTypeRef type = {0};
    size_t offset = 0u;
    size_t index;
    for (index = 0u; index < IDL_TYPE_REF_MAX_DEPTH - 1u; ++index) {
      memcpy(text + offset, "list<", sizeof("list<") - 1u);
      offset += sizeof("list<") - 1u;
    }
    memcpy(text + offset, "int32", sizeof("int32") - 1u);
    offset += sizeof("int32") - 1u;
    for (index = 0u; index < IDL_TYPE_REF_MAX_DEPTH - 1u; ++index)
      text[offset++] = '>';
    check_true(idl_type_ref_parse(text, offset, &type));
    memmove(text + sizeof("list<") - 1u, text, offset);
    memcpy(text, "list<", sizeof("list<") - 1u);
    offset += sizeof("list<") - 1u;
    text[offset++] = '>';
    check_false(idl_type_ref_parse(text, offset, &type));
    check_equal(type.collection_kind, IDL_COLLECTION_LIST);
  }

  it("preserves recursive collection arguments in the immutable Contract") {
    static const char source[] =
        "schema Nested;"
        "composite User { int32 id; string name; }"
        "message Batch {"
        " list<list<int32>> matrix;"
        " list<map<string,User>> records;"
        " map<string,list<User>> groups;"
        "}";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    const IdlDataDecl *batch;

    check_true(idl_contract_parse(
        source, sizeof(source) - 1u, &contract, &diagnostic));
    if (contract == NULL) return;
    batch = idl_contract_find_data(contract, "Batch");
    check_not_null(batch);
    if (batch != NULL && batch->field_count == 3u) {
      check_equal(batch->fields[0].collection_kind, IDL_COLLECTION_LIST);
      check_equal(batch->fields[0].inner_type, "list<int32>");
      check_equal(batch->fields[1].collection_kind, IDL_COLLECTION_LIST);
      check_equal(batch->fields[1].inner_type, "map<string,User>");
      check_equal(batch->fields[2].collection_kind, IDL_COLLECTION_MAP);
      check_equal(batch->fields[2].key_type, "string");
      check_equal(batch->fields[2].value_type, "list<User>");
    }
    check_equal(batch != NULL ? batch->field_count : 0u, (size_t)3u);
    idl_contract_destroy(contract);
  }

  it("preserves nested map keys without splitting their argument comma") {
    static const char source[] =
        "schema Types; message Value { map<map<string,int32>,list<int32>> items; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    const IdlDataDecl *value;
    check_true(idl_contract_parse(source, sizeof(source) - 1u,
                                  &contract, &diagnostic));
    if (contract == NULL) return;
    value = idl_contract_find_data(contract, "Value");
    check_not_null(value);
    if (value != NULL && value->field_count == 1u) {
      check_equal(value->fields[0].key_type, "map<string,int32>");
      check_equal(value->fields[0].value_type, "list<int32>");
    }
    idl_contract_destroy(contract);
  }

  it("rejects unknown root constructors and incorrect generic arity") {
    static const char *const sources[] = {
        "schema Bad; message Value { map<int32> items; }",
        "schema Bad; message Value { list<int32,int32> items; }",
        "schema Bad; message Value { Result<int32,int32> items; }"};
    size_t index;
    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      IdlContract *contract = NULL;
      IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
      check_false(idl_contract_parse(sources[index], strlen(sources[index]),
                                     &contract, &diagnostic));
      check_null(contract);
      check_true(diagnostic.status != IDL_OK);
    }
  }

  it("owns Data Service Channel and Component semantics without Node exposure") {
    static const char source[] =
        "schema Demo [version(4), db_init(sqlite, \"INSERT INTO demo VALUES (1)\")];"
        "message Request { optional uint32 id; }"
        "message Reply { string name; }"
        "message Event { uint64 seq; }"
        "service Echo { Call: Request -> Reply throws Reply; }"
        "channel Updates: Event;"
        "component Api { service Echo; channel Updates; }";
    IdlContract *contract = NULL;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    const IdlDataDecl *request;
    const IdlService *service;
    const IdlChannel *channel;
    const IdlComponent *component;

    check_true(idl_contract_parse(
        source, sizeof(source) - 1u, &contract, &diagnostic));
    check_not_null(contract);
    if (contract == NULL) return;

    check_equal(contract->abi_version, IDL_CONTRACT_ABI_VERSION);
    check_equal(strcmp(contract->name, "Demo"), 0);
    check_equal(strcmp(contract->version, "4"), 0);
    check_equal(idl_annotation_count(
                    contract->annotations, contract->annotation_count,
                    "db_init"),
                (size_t)1u);
    {
      const IdlAnnotation *annotation = idl_annotation_find(
          contract->annotations, contract->annotation_count, "db_init", 0u);
      check_not_null(annotation);
      if (annotation != NULL) {
        check_false(annotation->bare);
        check_equal(annotation->argument_count, (size_t)2u);
        check_equal(strcmp(idl_annotation_argument(annotation, 0u), "sqlite"), 0);
        check_equal(
            strcmp(idl_annotation_argument(annotation, 1u),
                   "INSERT INTO demo VALUES (1)"),
            0);
        check_equal(strcmp(annotation->value, "sqlite"), 0);
      }
    }

    request = idl_contract_find_data(contract, "Request");
    check_not_null(request);
    if (request != NULL) {
      check_true(request->kind == IDL_DATA_MESSAGE);
      check_equal(request->field_count, (size_t)1u);
      check_equal(strcmp(request->fields[0].name, "id"), 0);
      check_true(request->fields[0].optional);
      check_false(request->fields[0].nullable);
    }

    service = idl_contract_find_service(contract, "Echo");
    check_not_null(service);
    if (service != NULL) {
      check_equal(service->operation_count, (size_t)1u);
      check_equal(strcmp(service->operations[0].name, "Call"), 0);
      check_equal(strcmp(service->operations[0].request_type, "Request"), 0);
      check_equal(strcmp(service->operations[0].response_type, "Reply"), 0);
      check_equal(service->operations[0].error_count, (size_t)1u);
      check_equal(strcmp(service->operations[0].error_types[0], "Reply"), 0);
    }

    channel = idl_contract_find_channel(contract, "Updates");
    check_not_null(channel);
    if (channel != NULL) {
      check_equal(strcmp(channel->message_type, "Event"), 0);
      check_equal(strcmp(channel->qualified_name, "Demo.Updates"), 0);
      check_true(
          idl_contract_find_channel(contract, "Demo.Updates") == channel);
    }

    component = idl_contract_find_component(contract, "Api");
    check_not_null(component);
    if (component != NULL) {
      check_equal(strcmp(component->qualified_name, "Demo.Api"), 0);
      check_true(
          idl_contract_find_component(contract, "Demo.Api") == component);
      check_equal(component->capability_count, (size_t)2u);
      check_true(component->capabilities[0].kind == IDL_CAPABILITY_SERVICE);
      check_true(component->capabilities[1].kind == IDL_CAPABILITY_CHANNEL);
      check_equal(
          strcmp(component->capabilities[0].qualified_name, "Demo.Echo"), 0);
      check_equal(
          strcmp(component->capabilities[1].qualified_name, "Demo.Updates"), 0);
    }

    idl_contract_destroy(contract);
  }
}
