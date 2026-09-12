#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta collections and runtime: arithmetic 5") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("evaluates bounded integer arithmetic with Jinja precedence") {
    static const char source[] = "{{ 1+2*3 }}|{{ (1 + 2) * 3 }}|{{ 20 // 3 }}|{{ 20 % 3 }}|"
                                 "{{ 2 ** 3 ** 2 }}|{{ 10 - 3 - 2 }}|{{ 24 // 3 * 2 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7|9|6|2|64|5|16");
    free(output);
  }

  it("renders Jinja float literals with Python formatting thresholds") {
    static const char source[] =
        "{{ 1_000 }}|{{ 1.0 }}|{{ 0.1 }}|{{ 1e3 }}|{{ 1_000.25 }}|{{ 1e15 }}|{{ 1e16 }}|"
        "{{ 1e-4 }}|{{ 1e-5 }}|{{ -0.0 }}|{{ 5e-324 }}|"
        "{{ 2.2250738585072014e-308 }}|{{ 1.7976931348623157e308 }}|{{ 1e309 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1000|1.0|0.1|1000.0|1000.25|1000000000000000.0|1e+16|0.0001|1e-05|-0.0|"
                        "5e-324|2.2250738585072014e-308|1.7976931348623157e+308|inf");
    free(output);
  }

  it("rounds integer true division before converting operands to double") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{9007199254740993 / 3}}", "3002399751580331.0"},
      {"{{9007199254740992 / 9007199254740993}}", "0.9999999999999999"},
      {"{{ -9007199254740993 / 3}}|{{9007199254740993 / -3}}|{{ -9007199254740993 / -3}}",
       "-3002399751580331.0|-3002399751580331.0|3002399751580331.0"},
      {"{{9007199254740993 / 1}}|{{9007199254740995 / 1}}", "9007199254740992.0|9007199254740996.0"},
      {"{{27021597764222980 / 3}}|{{27021597764222978 / 3}}", "9007199254740994.0|9007199254740992.0"},
      {"{{9223372036854775807 / 1}}|{{ -9223372036854775808 / -1}}", "9.223372036854776e+18|9.223372036854776e+18"},
      {"{{1 / -9223372036854775808}}|{{1 / 9223372036854775807}}", "-1.0842021724855044e-19|1.0842021724855044e-19"},
      {"{% set a=9007199254740993 %}{% if a/3 == 3002399751580331 %}OK{% else %}WRONG{% endif %}", "OK"},
      {"{{9007199254740993 / 3.0}}|{{0 / -9223372036854775808}}|{{true / 2}}", "3002399751580330.5|-0.0|0.5"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      info("integer division: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("evaluates true division and mixed numeric arithmetic") {
    static const char source[] =
        "{{ 7 / 3 }}|{{ 4 / 2 }}|{{ 5.5 + 2 }}|{{ 5.5 - 2 }}|{{ 5.5 * 2 }}|"
        "{{ 5.5 // 2 }}|{{ -7.0 // 3 }}|{{ -7.0 % 3 }}|{{ 5.5 ** 2 }}|"
        "{{ 2 ** -2 }}|{{ true / 2 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2.3333333333333335|2.0|7.5|3.5|11.0|2.0|-3.0|2.0|30.25|0.25|0.5");
    free(output);
  }

  it("compares classifies and tests float truthiness") {
    static const char source[] =
        "{{ 1.5 == 1.5 }}|{{ 1.5 != 2 }}|{{ 1.5 < 2 }}|{{ 2 <= 2.0 }}|"
        "{{ 3 > 2.5 }}|{{ 3.0 >= 3 }}|{{ 9223372036854775807 < 9.223372036854776e18 }}|"
        "{{ 9223372036854775807 == 9.223372036854776e18 }}|"
        "{{ 0.0 and 'bad' or 'zero' }}|"
        "{{ 0.5 and 'yes' }}|{{ (4 / 2) is float }}|{{ 1.0 is number }}|"
        "{{ 1.0 is integer }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|True|True|True|False|zero|yes|True|True|False");
    free(output);
  }

  it("uses runtime CMeta floats in arithmetic comparison and membership") {
    static const char source[] =
        "{{ value }}|{{ value + 2 }}|{{ value / 2 }}|{{ value == 1.5 }}|{{ value > 1 }}";
    const int ages[] = {21, 37, 42};
    JinjaTestFloatRoot float_root = {1.5};
    JinjaTestFloatModel float_model;
    JinjaTestMembershipRoot membership_root = {
        {ages, 3u, sizeof(ages[0]), &cmeta_data_int}, {NULL, 0u, 0u, NULL}, {NULL, 0u, 0u, NULL}};
    JinjaTestMembershipModel membership_model;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_float_model_init(&float_model);
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(
        jinja_cmeta_render_string(templ, &float_model.desc, &float_root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "1.5|3.5|0.75|True|True");
    free(output);
    output = NULL;
    jinja_cmeta_release(templ);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ 37.0 in ages }}"), NULL, &error);
    jinja_test_membership_model_init(&membership_model);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &membership_model.desc, &membership_root, NULL,
                                          &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("matches Python unordered comparisons and truthiness for runtime NaN") {
    static const char source[] =
        "{{ value == value }}|{{ value != value }}|{{ value < 1 }}|{{ value >= 1 }}|"
        "{{ value }}|{{ value and 'yes' }}";
    JinjaTestFloatRoot root = {NAN};
    JinjaTestFloatModel model;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    char *output = NULL;

    jinja_test_float_model_init(&model);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "False|True|False|False|nan|yes");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("owns float literals and charges one final render node") {
    char source[] = "{{ 1.25 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);
    options.max_nodes = 1u;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    options.max_nodes = 2u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "1.25");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("keeps float parsing and rendering independent of numeric locale") {
#if defined(_WIN32)
    static const char source[] = "{{ 1.5 + 0.25 }}";
    const char *current_locale = setlocale(LC_NUMERIC, NULL);
    tstr original_locale = current_locale != NULL ? tstr_dup(current_locale) : NULL;
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    int locale_available = 0;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    if (original_locale != NULL) {
      locale_available = setlocale(LC_NUMERIC, ".1252") != NULL;
      if (locale_available)
        check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
                    JINJA_CMETA_OK);
      setlocale(LC_NUMERIC, original_locale);
    }
    check_not_null(original_locale);
    check(locale_available);
    check_equal(output, "1.75");
    free(output);
    tstr_free(original_locale);
#else
    check(1);
#endif
  }

  it("rejects malformed floats zero divisors and non-real powers") {
    static const struct {
      const char *source;
      JINJA_CMETA_STATUS expected;
    } cases[] = {
        {"{{ .5 }}", JINJA_CMETA_ERR_SYNTAX},       {"{{ 1. }}", JINJA_CMETA_ERR_SYNTAX},
        {"{{ 1__0.0 }}", JINJA_CMETA_ERR_SYNTAX},   {"{{ 1_.0 }}", JINJA_CMETA_ERR_SYNTAX},
        {"{{ 1.0_ }}", JINJA_CMETA_ERR_SYNTAX},     {"{{ 1.0 / 0 }}", JINJA_CMETA_ERR_RENDER},
        {"{{ 1.0 // 0 }}", JINJA_CMETA_ERR_RENDER}, {"{{ 1.0 % 0 }}", JINJA_CMETA_ERR_RENDER},
        {"{{ 0 ** -1 }}", JINJA_CMETA_ERR_RENDER},  {"{{ (-1) ** 0.5 }}", JINJA_CMETA_ERR_RENDER},
        {"{{ 'x' + 1.0 }}", JINJA_CMETA_ERR_RENDER}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error),
                  cases[i].expected);
      check_null(output);
    }
  }

  it("uses Jinja floor signs and unary integer operators") {
    static const char source[] = "{{ -17 // 5 }}|{{ -17 % 5 }}|{{ 17 // -5 }}|{{ 17 % -5 }}|"
                                 "{{ - 2 }}|{{ + 2 }}|{{ -(1 + 2) }}|{{ -(-2) }}|{{ -2 ** 2 }}|"
                                 "{{ -9223372036854775808 % -1 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "-4|3|-4|-3|-2|2|-3|2|4|0");
    free(output);
  }

  it("evaluates runtime arithmetic in comparisons and nested scopes") {
    static const char source[] = "{{ signed_value // unsigned_value }}|"
                                 "{{ signed_value % unsigned_value }}|"
                                 "{{ unsigned_value + boolean_value }}|{{ - signed_value }}|"
                                 "{{ signed_value + unsigned_value < 0 }}";
    static const char scoped_source[] =
        "{% for item in users %}{{ item.age + user.age }}|{% endfor %}"
        "{% if user.age * 2 == 74 %}yes{% endif %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestIntegerRoot integer_root = {-17, 5u, true};
    JinjaTestIntegerModel integer_model;
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_integer_model_init(&integer_model);
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(
        jinja_cmeta_render_string(templ, &integer_model.desc, &integer_root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "-4|3|6|17|True");
    free(output);
    jinja_cmeta_release(templ);

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(scoped_source, &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "74|79|yes");
    free(output);
  }

  it("reports bounded integer arithmetic failures deterministically") {
    static const struct {
      const char *source;
      JINJA_CMETA_STATUS expected;
    } cases[] = {{"{{ 9223372036854775807 + 1 }}", JINJA_CMETA_ERR_CAPACITY},
                 {"{{ -9223372036854775808 - 1 }}", JINJA_CMETA_ERR_CAPACITY},
                 {"{{ 9223372036854775807 * 2 }}", JINJA_CMETA_ERR_CAPACITY},
                 {"{{ 2 ** 63 }}", JINJA_CMETA_ERR_CAPACITY},
                 {"{{ -9223372036854775808 // -1 }}", JINJA_CMETA_ERR_CAPACITY},
                 {"{{ 1 // 0 }}", JINJA_CMETA_ERR_RENDER},
                 {"{{ 1 % 0 }}", JINJA_CMETA_ERR_RENDER},
                 {"{{ 'x' - 1 }}", JINJA_CMETA_ERR_RENDER},
                 {"{{ missing + 1 }}", JINJA_CMETA_ERR_RENDER}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error),
                  cases[i].expected);
      check_null(output);
    }
    {
      JinjaTestIntegerRoot integer_root = {0, SIZE_MAX, false};
      JinjaTestIntegerModel integer_model;
      JINJA_CMETA_TEMPLATE *templ =
          jinja_cmeta_compile(vstr_from_cstr("{{ unsigned_value + 0 }}"), NULL, &error);
      char *output = NULL;

      jinja_test_integer_model_init(&integer_model);
      check_not_null(templ);
      check_equal(jinja_cmeta_render_string(templ, &integer_model.desc, &integer_root, NULL,
                                            &output, &error),
                  JINJA_CMETA_ERR_CAPACITY);
      check_null(output);
      jinja_cmeta_release(templ);
    }
  }

  it("owns arithmetic paths and charges one final render node") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char source[] = "{{ user.age + 5 }}";
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);
    options.max_nodes = 1u;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    options.max_nodes = 2u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "42");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("bounds arithmetic syntax and expression nodes") {
    static const char *const malformed[] = {"{{ 1 + }}", "{{ * 1 }}", "{{ 1 ** }}"};
    JINJA_CMETA_TEMPLATE *templ;
    tstr source;
    size_t i;

    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      info("malformed test expression: %s", malformed[i]);
      templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    }
    source = jinja_test_arithmetic_terms(32u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
    tstr_free(source);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    source = jinja_test_arithmetic_terms(33u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);
  }
}
