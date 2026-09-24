#include "data_bind_validation_plan.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

static DataBindStatus compile_plan(
    const char *schema, const char *type_name,
    DataBind **out_codec, DataBindValidationPlan **out_plan,
    DataBindError *error) {
  DataBindStatus status;
  *out_codec = NULL;
  *out_plan = NULL;
  status = data_bind_create_from_text(
      schema, strlen(schema), out_codec, error);
  if (status != DATA_BIND_OK) return status;
  status = data_bind_validation_plan_compile(
      *out_codec, type_name, out_plan, error);
  if (status != DATA_BIND_OK) {
    data_bind_free(*out_codec);
    *out_codec = NULL;
  }
  return status;
}

spec("DataBind immutable ValidationPlan") {
  it("reuses one immutable plan after JSON and XML decode and codec destruction") {
    static const char schema[] =
        "message Profile {"
        " @Min(1) @Max(120) uint64 age;"
        " @Size(min = 2, max = 8) @Pattern(\"^[a-z]+$\") string name;"
        " optional nullable @Pattern(\"^[a-z]+$\") string alias;"
        "}";
    static const char json[] =
        "{\"age\":42,\"name\":\"alice\"}";
    static const char xml[] =
        "<Profile><age>42</age><name>alice</name></Profile>";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *from_json = NULL;
    DataBindValue *from_xml = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        compile_plan(schema, "Profile", &codec, &plan, &error),
        DATA_BIND_OK);
    check_not_null(codec);
    check_not_null(plan);
    if (codec == NULL || plan == NULL) {
      data_bind_validation_plan_free(plan);
      data_bind_free(codec);
      return;
    }

    check_equal(data_bind_validation_plan_type_name(plan), "Profile");
    check_equal(data_bind_validation_plan_rule_count(plan), (size_t)5u);

    check_equal(
        data_bind_parse_json(
            codec, "Profile", json, sizeof(json) - 1u,
            &from_json, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_parse_xml(
            codec, "Profile", xml, sizeof(xml) - 1u,
            &from_xml, &error),
        DATA_BIND_OK);
    check_not_null(from_json);
    check_not_null(from_xml);

    /* Schema/reflection ownership ends here. */
    data_bind_free(codec);
    codec = NULL;

    if (from_json != NULL)
      check_equal(
          data_bind_validation_plan_validate(plan, from_json, &error),
          DATA_BIND_OK);
    if (from_xml != NULL)
      check_equal(
          data_bind_validation_plan_validate(plan, from_xml, &error),
          DATA_BIND_OK);

    data_bind_value_free(from_json);
    data_bind_value_free(from_xml);
    data_bind_validation_plan_free(plan);
  }

  it("keeps value-constraint failures distinct and leaves decoded input unchanged") {
    static const char schema[] =
        "message Profile {"
        " @Min(1) @Max(120) uint64 age;"
        " @Size(min = 2, max = 8) @Pattern(\"[a-z]+\") string name;"
        "}";
    static const char too_old[] =
        "{\"age\":121,\"name\":\"alice\"}";
    static const char too_short[] =
        "{\"age\":42,\"name\":\"a\"}";
    static const char bad_pattern[] =
        "{\"age\":42,\"name\":\"alice1\"}";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *value = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    uint64_t age = 0u;

    check_equal(
        compile_plan(schema, "Profile", &codec, &plan, &error),
        DATA_BIND_OK);
    if (codec == NULL || plan == NULL) {
      data_bind_validation_plan_free(plan);
      data_bind_free(codec);
      return;
    }

    check_equal(
        data_bind_parse_json(
            codec, "Profile", too_old, sizeof(too_old) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_ERR_VALIDATION);
    check_equal(data_bind_status_name(error.code), "validation");
    check_equal(error.path, "Profile.age");
    check_equal(
        data_bind_value_get_uint64(
            data_bind_value_get(value, "age"), &age),
        DATA_BIND_OK);
    check_equal(age, UINT64_C(121));
    data_bind_value_free(value);
    value = NULL;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_parse_json(
            codec, "Profile", too_short, sizeof(too_short) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_ERR_VALIDATION);
    check_equal(error.path, "Profile.name");
    check_not_null(strstr(error.message, "@Size"));
    data_bind_value_free(value);
    value = NULL;

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_parse_json(
            codec, "Profile", bad_pattern, sizeof(bad_pattern) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_ERR_VALIDATION);
    check_equal(error.path, "Profile.name");
    check_not_null(strstr(error.message, "@Pattern"));
    data_bind_value_free(value);

    data_bind_validation_plan_free(plan);
    data_bind_free(codec);
  }

  it("skips absent and explicit-null fields after presence normalization") {
    static const char schema[] =
        "message Profile {"
        " uint32 id;"
        " optional nullable @Size(min = 2) @Pattern(\"[a-z]+\") string alias;"
        "}";
    static const char absent[] = "{\"id\":1}";
    static const char explicit_null[] =
        "{\"id\":1,\"alias\":null}";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *value = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        compile_plan(schema, "Profile", &codec, &plan, &error),
        DATA_BIND_OK);
    if (codec == NULL || plan == NULL) {
      data_bind_validation_plan_free(plan);
      data_bind_free(codec);
      return;
    }

    check_equal(
        data_bind_parse_json(
            codec, "Profile", absent, sizeof(absent) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_OK);
    data_bind_value_free(value);
    value = NULL;

    check_equal(
        data_bind_parse_json(
            codec, "Profile", explicit_null,
            sizeof(explicit_null) - 1u, &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_OK);
    data_bind_value_free(value);

    data_bind_validation_plan_free(plan);
    data_bind_free(codec);
  }

  it("validates collection Size using the same compiled rule model") {
    static const char schema[] =
        "message Bag { @Size(max = 2) list<uint32> ids; }";
    static const char valid[] = "{\"ids\":[1,2]}";
    static const char invalid[] = "{\"ids\":[1,2,3]}";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *value = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        compile_plan(schema, "Bag", &codec, &plan, &error),
        DATA_BIND_OK);
    if (codec == NULL || plan == NULL) {
      data_bind_validation_plan_free(plan);
      data_bind_free(codec);
      return;
    }

    check_equal(
        data_bind_parse_json(
            codec, "Bag", valid, sizeof(valid) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_OK);
    data_bind_value_free(value);
    value = NULL;

    check_equal(
        data_bind_parse_json(
            codec, "Bag", invalid, sizeof(invalid) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_ERR_VALIDATION);
    check_equal(error.path, "Bag.ids");
    data_bind_value_free(value);

    data_bind_validation_plan_free(plan);
    data_bind_free(codec);
  }

  it("preserves exact uint64 comparisons above the double precision boundary") {
    static const char schema[] =
        "message Counter {"
        " @Min(18446744073709551614) uint64 value;"
        "}";
    static const char valid[] =
        "{\"value\":18446744073709551615}";
    static const char invalid[] =
        "{\"value\":18446744073709551613}";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *value = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        compile_plan(schema, "Counter", &codec, &plan, &error),
        DATA_BIND_OK);
    if (codec == NULL || plan == NULL) {
      data_bind_validation_plan_free(plan);
      data_bind_free(codec);
      return;
    }

    check_equal(
        data_bind_parse_json(
            codec, "Counter", valid, sizeof(valid) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_OK);
    data_bind_value_free(value);
    value = NULL;

    check_equal(
        data_bind_parse_json(
            codec, "Counter", invalid, sizeof(invalid) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_validation_plan_validate(plan, value, &error),
        DATA_BIND_ERR_VALIDATION);
    data_bind_value_free(value);

    data_bind_validation_plan_free(plan);
    data_bind_free(codec);
  }

  it("rejects constraint/type mismatches and invalid regex during plan compilation") {
    static const char bad_min[] =
        "message Bad { @Min(1) string value; }";
    static const char bad_pattern_type[] =
        "message Bad { @Pattern(\"x\") uint32 value; }";
    static const char bad_size[] =
        "message Bad { @Size(max = 2) uint32 value; }";
    static const char bad_regex[] =
        "message Bad { @Pattern(\"[\") string value; }";
    const char *schemas[] = {
        bad_min, bad_pattern_type, bad_size, bad_regex};
    size_t i;

    for (i = 0u; i < sizeof(schemas) / sizeof(schemas[0]); ++i) {
      DataBind *codec = NULL;
      DataBindValidationPlan *plan = NULL;
      DataBindError error = DATA_BIND_ERROR_INIT;

      check_equal(
          data_bind_create_from_text(
              schemas[i], strlen(schemas[i]), &codec, &error),
          DATA_BIND_OK);
      check_not_null(codec);
      if (codec == NULL) continue;

      check_equal(
          data_bind_validation_plan_compile(
              codec, "Bad", &plan, &error),
          DATA_BIND_ERR_SCHEMA);
      check_null(plan);
      check_equal(error.path, "Bad.value");
      data_bind_free(codec);
    }
  }

  it("recursively validates composites and group items with stable nested paths") {
    static const char schema[] =
        "composite Header { @Min(1) uint32 seq; }"
        "group Fill { @Min(1) uint32 qty; @Pattern(\"[A-Z]+\") string desk; }"
        "message Order { Header header; group<Fill> fills; }";
    static const char bad_header[] =
        "{\"header\":{\"seq\":0},\"fills\":[{\"qty\":1,\"desk\":\"A\"}]}";
    static const char bad_fill[] =
        "{\"header\":{\"seq\":1},"
        "\"fills\":[{\"qty\":1,\"desk\":\"A\"},{\"qty\":0,\"desk\":\"B\"}]}";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *value = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        compile_plan(schema, "Order", &codec, &plan, &error),
        DATA_BIND_OK);
    check_not_null(plan);
    if (codec == NULL || plan == NULL) {
      data_bind_validation_plan_free(plan);
      data_bind_free(codec);
      return;
    }

    /* Direct-rule count remains a per-record view; child rules stay opaque. */
    check_equal(data_bind_validation_plan_rule_count(plan), (size_t)0u);

    check_equal(
        data_bind_parse_json(
            codec, "Order", bad_header, sizeof(bad_header) - 1u,
            &value, &error),
        DATA_BIND_OK);
    check_not_null(value);

    /* Nested plan execution must no longer need its schema/reflection owner. */
    data_bind_free(codec);
    codec = NULL;

    if (value != NULL) {
      check_equal(
          data_bind_validation_plan_validate(plan, value, &error),
          DATA_BIND_ERR_VALIDATION);
      check_equal(error.path, "Order.header.seq");
    }
    data_bind_value_free(value);
    value = NULL;

    /* Recreate only the codec needed to decode the second fixture. */
    check_equal(
        data_bind_create_from_text(
            schema, strlen(schema), &codec, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_parse_json(
            codec, "Order", bad_fill, sizeof(bad_fill) - 1u,
            &value, &error),
        DATA_BIND_OK);
    data_bind_free(codec);
    codec = NULL;

    if (value != NULL) {
      check_equal(
          data_bind_validation_plan_validate(plan, value, &error),
          DATA_BIND_ERR_VALIDATION);
      check_equal(error.path, "Order.fills[1].qty");
    }

    data_bind_value_free(value);
    data_bind_validation_plan_free(plan);
  }

  it("validates nested Pattern after passing parent and sibling rules") {
    static const char schema[] =
        "composite Header { @Min(1) uint32 seq; }"
        "group Fill { @Min(1) uint32 qty; @Pattern(\"[A-Z]+\") string desk; }"
        "message Order { Header header; group<Fill> fills; }";
    static const char json[] =
        "{\"header\":{\"seq\":1},"
        "\"fills\":[{\"qty\":1,\"desk\":\"A\"},{\"qty\":2,\"desk\":\"bad1\"}]}";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindValue *value = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        compile_plan(schema, "Order", &codec, &plan, &error),
        DATA_BIND_OK);
    check_equal(
        data_bind_parse_json(
            codec, "Order", json, sizeof(json) - 1u,
            &value, &error),
        DATA_BIND_OK);

    if (plan != NULL && value != NULL) {
      check_equal(
          data_bind_validation_plan_validate(plan, value, &error),
          DATA_BIND_ERR_VALIDATION);
      check_equal(error.path, "Order.fills[1].desk");
      check_not_null(strstr(error.message, "@Pattern"));
    }

    data_bind_value_free(value);
    data_bind_validation_plan_free(plan);
    data_bind_free(codec);
  }

  it("rejects recursive validation type cycles during plan compilation") {
    static const char schema[] =
        "message Node { list<Node> children; }";
    DataBind *codec = NULL;
    DataBindValidationPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_equal(
        data_bind_create_from_text(
            schema, strlen(schema), &codec, &error),
        DATA_BIND_OK);
    check_not_null(codec);
    if (codec == NULL) return;

    check_equal(
        data_bind_validation_plan_compile(
            codec, "Node", &plan, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_equal(error.path, "Node");
    check_not_null(strstr(error.message, "cycle"));

    data_bind_free(codec);
  }

}
