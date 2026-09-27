#include "idl_contract.h"
#include "tinytest.h"

#include <string.h>

spec("typed IDL contract snapshot") {
  it("owns Data Service Channel and Component semantics without Node exposure") {
    static const char source[] =
        "schema Demo [version(4)];"
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
    }

    component = idl_contract_find_component(contract, "Api");
    check_not_null(component);
    if (component != NULL) {
      check_equal(strcmp(component->qualified_name, "Demo.Api"), 0);
      check_equal(component->capability_count, (size_t)2u);
      check_true(component->capabilities[0].kind == IDL_CAPABILITY_SERVICE);
      check_true(component->capabilities[1].kind == IDL_CAPABILITY_CHANNEL);
    }

    idl_contract_destroy(contract);
  }
}
