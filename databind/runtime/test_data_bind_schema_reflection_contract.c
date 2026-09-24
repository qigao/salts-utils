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

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error), DATA_BIND_OK);
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

  it("rejects nullable runtime schemas until exact lowering is implemented") {
    static const char schema[] =
        "message Nullable { nullable string name; }";
    DataBind *codec = (DataBind *)(uintptr_t)1;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_ERR_SCHEMA);
    check_null(codec);
    check_equal(error.code, DATA_BIND_ERR_SCHEMA);
    check_equal(error.path, "Nullable.name");
    check_not_null(strstr(error.message, "nullable"));
  }

  it("reflects minimal service contracts and transport projections") {
    static const char schema[] =
        "message GetUserRequest {"
        "  [path] uint64 id;"
        "  optional [query] string expand default \"summary\";"
        "  [header(\"Authorization\")] string authorization;"
        "}"
        "message GetUserResponse { string name; }"
        "message CreateUserRequest { [body] string name; }"
        "message CreateUserResponse { uint64 id; }"
        "message PingRequest { uint64 nonce; }"
        "message PingResponse { uint64 nonce; }"
        "message NotFoundError { string resource; }"
        "message service { uint64 throws; }"
        "service UserService {"
        "  [GET(\"/users/{id}\"), rpc]"
        "  GetUser: GetUserRequest -> GetUserResponse throws NotFoundError;"
        "  [POST(\"/users\")]"
        "  CreateUser: CreateUserRequest -> CreateUserResponse;"
        "  [rpc(\"legacy.ping\")]"
        "  Ping: PingRequest -> PingResponse;"
        "}"
        "service KeywordService {"
        "  [rpc] Echo: service -> service;"
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
    check_equal(operation.request_type, "GetUserRequest");
    check_equal(operation.response_type, "GetUserResponse");
    check_equal(operation.error_count, 1u);
    check_equal(data_bind_service_operation_error_at(
                    codec, "UserService", "GetUser", 0u),
                "NotFoundError");
    check_true(operation.has_http);
    check_equal(operation.http_method, "GET");
    check_equal(operation.http_path, "/users/{id}");
    check_true(operation.has_rpc);
    check_equal(operation.rpc_name, "UserService.GetUser");

    operation = (DataBindServiceOperation)DATA_BIND_SERVICE_OPERATION_INIT;
    check(data_bind_service_operation_find(codec, "UserService", "CreateUser",
                                           &operation) == 1);
    check_true(operation.has_http);
    check_false(operation.has_rpc);
    check_equal(operation.http_method, "POST");
    check_equal(operation.http_path, "/users");

    operation = (DataBindServiceOperation)DATA_BIND_SERVICE_OPERATION_INIT;
    check(data_bind_service_operation_find(codec, "UserService", "Ping",
                                           &operation) == 1);
    check_false(operation.has_http);
    check_true(operation.has_rpc);
    check_equal(operation.rpc_name, "legacy.ping");

    operation = (DataBindServiceOperation)DATA_BIND_SERVICE_OPERATION_INIT;
    check(data_bind_service_operation_find(codec, "KeywordService", "Echo",
                                           &operation) == 1);
    check_equal(operation.request_type, "service");
    check_equal(operation.response_type, "service");
    check_equal(operation.rpc_name, "KeywordService.Echo");

    check(data_bind_schema_field_at(codec, "service", 0u, &field) == 1);
    check_equal(field.name, "throws");

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "GetUserRequest", 0u, &field) == 1);
    check_equal(field.binding_kind, "path");
    check_equal(field.binding_name, "id");

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "GetUserRequest", 1u, &field) == 1);
    check_equal(field.binding_kind, "query");
    check_equal(field.binding_name, "expand");

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "GetUserRequest", 2u, &field) == 1);
    check_equal(field.binding_kind, "header");
    check_equal(field.binding_name, "Authorization");

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "CreateUserRequest", 0u, &field) == 1);
    check_equal(field.binding_kind, "body");
    check_equal(field.binding_name, "name");

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

  it("rejects invalid service contracts without publishing partial codecs") {
    static const char valid_schema[] =
        "message Req { [path] uint64 id; }"
        "message Res { uint64 id; }"
        "service Stable { [GET(\"/stable/{id}\")] Read: Req -> Res; }";
    static const char invalid_route[] =
        "message Req { [path] uint64 id; }"
        "message Res { uint64 id; }"
        "service Bad { [GET(\"stable/{id}\")] Read: Req -> Res; }";
    static const char unknown_type[] =
        "message Req { uint64 id; }"
        "service Bad { [rpc] Read: Req -> Missing; }";
    static const char duplicate_rpc[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service A { [rpc(\"same.call\")] One: Req -> Res; }"
        "service B { [rpc(\"same.call\")] Two: Req -> Res; }";
    static const char duplicate_service[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service Same { One: Req -> Res; }"
        "service Same { Two: Req -> Res; }";
    static const char duplicate_operation[] =
        "message Req { uint64 id; }"
        "message Res { uint64 id; }"
        "service Same { One: Req -> Res; One: Req -> Res; }";
    static const char path_mismatch[] =
        "message Req { [path] uint64 other; }"
        "message Res { uint64 id; }"
        "service Bad { [GET(\"/bad/{id}\")] Read: Req -> Res; }";
    static const char conflicting_binding[] =
        "message Req { [path, query] uint64 id; }"
        "message Res { uint64 id; }"
        "service Bad { [GET(\"/bad/{id}\")] Read: Req -> Res; }";
    static const char rpc_conflicting_binding[] =
        "message Req { [query, header] uint64 id; }"
        "message Res { uint64 id; }"
        "service Bad { [rpc] Read: Req -> Res; }";
    DataBind *stable = NULL;
    DataBind *invalid = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_create_from_text(valid_schema, strlen(valid_schema),
                                           &stable, &error),
                DATA_BIND_OK);
    check_not_null(stable);
    check_equal(data_bind_service_count(stable), 1u);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(invalid_route, strlen(invalid_route),
                                           &invalid, &error),
                DATA_BIND_ERR_PARSE);
    check_null(invalid);
    check_equal(data_bind_service_count(stable), 1u);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(unknown_type, strlen(unknown_type),
                                           &invalid, &error),
                DATA_BIND_ERR_PARSE);
    check_null(invalid);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(duplicate_rpc, strlen(duplicate_rpc),
                                           &invalid, &error),
                DATA_BIND_ERR_PARSE);
    check_null(invalid);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(duplicate_service,
                                           strlen(duplicate_service),
                                           &invalid, &error),
                DATA_BIND_ERR_PARSE);
    check_null(invalid);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(duplicate_operation,
                                           strlen(duplicate_operation),
                                           &invalid, &error),
                DATA_BIND_ERR_PARSE);
    check_null(invalid);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(path_mismatch,
                                           strlen(path_mismatch),
                                           &invalid, &error),
                DATA_BIND_ERR_PARSE);
    check_null(invalid);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(conflicting_binding,
                                           strlen(conflicting_binding),
                                           &invalid, &error),
                DATA_BIND_ERR_PARSE);
    check_null(invalid);

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_create_from_text(rpc_conflicting_binding,
                                           strlen(rpc_conflicting_binding),
                                           &invalid, &error),
                DATA_BIND_ERR_PARSE);
    check_null(invalid);

    data_bind_free(stable);
  }

}
