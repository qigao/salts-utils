#include "data_bind.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

spec("DataBind immutable ValidationPlan") {
  it("reflects canonical Constraint IR and reuses one plan for JSON and XML") {
    static const char schema[] =
        "message Profile {"
        " @Min(1) @Max(120) uint64 age;"
        " @Size(min = 2, max = 8) @Pattern(\"^[a-z]+$\") string name;"
        " optional @Size(max = 8) string alias;"
        "}";
    static const char json[] =
        "{\"age\":42,\"name\":\"alice\",\"alias\":\"ali\"}";
    static const char xml[] =
        "<Profile><age>42</age><name>alice</name><alias>ali</alias></Profile>";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *from_json = NULL;
    DataBindValue *from_xml = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindSchemaConstraint constraint = DATA_BIND_SCHEMA_CONSTRAINT_INIT;

    check_equal(data_bind_create_from_text(
                    schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    check_equal(
        data_bind_schema_field_constraint_count(codec, "Profile", 0u), 2u);
    check(data_bind_schema_field_constraint_at(
              codec, "Profile", 0u, 0u, &constraint) == 1);
    check_equal(constraint.kind, DATA_BIND_CONSTRAINT_MIN);
    check_equal(constraint.field_name, "age");
    check_equal(constraint.numeric_value, "1");
    check_null(constraint.pattern);

    constraint =
        (DataBindSchemaConstraint)DATA_BIND_SCHEMA_CONSTRAINT_INIT;
    check(data_bind_schema_field_constraint_at(
              codec, "Profile", 1u, 0u, &constraint) == 1);
    check_equal(constraint.kind, DATA_BIND_CONSTRAINT_SIZE);
    check_equal(constraint.has_size_min, 1);
    check_equal(constraint.size_min, 2u);
    check_equal(constraint.has_size_max, 1);
    check_equal(constraint.size_max, 8u);

    constraint =
        (DataBindSchemaConstraint)DATA_BIND_SCHEMA_CONSTRAINT_INIT;
    check(data_bind_schema_field_constraint_at(
              codec, "Profile", 2u, 0u, &constraint) == 1);
    check_equal(constraint.kind, DATA_BIND_CONSTRAINT_SIZE);
    check_equal(constraint.has_size_min, 0);
    check_equal(constraint.has_size_max, 1);
    check_equal(constraint.size_max, 8u);

    check_equal(data_bind_validation_plan_compile(
                    codec, "Profile", &plan, &error),
                DATA_BIND_OK);
    check_not_null(plan);
    check_equal(data_bind_validation_plan_rule_count(plan), 5u);

    check_equal(data_bind_parse_json(
                    codec, "Profile", json, sizeof(json) - 1u,
                    &from_json, &error),
                DATA_BIND_OK);
    check_equal(data_bind_parse_xml(
                    codec, "Profile", xml, sizeof(xml) - 1u,
                    &from_xml, &error),
                DATA_BIND_OK);
    check_not_null(from_json);
    check_not_null(from_xml);

    /* Execution retains no codec/schema/Constraint-IR dependency. */
    data_bind_free(codec);
    codec = NULL;

    check_equal(
        data_bind_validation_plan_validate(plan, from_json, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, from_xml, &error),
        DATA_BIND_OK);

    data_bind_value_free(from_json);
    data_bind_value_free(from_xml);
    data_bind_validation_plan_free(plan);
  }

  it("keeps validation failures distinct and leaves decoded input unchanged") {
    static const char schema[] =
        "message Profile {"
        " @Min(1) @Max(120) uint64 age;"
        " @Size(min = 2, max = 8) @Pattern(\"^[a-z]+$\") string name;"
        "}";
    static const char too_old[] =
        "{\"age\":121,\"name\":\"alice\"}";
    static const char bad_name[] =
        "{\"age\":42,\"name\":\"Alice\"}";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *value = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_create_from_text(
                    schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_equal(data_bind_validation_plan_compile(
                    codec, "Profile", &plan, &error),
                DATA_BIND_OK);

    check_equal(data_bind_parse_json(
                    codec, "Profile", too_old, sizeof(too_old) - 1u,
                    &value, &error),
                DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_ERR_VALIDATION);
    check_equal(data_bind_status_name(error.code), "validation");
    check_equal(error.path, "Profile.age");
    {
      uint64_t age = 0u;
      check_equal(data_bind_value_get_uint64(
                      data_bind_value_get(value, "age"), &age),
                  DATA_BIND_OK);
      check_equal(age, UINT64_C(121));
    }
    data_bind_value_free(value);
    value = NULL;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(data_bind_parse_json(
                    codec, "Profile", bad_name, sizeof(bad_name) - 1u,
                    &value, &error),
                DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_ERR_VALIDATION);
    check_equal(error.path, "Profile.name");
    check_equal(
        data_bind_value_as_string(data_bind_value_get(value, "name")),
        "Alice");

    data_bind_value_free(value);
    data_bind_validation_plan_free(plan);
    data_bind_free(codec);
  }

  it("compiles regex once and fails atomically for an invalid pattern") {
    static const char schema[] =
        "message Invalid { @Pattern(\"[\") string value; }";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(data_bind_create_from_text(
                    schema, strlen(schema), &codec, &error),
                DATA_BIND_OK);
    check_equal(data_bind_validation_plan_compile(
                    codec, "Invalid", &plan, &error),
                DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_equal(error.path, "Invalid.value");

    data_bind_free(codec);
  }
}
