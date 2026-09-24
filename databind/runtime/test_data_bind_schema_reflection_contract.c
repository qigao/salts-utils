#include "data_bind.h"
#include "tinytest.h"

#include <stddef.h>
#include <string.h>

spec("DataBind schema reflection contract") {
  it("keeps structural reflection distinct from schema overlay metadata") {
    static const char schema[] =
        "schema Market [id(1), version(2), byte_order(little)];"
        "enum Side <uint8> { Buy = 1; Sell = 2; }"
        "message Order { uint32 id; optional uint32 venue default 7; Side side; }";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaType type = DATA_BIND_SCHEMA_TYPE_INIT;
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindSchemaEnumItem item = DATA_BIND_SCHEMA_ENUM_ITEM_INIT;
    DataBindSchemaAttribute attr = DATA_BIND_SCHEMA_ATTRIBUTE_INIT;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);

    check_equal(data_bind_schema_name(codec), "Market");
    check_equal(data_bind_schema_attribute_count(codec), 3u);
    check(data_bind_schema_attribute_at(codec, 0u, &attr) == 1);
    check_equal(attr.name, "id");
    check_equal(attr.value, "1");

    check(data_bind_schema_find_type(codec, "Order", &type) == 1);
    check_equal(type.name, "Order");
    check_equal(type.kind, DATA_BIND_SCHEMA_MESSAGE);
    check_equal(type.field_count, 3u);

    check(data_bind_schema_field_at(codec, "Order", 0u, &field) == 1);
    check_equal(field.name, "id");
    check_equal(field.type, "uint32");
    check_equal(field.is_optional, 0);
    check_equal(field.has_default, 0);

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "Order", 1u, &field) == 1);
    check_equal(field.name, "venue");
    check_equal(field.type, "uint32");
    check_equal(field.is_optional, 1);
    check_equal(field.has_default, 1);
    check_equal(field.default_value, "7");

    type = (DataBindSchemaType)DATA_BIND_SCHEMA_TYPE_INIT;
    check(data_bind_schema_find_type(codec, "Side", &type) == 1);
    check_equal(type.kind, DATA_BIND_SCHEMA_ENUM);
    check_equal(type.underlying_type, "uint8");
    check_equal(type.item_count, 2u);

    check(data_bind_schema_enum_item_at(codec, "Side", 0u, &item) == 1);
    check_equal(item.name, "Buy");
    check_equal(item.value, "1");

    data_bind_free(codec);
  }

  it("reflects transport-neutral Service contracts") {
    static const char schema[] =
        "message GetUserRequest {"
        "  uint64 id;"
        "  optional string expand default \"summary\";"
        "  string authorization;"
        "}"
        "message GetUserResponse { string name; }"
        "message CreateUserRequest { string name; }"
        "message CreateUserResponse { uint64 id; }"
        "message PingRequest { uint64 nonce; }"
        "message PingResponse { uint64 nonce; }"
        "message NotFoundError { string resource; }"
        "message service { uint64 throws; }"
        "service UserService {"
        "  GetUser: GetUserRequest -> GetUserResponse throws NotFoundError;"
        "  CreateUser: CreateUserRequest -> CreateUserResponse;"
        "  Ping: PingRequest -> PingResponse;"
        "}"
        "service KeywordService {"
        "  Echo: service -> service;"
        "}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindService service = DATA_BIND_SERVICE_INIT;
    DataBindServiceOperation operation = DATA_BIND_SERVICE_OPERATION_INIT;
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);

    check_equal(data_bind_service_count(codec), 2u);
    check(data_bind_service_find(codec, "UserService", &service) == 1);
    check_equal(service.name, "UserService");
    check_equal(service.operation_count, 3u);

    check(data_bind_service_operation_find(codec, "UserService", "GetUser",
                                           &operation) == 1);
    check_equal(operation.service_name, "UserService");
    check_equal(operation.name, "GetUser");
    check_equal(operation.request_type, "GetUserRequest");
    check_equal(operation.response_type, "GetUserResponse");
    check_equal(operation.error_count, 1u);
    check_equal(data_bind_service_operation_error_at(
                    codec, "UserService", "GetUser", 0u),
                "NotFoundError");

    operation = (DataBindServiceOperation)DATA_BIND_SERVICE_OPERATION_INIT;
    check(data_bind_service_operation_find(codec, "KeywordService", "Echo",
                                           &operation) == 1);
    check_equal(operation.request_type, "service");
    check_equal(operation.response_type, "service");

    check(data_bind_schema_field_at(codec, "service", 0u, &field) == 1);
    check_equal(field.name, "throws");

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "GetUserRequest", 0u, &field) == 1);
    check_equal(field.name, "id");

    service = (DataBindService)DATA_BIND_SERVICE_INIT;
    service.size = offsetof(DataBindService, operation_count);
    check(data_bind_service_find(codec, "UserService", &service) == 1);
    check_equal(service.size, offsetof(DataBindService, operation_count));
    check_equal(service.name, "UserService");

    operation = (DataBindServiceOperation)DATA_BIND_SERVICE_OPERATION_INIT;
    operation.size = offsetof(DataBindServiceOperation, error_count);
    check(data_bind_service_operation_find(codec, "UserService", "GetUser",
                                           &operation) == 1);
    check_equal(operation.size,
                offsetof(DataBindServiceOperation, error_count));
    check_equal(operation.service_name, "UserService");
    check_equal(operation.name, "GetUser");
    check_equal(operation.request_type, "GetUserRequest");
    check_equal(operation.response_type, "GetUserResponse");

    data_bind_free(codec);
  }

  it("rejects invalid transport-neutral Service semantics") {
    static const char valid_schema[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service Stable { Read: Req -> Res; }";
    static const char unknown_request[] =
        "message Res { uint64 id; }"
        "service Bad { Read: Missing -> Res; }";
    static const char unknown_response[] =
        "message Req { uint64 id; }"
        "service Bad { Read: Req -> Missing; }";
    static const char unknown_error[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service Bad { Read: Req -> Res throws Missing; }";
    static const char repeated_error[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "message Err { uint64 code; }"
        "service Bad { Read: Req -> Res throws Err, Err; }";
    static const char duplicate_service[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service Same { One: Req -> Res; }"
        "service Same { Two: Req -> Res; }";
    static const char duplicate_operation[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service Same { One: Req -> Res; One: Req -> Res; }";
    const char *invalid_schemas[] = {
        unknown_request, unknown_response, unknown_error, repeated_error,
        duplicate_service, duplicate_operation};
    DataBind *stable = NULL;
    DataBind *invalid = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t i;

    check_equal(data_bind_create_from_text(valid_schema, strlen(valid_schema),
                                           &stable, &error),
                DATA_BIND_OK);
    check_not_null(stable);
    check_equal(data_bind_service_count(stable), 1u);

    for (i = 0u; i < sizeof(invalid_schemas) / sizeof(invalid_schemas[0]); ++i) {
      error = (DataBindError)DATA_BIND_ERROR_INIT;
      check_equal(data_bind_create_from_text(
                      invalid_schemas[i], strlen(invalid_schemas[i]),
                      &invalid, &error),
                  DATA_BIND_ERR_PARSE);
      check_null(invalid);
      check_equal(data_bind_service_count(stable), 1u);
    }

    data_bind_free(stable);
  }

  it("rejects transport projection annotations in canonical Service IDL") {
    static const char operation_http[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service Bad { [GET(\"/bad\")] Read: Req -> Res; }";
    static const char operation_rpc[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service Bad { [rpc] Read: Req -> Res; }";
    static const char field_path[] =
        "message Req { [path] uint64 id; }"
        "message Res { uint64 id; }"
        "service Bad { Read: Req -> Res; }";
    static const char field_header[] =
        "message Req { [header(\"X-ID\")] uint64 id; }"
        "message Res { uint64 id; }"
        "service Bad { Read: Req -> Res; }";
    const char *invalid_schemas[] = {
        operation_http, operation_rpc, field_path, field_header};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t i;

    for (i = 0u; i < sizeof(invalid_schemas) / sizeof(invalid_schemas[0]); ++i) {
      error = (DataBindError)DATA_BIND_ERROR_INIT;
      check_equal(data_bind_create_from_text(
                      invalid_schemas[i], strlen(invalid_schemas[i]),
                      &codec, &error),
                  DATA_BIND_ERR_PARSE);
      check_null(codec);
      check(strstr(error.message, "Transport") != NULL);
    }
  }
}
