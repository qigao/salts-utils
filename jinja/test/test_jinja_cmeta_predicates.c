#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta: predicates 1") {
  it("interleaves tests and filters while respecting explicit test arguments") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ 3 is odd|string is string }}|{{ 1 is integer() is true }}", "True|True"},
      {"{{ 1 is eq 1 is true }}|{{ (1 is integer) is true }}", "True|True"},
      {"{{ 3 is odd|string|length is eq 4 }}|{{ 1 is eq(2) is not true }}", "True|True"},
      {"{{ 3 is odd|string is string|string|length }}", "4"},
      {"{% autoescape true %}{{ 3 is odd|string|safe is escaped }}{% endautoescape %}", "True"},
      {"{{ false and 3 is odd|string is eq(1/0) }}|{{ true or 1 is integer() is in(none) }}", "False|True"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects bare consecutive tests even after an earlier explicit test") {
    static const char *const sources[] = {"{{ 1 is integer is true }}", "{{ 1 is integer is true() }}",
      "{{ 1 is integer() is true is boolean }}", "{{ 3 is odd|string is string is boolean }}",
      "{{ 1 is integer is unknown_test }}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("filters test results without filtering shorthand arguments") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ 3 is odd|string|length }}|{{ 3 is not even|string }}", "4|True"},
      {"{{ 1 is eq 1|string }}|{{ 1 is eq (1|string) }}", "True|False"},
      {"{{ 3 is odd|string|center(width=6) }}|{{ 1 is eq(2)|default('no',boolean=true) }}", " True |no"},
      {"{{ 3 is odd|abs + 2 }}|{% if 3 is odd|string|length == 4 %}yes{% endif %}", "3|yes"},
      {"{% autoescape true %}{{ 1 is eq(2)|default('<no>',true) }}|"
        "{{ 1 is eq(2)|default('<no>',true)|safe }}{% endautoescape %}", "&lt;no&gt;|<no>"},
      {"{{ false and 3 is odd|length }}|{{ true or 3 is odd|length }}", "False|True"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("bounds repeated explicit tests with the expression node budget") {
    enum { TEST_CHAIN_LENGTH = 80 };
    static const char step[] = " is true()";
    tstr source = tstr_new_len("{{ true", sizeof("{{ true") - 1u);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    check_not_null(source);
    for (size_t i = 0u; i < TEST_CHAIN_LENGTH; ++i) {
      tstr next = tstr_cat_len(source, step, sizeof(step) - 1u);
      check_not_null(next);
      source = next;
    }
    tstr completed = tstr_cat_len(source, " }}", sizeof(" }}") - 1u);
    check_not_null(completed);
    source = completed;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);
  }

  it("propagates errors from filters following tests") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ 3 is odd|length }}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(jinja_test_render("{{ 3 is odd|string(1) }}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{ 3 is odd|unknown_filter }}"), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_UNSUPPORTED);
  }

  it("accepts postfix chains in shorthand test arguments") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ 6 is divisibleby [3][0] }}|{{ 2 is in range(3) }}", "True|True"},
      {"{{ 3 is in [1,2,3][1:] }}|{{ 6 is divisibleby {'n':3}.n }}", "True|True"},
      {"{{ 1 is eq range(4).count(2) }}|{{ 2 is eq range(4)[1:][1] }}", "True|True"},
      {"{{ 1 is eq {'x':[1]}.x[0] }}|{{ 'd' is eq user.name[1] }}", "True|True"},
      {"{{ 2 is in range(3) and true }}|{{ 0 if 2 is not in range(3) else 1 }}", "True|1"},
      {"{{ 6 is divisibleby [3][0] + 1 }}|{{ 2 is eq [2][0] == true }}", "2|True"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("evaluates membership tests on supported iterables") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ 1 is in([1,2]) }}|{{ 3 is not in(seq=[1,2]) }}|{{ true is in [1] }}", "True|True|True"},
      {"{{ 'a' is in({'a':2}) }}|{{ 2 is in({'a':2}) }}|{{ 2 is in((1,2)) }}", "True|False|True"},
      {"{{ '\u4e2d' is in('a\u4e2db') }}|{{ '' is in('') }}|{{ 4 is in(range(0,6,2)) }}", "True|True|True"},
      {"{{ false and 1 is in(none) }}|{{ true or 1 is in() }}", "False|True"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("propagates shorthand argument lookup errors and rejects malformed chains") {
    static const char *const malformed[] = {"{{ 2 is in range(3 }}", "{{ 6 is divisibleby [3][ }}",
      "{{ 2 is eq [2]. }}", "{{ 2 is in [1,2][:::] }}"};
    for (size_t i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ 1 is eq missing[0] }}", &model, &root, NULL, &output, &error),
      JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(jinja_test_render("{{ false and 1 is eq missing[0] }}|{{ 6 is divisibleby(-3) }}",
      &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True");
    free(output);
  }

  it("treats default undefined membership containers as empty") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ 1 in missing }}|{{ missing not in missing }}",
      &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True");
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{{ 1 is in(missing) }}|{{ missing is not in(seq=missing) }}",
      &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True");
    free(output);
  }

  it("membership tests consume iterators through the first matching tuple") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(
      "{% for pairs in [{'a':1,'b':2,'c':3}|items] %}{{ ('b',2) is in(seq=pairs) }}|"
      "{{ pairs|first }}|{{ ('b',2) is not in(pairs) }}|{{ pairs|list }}{% endfor %}",
      &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|('c', 3)|True|[]");
    free(output);
  }

  it("rejects invalid membership test arguments and containers") {
    static const char *const sources[] = {"{{ 1 is in }}", "{{ 1 is in() }}", "{{ 1 is in([],[]) }}",
      "{{ 1 is in(other=[]) }}", "{{ 1 is in([],seq=[]) }}", "{{ 1 is in(none) }}",
      "{{ 1 is in(3) }}", "{{ 1 is in('1') }}", "{{ [] is in({}) }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("membership tests retain borrowed sequence scan limits") {
    const int ages[] = {21, 37, 42};
    JinjaTestMembershipRoot root = {{ages, 3u, sizeof(ages[0]), &cmeta_data_int},
      {NULL, 0u, 0u, NULL}, {NULL, 0u, 0u, NULL}};
    JinjaTestMembershipModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_membership_model_init(&model);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{ 37 is in(seq=ages) }}"), NULL, &error);
    check_not_null(templ);
    options.max_nodes = 2u;
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    root.ages.count = 2u;
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("evaluates comparison test aliases with existing value semantics") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ 1 is eq(true) }}|{{ 1 is equalto 1.0 }}|{{ 1 is ne('1') }}", "True|True|True"},
      {"{{ 1 is lt(2) }}|{{ 1 is lessthan(2) }}|{{ 2 is le(2) }}", "True|True|True"},
      {"{{ 3 is gt(2) }}|{{ 3 is greaterthan(2) }}|{{ 2 is ge(2) }}", "True|True|True"},
      {"{{ 1 is not eq(2) }}|{{ 2 is lt(1) }}|{{ 2 is ne(2) }}", "True|False|False"},
      {"{{ [1,2] is lt([1,3]) }}|{{ (1,2) is equalto((1,2)) }}|{{ [1] is eq((1,)) }}", "True|True|False"},
      {"{{ {'a':1} is eq({'a':true}) }}|{{ none is eq(none) }}|{{ missing is eq(missing) }}", "True|True|True"},
      {"{{ '\u4e2d' is gt('a') }}|{{ user.age is ge(42) }}|{{ range(3) is eq(range(0,3)) }}", "True|True|True"},
      {"{{ false and 1 is lt(none) }}|{{ true or 1 is eq() }}", "False|True"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects comparison test arity keywords and unordered types") {
    static const char *const sources[] = {"{{ 1 is eq }}", "{{ 1 is eq() }}", "{{ 1 is eq(1,2) }}",
      "{{ 1 is eq(b=1) }}", "{{ 1 is ge(num=1) }}", "{{ 1 is lt(none) }}",
      "{{ [] is lt(()) }}", "{{ {} is le({}) }}", "{{ missing is gt(1) }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("preserves comparison test precision for borrowed unsigned integers and NaN") {
    JinjaTestIntegerRoot integer = {0, SIZE_MAX, false};
    JinjaTestIntegerModel integers;
    JinjaTestFloatRoot floating = {NAN};
    JinjaTestFloatModel floats;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_integer_model_init(&integers);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
      "{{ unsigned_value is eq(unsigned_value) }}|{{ unsigned_value is gt(-1) }}|"
      "{{ unsigned_value is lt(18446744073709551616.0) }}|"
      "{{ unsigned_value is eq(18446744073709551616.0) }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &integers.desc, &integer, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|False");
    free(output);
    jinja_cmeta_release(templ);
    output = NULL;
    jinja_test_float_model_init(&floats);
    templ = jinja_cmeta_compile(vstr_from_cstr(
      "{{ value is eq(value) }}|{{ value is ne(value) }}|{{ value is ge(0) }}|{{ value is lt(0) }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &floats.desc, &floating, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|False|False");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("evaluates extra comparison test arguments before reporting arity") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ 1 is eq(1,9223372036854775807+1) }}", &model, &root,
      NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("treats reserved words as keyword names without defeating short circuit") {
    static const char *const sources[] = {"{{ 12 is divisibleby(none=3) }}",
      "{{ 12 is divisibleby(true=3) }}", "{{ 12 is divisibleby(and=3) }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{ false and (12 is divisibleby(none=3)) }}|"
      "{{ true or (12 is divisibleby(and=3)) }}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True");
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{{ 12 is divisibleby(none=9223372036854775807+1) }}",
      &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("binds divisibleby keyword arguments including grouped values") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ user.age is divisibleby(num=7) }}|"
      "{{ user.age is not divisibleby(num=5,) }}|{{ 12 is divisibleby(num=(1+2)) }}",
      &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
    free(output);
  }

  it("reports invalid test keyword binding after evaluating arguments") {
    static const char *const sources[] = {"{{ 12 is divisibleby(other=3) }}",
      "{{ 12 is divisibleby(3,num=3) }}", "{{ 12 is divisibleby(value=12,num=3) }}",
      "{{ 3 is odd(value=3) }}", "{{ missing is defined(other=none) }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{ 12 is divisibleby(other=9223372036854775807+1) }}",
      &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("rejects repeated test keywords and positional arguments after keywords") {
    static const char *const sources[] = {"{{ user.age is divisibleby(num=3,num=3) }}",
      "{{ user.age is divisibleby(other=3,other=3) }}", "{{ 12 is divisibleby(num=3,3) }}",
      "{{ user.age is divisibleby(not =3,not=3) }}",
      "{{ user.age is divisibleby(not\t=3,not =3) }}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("evaluates divisibleby positional arguments without integer overflow") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ 12 is divisibleby 3 }}|{{ 12 is not divisibleby(5) }}", "True|True"},
      {"{{ 12 is divisibleby(3,) }}|{{ 12 is divisibleby((1 + 2)) }}", "True|True"},
      {"{{ -12 is divisibleby(-3) }}|{{ -9223372036854775808 is divisibleby(-1) }}", "True|True"},
      {"{{ 4.5 is divisibleby(1.5) }}|{{ 4.5 is divisibleby(2) }}", "True|False"},
      {"{{ false is divisibleby(true) }}|{{ user.age is divisibleby(7) }}", "True|True"},
      {"{{ 1 if 9 is divisibleby 3 else 0 }}", "1"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("reports test arity and divisor errors at rendering") {
    static const char *const sources[] = {
      "{{ 3 is odd(()) }}", "{{ 3 is odd((())) }}", "{{ 3 is odd((),) }}", "{{ 3 is odd(1) }}",
      "{{ 3 is divisibleby }}", "{{ 3 is divisibleby() }}", "{{ 3 is divisibleby(1,2) }}",
      "{{ 3 is divisibleby(0) }}", "{{ 3 is divisibleby(-0.0) }}", "{{ 3 is divisibleby(false) }}",
      "{{ 3 is divisibleby('3') }}", "{{ 3 is divisibleby(()) }}", "{{ missing is divisibleby(3) }}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("accepts empty parentheses on supported zero-argument tests") {
    static const char source[] = "{{ user.name is defined() }}|{{ missing is undefined() }}|"
      "{{ 3 is odd() }}|{{ 2 is even () }}|{{ 3 is not even() }}|"
      "{{ none is none() }}|{{ true is true() }}|{{ false is false() }}|"
      "{{ (user.name|safe) is escaped() }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|True|True|True|True|True");
    free(output);
  }

  it("evaluates test arguments eagerly but respects surrounding short circuit") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ 3 is odd(1, 9223372036854775807 + 1) }}", &model, &root,
                                 NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{{ false and 3 is divisibleby(0) }}|{{ true or 3 is odd(1) }}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True");
    free(output);
  }

  it("keeps full unsigned precision for divisibleby") {
    JinjaTestIntegerRoot root = {0, SIZE_MAX, false};
    JinjaTestIntegerModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_integer_model_init(&model);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{{ unsigned_value is divisibleby(3) }}|{{ unsigned_value is divisibleby(2) }}|"
        "{{ unsigned_value is divisibleby(unsigned_value) }}|{{ 0 is divisibleby(unsigned_value) }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True|True");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("does not match nonfinite dividends but accepts zero divided by infinity") {
    static const double values[] = {INFINITY, -INFINITY, NAN};
    static const char *const expected[] = {"False|True", "False|True", "False|False"};
    JinjaTestFloatModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    jinja_test_float_model_init(&model);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{{ value is divisibleby(2) }}|{{ 0 is divisibleby(value) }}"), NULL, &error);
    check_not_null(templ);
    for (size_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
      JinjaTestFloatRoot root = {values[i]};
      char *output = NULL;
      check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
    jinja_cmeta_release(templ);
  }

  it("preserves borrowed unsigned parity and rejects nonfinite parity matches") {
    JinjaTestIntegerRoot integer = {0, SIZE_MAX, false};
    JinjaTestIntegerModel integers;
    JinjaTestFloatModel floats;
    static const double values[] = {INFINITY, -INFINITY, NAN};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;
    jinja_test_integer_model_init(&integers);
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ unsigned_value is odd }}|{{ unsigned_value is even }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &integers.desc, &integer, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False");
    free(output);
    jinja_cmeta_release(templ);
    jinja_test_float_model_init(&floats);
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ value is odd }}|{{ value is even }}"), NULL, &error);
    check_not_null(templ);
    for (size_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
      JinjaTestFloatRoot root = {values[i]};
      output = NULL;
      check_equal(jinja_cmeta_render_string(templ, &floats.desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, "False|False");
      free(output);
    }
    jinja_cmeta_release(templ);
  }

  it("tests numeric parity for integers floats and booleans") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ 3 is odd }}|{{ 3 is even }}|{{ -3 is odd }}|{{ -4 is even }}", "True|False|True|True"},
      {"{{ true is odd }}|{{ false is even }}|{{ 0 is even }}|{{ 2 is not odd }}", "True|True|True|True"},
      {"{{ 3.0 is odd }}|{{ -3.0 is odd }}|{{ 2.5 is even }}|{{ -1.5 is odd }}", "True|True|False|False"},
      {"{{ 9223372036854775807 is odd }}|{{ -9223372036854775808 is even }}", "True|True"},
      {"{% if user.age is even %}even{% else %}odd{% endif %}", "even"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects nonnumeric parity operands") {
    static const char *const cases[] = {"{{ '3' is odd }}", "{{ none is even }}", "{{ missing is odd }}", "{{ [] is even }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }
}

spec("Jinja CMeta builtin queries") {
  it("rejects malformed borrowed query names") {
    static const unsigned char invalid[] = {0xc0u, 0xafu};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_buf((const char *)invalid, sizeof(invalid)), 0}, false,
        {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{user.name is filter}}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_METADATA);
    check_null(output);
    root.user.name = vstr_from_cstr("length");
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_string_bytes = 5u;
    check_equal(jinja_test_render("{{user.name is filter}}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    free(output);
  }

  it("rejects unhashable query values and extra arguments") {
    static const char *sources[] = {
      "{{[] is filter}}", "{{{} is test}}", "{{([],1) is filter}}",
      "{{'length' is filter(1)}}", "{{'eq' is test(value='eq')}}"
    };
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
      free(output);
    }
  }

  it("queries exact names and reports only this engines available builtins") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{'upper' is filter}}|{{'sameas' is test}}|"
        "{{'length\\0x' is filter}}|{{'defined.custom' is test}}|{{false and ([] is test)}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|False|False|False");
    free(output);
  }
  it("builtin_filter_query") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{'batch' is filter}}|{{'slice' is filter}}|{{'d' is filter}}|{{'not_a_filter' is not filter}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True");
    free(output);
  }
  it("builtin_test_query") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{'filter' is test}}|{{'test' is test}}|{{'eq' is test}}|{{'==' is test}}|{{'not_a_test' is test}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|False");
    free(output);
  }
  it("builtin_query_types") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{none is filter}}|{{42 is test}}|{{missing is filter}}|{{('x',) is test}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False|False|False");
    free(output);
  }
  it("builtin_query_dynamic") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set n='len'~'gth' %}{{n is filter(*[],**{})}}|{{('in'|safe) is test}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True");
    free(output);
  }
}

spec("Jinja CMeta collections and runtime: predicates 6") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("evaluates conditional expression values, precedence, and nesting") {
    static const char source[] =
        "{{ 'yes' if true else 'no' }}|{{ 1 if false else 2 }}|"
        "{{ false or 'a' if true else 'b' }}|{{ 1 == 1 if true else false }}|"
        "{{ (1 if true else 2) + 3 }}|"
        "{{ 'a' if false else 'b' if false else 'c' }}|"
        "<{{ 1 if false if true else 4 }}>|{{ 1 if true if false else 4 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "yes|2|a|True|4|c|<>|4");
    free(output);
  }

  it("returns undefined without else and short circuits conditional branches") {
    static const char source[] =
        "<{{ 'x' if false }}>|{{ ('x' if false) == missing }}|"
        "{{ 'ok' if true else 1 // 0 }}|{{ 1 // 0 if false else 'fallback' }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<>|True|ok|fallback");
    free(output);
  }

  it("resolves conditional branches in loop and parent scopes") {
    static const char source[] =
        "{{ user.name if active else 'off' }}|"
        "{% for item in users %}{{ item.name if item.age > user.age else user.name }}|{% endfor %}"
        "{% if (users if active else false) %}nonempty{% else %}empty{% endif %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "root|root|Lin|nonempty");
    free(output);
  }

  it("owns conditional expression source and charges one final render node") {
    char source[] = "{{ user.age if active else 0 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);
    options.max_nodes = 2u;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "37");
    free(output);

    options.max_nodes = 1u;
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    jinja_cmeta_release(templ);
  }

  it("bounds conditional expression syntax and nodes") {
    static const char *const malformed[] = {"{{ 'a' if else 'b' }}", "{{ 'a' if }}",
                                            "{{ 'a' else 'b' }}",
                                            "{% if true if true else false %}x{% endif %}"};
    JINJA_CMETA_TEMPLATE *templ;
    tstr source;
    size_t i;

    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    }

    source = jinja_test_conditional_chain(21u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
    tstr_free(source);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    source = jinja_test_conditional_chain(22u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);
  }

  it("renders none and applies none truthiness and equality") {
    static const char source[] =
        "<{{ none }}>|<{{ None }}>|{{ none == None }}|{{ none != missing }}|"
        "{{ none == missing }}|{{ not none }}|{{ not not none }}|"
        "{% if none %}bad{% else %}empty{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<None>|<None>|True|True|False|True|False|empty");
    free(output);
  }

  it("supports defined undefined none and is not tests") {
    static const char source[] =
        "{{ missing is defined }}|{{ missing is undefined }}|{{ none is defined }}|"
        "{{ none is undefined }}|{{ none is none }}|{{ missing is not none }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|True|False|True|True");
    free(output);
  }

  it("applies core literal type tests with Jinja precedence") {
    static const char source[] =
        "{{ true is boolean }}|{{ false is false }}|{{ true is true }}|"
        "{{ true is number }}|{{ true is integer }}|{{ 1 is integer }}|"
        "{{ 1 is number }}|{{ 1 is not boolean }}|{{ -1 is integer }}|"
        "{{ not 1 is integer }}|{{ (1 + 2) is integer }}|{{ 1 + (true is integer) }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|False|True|True|True|True|False|True|1");
    free(output);
  }

  it("classifies runtime CMeta scalars containers and loop values") {
    static const char source[] =
        "{{ user.name is string }}|{{ user.age is integer }}|{{ active is boolean }}|"
        "{{ users is sequence }}|{{ user is mapping }}|"
        "{% for item in users %}{{ item.name is string }}{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|True|TrueTrue");
    free(output);
  }

  it("classifies runtime floating point values separately from integers") {
    static const char source[] =
        "{{ value is float }}|{{ value is number }}|{{ value is integer }}";
    JinjaTestFloatRoot root = {1.5};
    JinjaTestFloatModel model;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    char *output = NULL;

    jinja_test_float_model_init(&model);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True|True|False");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("matches Jinja sequence and iterable predicate classification") {
    static const char source[] =
        "{{ missing is sequence }}|{{ missing is iterable }}|{{ user is sequence }}|"
        "{{ user is iterable }}|{{ user.name is sequence }}|{{ user.name is iterable }}|"
        "{{ users is iterable }}|{{ 1 is sequence }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|True|True|True|False");
    free(output);
  }

  it("owns test source and bounds test expressions and syntax") {
    static const char *const malformed[] = {"{{ 1 is }}", "{{ 1 is integer is true }}",
                                            "{{ 1 is not }}"};
    static const char *const unsupported[] = {"{{ 1 is unknown_test }}",
                                              "{{ 1 is custom.test }}",
                                              "{{ none is None }}"};
    char owned_source[] = "{{ user.name is string }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_buf(owned_source, sizeof(owned_source) - 1u), NULL, &error);
    tstr source;
    char *output = NULL;
    size_t i;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_not_null(templ);
    memset(owned_source, 'x', sizeof(owned_source) - 1u);
    options.max_nodes = 2u;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
    output = NULL;
    options.max_nodes = 1u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    jinja_cmeta_release(templ);

    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
      templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
    for (i = 0u; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
      info("unsupported test expression: %s", unsupported[i]);
      error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
      templ = jinja_cmeta_compile(vstr_from_cstr(unsupported[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_UNSUPPORTED);
    }

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    source = jinja_test_linear_is_tests((JINJA_CMETA_MAX_CONDITION_BRANCHES + 1u) / 3u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
    tstr_free(source);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    source = jinja_test_linear_is_tests((JINJA_CMETA_MAX_CONDITION_BRANCHES + 1u) / 3u + 1u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);
  }

  it("reports a lone assignment operator as comparison syntax") {
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{ 1 = 1 }}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
  }

  it("does not let an unsupported comparison hide malformed grouping") {
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr("{{ user.age == 37) }}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
  }

  it("rejects integer literals outside int64 range") {
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr("{{ 9223372036854775808 }}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ -9223372036854775809 }}"), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
  }

  it("charges integer literal results to the render node budget") {
    static const char source[] = "{{ 1 }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("accepts Jinja whitespace between a unary sign and its operand") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ - 1 }}|{{ + 1 }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "-1|1");
    free(output);
  }
}
