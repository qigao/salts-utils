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

  it("reflects normalized validation constraints without executing them") {
    static const char schema[] =
        "message User {"
        " @Min(-5) @Max(150) int32 age;"
        " @Size(min = 1, max = 100) string name;"
        " optional @Pattern(\"^[^@]+@[^@]+$\") string email;"
        " uint32 unconstrained;"
        "}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaConstraint reflected = DATA_BIND_SCHEMA_CONSTRAINT_INIT;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    check_equal(data_bind_schema_field_constraint_count(codec, "User", 0u),
                (size_t)2u);
    check_equal(data_bind_schema_field_constraint_count(codec, "User", 1u),
                (size_t)1u);
    check_equal(data_bind_schema_field_constraint_count(codec, "User", 2u),
                (size_t)1u);
    check_equal(data_bind_schema_field_constraint_count(codec, "User", 3u),
                (size_t)0u);

    check(data_bind_schema_field_constraint_at(codec, "User", 0u, 0u,
                                               &reflected) == 1);
    check_equal(reflected.kind, DATA_BIND_SCHEMA_CONSTRAINT_MIN);
    check_equal(reflected.kind_name, "min");
    check_equal(reflected.value, "-5");
    check_false(reflected.has_min);
    check_false(reflected.has_max);
    check_null(reflected.pattern);
    check_equal(data_bind_schema_constraint_kind_name(reflected.kind), "min");

    reflected = (DataBindSchemaConstraint)DATA_BIND_SCHEMA_CONSTRAINT_INIT;
    check(data_bind_schema_field_constraint_at(codec, "User", 0u, 1u,
                                               &reflected) == 1);
    check_equal(reflected.kind, DATA_BIND_SCHEMA_CONSTRAINT_MAX);
    check_equal(reflected.kind_name, "max");
    check_equal(reflected.value, "150");

    reflected = (DataBindSchemaConstraint)DATA_BIND_SCHEMA_CONSTRAINT_INIT;
    check(data_bind_schema_field_constraint_at(codec, "User", 1u, 0u,
                                               &reflected) == 1);
    check_equal(reflected.kind, DATA_BIND_SCHEMA_CONSTRAINT_SIZE);
    check_equal(reflected.kind_name, "size");
    check_true(reflected.has_min);
    check_equal(reflected.min_size, (size_t)1u);
    check_true(reflected.has_max);
    check_equal(reflected.max_size, (size_t)100u);
    check_null(reflected.value);
    check_null(reflected.pattern);

    reflected = (DataBindSchemaConstraint)DATA_BIND_SCHEMA_CONSTRAINT_INIT;
    check(data_bind_schema_field_constraint_at(codec, "User", 2u, 0u,
                                               &reflected) == 1);
    check_equal(reflected.kind, DATA_BIND_SCHEMA_CONSTRAINT_PATTERN);
    check_equal(reflected.kind_name, "pattern");
    check_equal(reflected.pattern, "^[^@]+@[^@]+$");
    check_null(reflected.value);

    check_equal(
        data_bind_schema_constraint_kind_name(DATA_BIND_SCHEMA_CONSTRAINT_UNKNOWN),
        "unknown");
    check_equal(
        data_bind_schema_constraint_kind_name((DataBindSchemaConstraintKind)999),
        "unknown");

    data_bind_free(codec);
  }

  it("keeps constraint reflection size-prefixed and clears invalid queries") {
    static const char schema[] =
        "message User { @Size(max = 32) string name; }";
    struct {
      DataBindSchemaConstraint value;
      unsigned char guard[16];
    } compact;
    static const unsigned char expected_guard[16] = {
        0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
        0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    size_t prefix = offsetof(DataBindSchemaConstraint, pattern);

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    memset(&compact, 0xa5, sizeof(compact));
    compact.value.size = prefix;
    check(data_bind_schema_field_constraint_at(codec, "User", 0u, 0u,
                                               &compact.value) == 1);
    check_equal(compact.value.size, prefix);
    check_equal(compact.value.kind, DATA_BIND_SCHEMA_CONSTRAINT_SIZE);
    check_true(compact.value.has_max);
    check_equal(compact.value.max_size, (size_t)32u);
    check_equal(compact.guard, expected_guard, sizeof(expected_guard));

    compact.value =
        (DataBindSchemaConstraint)DATA_BIND_SCHEMA_CONSTRAINT_INIT;
    compact.value.kind = DATA_BIND_SCHEMA_CONSTRAINT_PATTERN;
    compact.value.pattern = "stale";
    check(data_bind_schema_field_constraint_at(codec, "User", 0u, 9u,
                                               &compact.value) == 0);
    check_equal(compact.value.size, sizeof(DataBindSchemaConstraint));
    check_equal(compact.value.kind, DATA_BIND_SCHEMA_CONSTRAINT_UNKNOWN);
    check_null(compact.value.pattern);

    check_equal(data_bind_schema_field_constraint_count(codec, "Missing", 0u),
                (size_t)0u);
    check_equal(data_bind_schema_field_constraint_count(codec, "User", 9u),
                (size_t)0u);

    data_bind_free(codec);
  }

  it("reflects nullable independently and preserves JSON absent-null-value states") {
    static const char schema[] =
        "message User {"
        " string required_value;"
        " optional string optional_value;"
        " nullable string nullable_value;"
        " optional nullable string tri_state_value;"
        " optional nullable string locale default \"en\";"
        "}";
    static const char explicit_null[] =
        "{\"required_value\":\"r\",\"nullable_value\":null,"
        "\"tri_state_value\":null,\"locale\":null}";
    static const char absent_with_default[] =
        "{\"required_value\":\"r\",\"nullable_value\":\"n\"}";
    static const char illegal_null[] =
        "{\"required_value\":null,\"nullable_value\":\"n\"}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
    DataBindValue *value = NULL;
    const DataBindValue *field_value;
    const char *text = NULL;
    size_t text_len = 0u;

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    check(data_bind_schema_field_at(codec, "User", 0u, &field) == 1);
    check_equal(field.is_optional, 0);
    check_equal(field.is_nullable, 0);

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "User", 1u, &field) == 1);
    check_equal(field.is_optional, 1);
    check_equal(field.is_nullable, 0);

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "User", 2u, &field) == 1);
    check_equal(field.is_optional, 0);
    check_equal(field.is_nullable, 1);

    field = (DataBindSchemaField)DATA_BIND_SCHEMA_FIELD_INIT;
    check(data_bind_schema_field_at(codec, "User", 3u, &field) == 1);
    check_equal(field.is_optional, 1);
    check_equal(field.is_nullable, 1);

    check_equal(data_bind_parse_json(codec, "User", explicit_null,
                                     sizeof(explicit_null) - 1u, &value, &error),
                DATA_BIND_OK);
    check_not_null(value);
    if (value != NULL) {
      check_null(data_bind_value_get(value, "optional_value"));
      field_value = data_bind_value_get(value, "nullable_value");
      check_not_null(field_value);
      if (field_value != NULL)
        check_equal(data_bind_value_kind(field_value), DATA_BIND_VALUE_NULL);
      field_value = data_bind_value_get(value, "tri_state_value");
      check_not_null(field_value);
      if (field_value != NULL)
        check_equal(data_bind_value_kind(field_value), DATA_BIND_VALUE_NULL);
      field_value = data_bind_value_get(value, "locale");
      check_not_null(field_value);
      if (field_value != NULL)
        check_equal(data_bind_value_kind(field_value), DATA_BIND_VALUE_NULL);
      data_bind_value_free(value);
      value = NULL;
    }

    check_equal(data_bind_parse_json(codec, "User", absent_with_default,
                                     sizeof(absent_with_default) - 1u,
                                     &value, &error),
                DATA_BIND_OK);
    check_not_null(value);
    if (value != NULL) {
      check_null(data_bind_value_get(value, "optional_value"));
      check_null(data_bind_value_get(value, "tri_state_value"));
      field_value = data_bind_value_get(value, "locale");
      check_not_null(field_value);
      if (field_value != NULL) {
        check_equal(data_bind_value_kind(field_value), DATA_BIND_VALUE_STRING);
        check_equal(data_bind_value_get_string(field_value, &text, &text_len),
                    DATA_BIND_OK);
        check_equal(text_len, 2u);
        check(memcmp(text, "en", 2u) == 0);
      }
      data_bind_value_free(value);
      value = NULL;
    }

    check_equal(data_bind_parse_json(codec, "User", illegal_null,
                                     sizeof(illegal_null) - 1u, &value, &error),
                DATA_BIND_ERR_TYPE_MISMATCH);
    check_null(value);

    data_bind_free(codec);
  }

  it("keeps appended nullable reflection outside older size-prefixed callers") {
    static const char schema[] =
        "message User { nullable string name; }";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaField compact;
    unsigned char before[sizeof(int)];
    size_t prefix = offsetof(DataBindSchemaField, is_nullable);

    check_equal(data_bind_create_from_text(schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    memset(&compact, 0xa5, sizeof(compact));
    compact.size = prefix;
    memcpy(before, (const unsigned char *)&compact + prefix, sizeof(before));
    check(data_bind_schema_field_at(codec, "User", 0u, &compact) == 1);
    check_equal(compact.size, prefix);
    check(memcmp(before, (const unsigned char *)&compact + prefix,
                 sizeof(before)) == 0);

    data_bind_free(codec);
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
