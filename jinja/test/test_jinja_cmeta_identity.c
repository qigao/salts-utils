#include "jinja_cmeta_test_support.h"

#define JINJA_TEST_RUNTIME_NAN \
  "{% set big=1e308 %}{% set inf=big*big %}{% set n=inf-inf %}"

spec("Jinja container identity comparisons") {
  static JinjaTestModel model;
  static JinjaTestRoot root;
  static JINJA_CMETA_ERROR error;
  static char *output;
  before_each() {
    jinja_test_model_init(&model);
    root = (JinjaTestRoot){{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    output = NULL;
  }
  after_each() { free(output); }

  it("keeps scalar NaN comparisons unordered while containers recognize its identity") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN
        "{{n==n}}|{{n!=n}}|{{n is eq(n)}}|{{n is sameas(n)}}|{{[n]==[n]}}|{{(n,)!=(n,)}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|False|True|True|False");
  }

  it("does not equate independently created NaNs inside containers") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN "{% set m=inf-inf %}"
        "{{[n]==[m]}}|{{(n,)==(m,)}}|{{n in [m]}}|{{n in [n]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False|False|True");
  }

  it("continues lexicographic ordering after an identical NaN element") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN
        "{% set a=[n,1] %}{% set b=[n,2] %}"
        "{{a<b}}|{{a<=b}}|{{b>a}}|{{b>=a}}|{{a<=a}}|{{a>a}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|True|False");
  }

  it("finds and replaces the same NaN dictionary key without breaking items") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN
        "{% set d={n:1,n:2} %}{{d|length}}|{{d[n]}}|{{n in d}}|{{d|items|list}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1|2|True|[(nan, 2)]");
  }

  it("deduplicates tuple keys containing the same NaN") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN
        "{% set d={(n,):1,(n,):2} %}{{d|length}}|{{d[(n,)]}}|{{d|items|list}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1|2|[((nan,), 2)]");
  }

  it("keeps independently created NaNs as distinct dictionary keys") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN "{% set m=inf-inf %}"
        "{% set d={n:1,m:2} %}{{d|length}}|{{d[n]}}|{{d[m]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2|1|2");
  }

  it("compares dictionary NaN values by identity or equality") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN "{% set m=inf-inf %}"
        "{{{'x':n}=={'x':n}}}|{{{'x':n}!={'x':n}}}|{{{'x':n}=={'x':m}}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|False");
  }

  it("uses identity in both membership operators and membership tests") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN
        "{{n in [n]}}|{{n not in (n,)}}|{{n is in([n])}}|{{n is not in((n,))}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True|False");
  }

  it("stops reverse iterator membership at the identical NaN") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN
        "{% set it=[1,n,3]|reverse %}{{n in it}}|{{it|list}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|[1]");
  }

  it("stops items membership after a tuple with an identical NaN value") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN
        "{% set it={1:n,2:0}|items %}{{(1,n) in it}}|{{it|list}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|[(2, 0)]");
  }

  it("loop changed distinguishes NaN aliases from independent values") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN "{% set m=inf-inf %}"
        "{% for x in [n,n,m,n,n] %}{{loop.changed(x)}}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "TrueFalseTrueTrueFalse");
  }

  it("preserves NaN identity in shared nested container elements") {
    check_equal(jinja_test_render(JINJA_TEST_RUNTIME_NAN
        "{% set a=[n] %}{{[a]==[a]}}|{{a in [a]}}|{{{'a':a}=={'a':a}}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
  }

  it("recognizes borrowed NaN identity without merging distinct host elements") {
    double values[] = {NAN, NAN};
    root.users = (JINJA_CMETA_SEQUENCE_VIEW){values, 2u, sizeof(values[0]), &cmeta_data_double};
    check_equal(jinja_test_render(
        "{{users[0] in users}}|{{users[0] in [users[0]]}}|"
        "{{users[0] in [users[1]]}}|{{users[0]==users[0]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|False|False");
  }

  it("retains numeric dictionary key equality across scalar kinds") {
    check_equal(jinja_test_render("{% set d={true:1,1:2,1.0:3} %}"
        "{{d|length}}|{{d[true]}}|{{d[1.0]}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1|3|3");
  }

  it("does not let borrowed identity bypass malformed float metadata") {
    double value = NAN;
    cmeta_data_desc invalid = cmeta_data_double;
    cmeta_data_float_shape wrong_width = {32u};
    invalid.shape = &wrong_width;
    check_true(cmeta_data_desc_valid(&invalid));
    root.users = (JINJA_CMETA_SEQUENCE_VIEW){&value, 1u, sizeof(value), &invalid};
    check_equal(jinja_test_render("{{users[0] in users}}", &model, &root, NULL,
        &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
  }

  it("validates borrowed float metadata inside identical container aliases") {
    static const char *sources[] = {
      "{% set a=[users[0]] %}{{a in [a]}}",
      "{% set a=(users[0],) %}{{[a]==[a]}}",
      "{% set a={'x':users[0]} %}{{{'a':a}=={'a':a}}}"
    };
    double value = NAN;
    cmeta_data_desc invalid = cmeta_data_double;
    cmeta_data_float_shape wrong_width = {32u};
    invalid.shape = &wrong_width;
    check_true(cmeta_data_desc_valid(&invalid));
    root.users = (JINJA_CMETA_SEQUENCE_VIEW){&value, 1u, sizeof(value), &invalid};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
          JINJA_CMETA_ERR_METADATA);
      check_null(output);
    }
  }

  it("does not let borrowed string identity bypass its byte capacity") {
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_string_bytes = 8u;
    root.user.name = vstr_from_cstr("0123456789abcdef");
    check_equal(jinja_test_render("{{user.name in [user.name]}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("enforces borrowed string capacity inside identical container aliases") {
    static const char *sources[] = {
      "{% set a=[user.name] %}{{a in [a]}}",
      "{% set a=(user.name,) %}{{[a]==[a]}}",
      "{% set a={'x':user.name} %}{{{'a':a}=={'a':a}}}"
    };
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_string_bytes = 8u;
    root.user.name = vstr_from_cstr("0123456789abcdef");
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, &options, &output, &error),
          JINJA_CMETA_ERR_CAPACITY);
      check_null(output);
    }
  }

  it("still rejects unhashable nested keys when the tuple is its own alias") {
    check_equal(jinja_test_render("{% set k=([],) %}{{{k:1}[k]}}", &model, &root, NULL,
        &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }
}

spec("Jinja CMeta sameas") {
  it("sameas_replace_equal_distinct_parameters") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("ab"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a='abc' %}{{a|replace('ab','ab'|safe) is sameas(a)}}|{{a|replace('ab'|safe,'ab') is sameas(a)}}|{{a|replace(user.name,'ab') is sameas(a)}}|{{a|replace('ab',user.name) is sameas(a)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False|False|False");
    free(output);
  }
  it("sameas_replace_shared_parameter") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("ab"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a='abc' %}{% set x=user.name %}{{a|replace(x,x) is sameas(a)}}|{% set x=x|safe %}{{a|replace(x,x) is sameas(a)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False");
    free(output);
  }
  it("sameas_replace_plain_no_match") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a='abcΩ' %}{{a|replace('x','y') is sameas(a)}}|{% autoescape true %}{{a|replace('x','y') is sameas(a)}}{% endautoescape %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True");
    free(output);
  }
  it("sameas_replace_plain_identical") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a='abcΩ' %}{{a|replace('Ω','Ω') is sameas(a)}}|{{a|replace('','') is sameas(a)}}|{% autoescape true %}{{a|replace('a','a') is sameas(a)}}{% endautoescape %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
    free(output);
  }
  it("sameas_replace_markup_zero_count") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a='<abc>'|safe %}{% set b=a|replace('x','y',0) %}{{b is sameas(a)}}|{{b is escaped}}|{% autoescape true %}{% set b=a|replace('x','y',0) %}{{b is sameas(a)}}|{{b is escaped}}{% endautoescape %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False|False|True");
    free(output);
  }
  it("sameas_replace_markup_noop") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a='abcΩ'|safe %}{{a|replace('x','y') is sameas(a)}}|{{a|replace('Ω','Ω') is sameas(a)}}|{% autoescape true %}{{a|replace('x','y') is sameas(a)}}|{{a|replace('Ω','Ω') is sameas(a)}}{% endautoescape %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False|False|False");
    free(output);
  }
  it("sameas_empty_sum_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=1.5 %}{% set b=[] %}"
        "{{a is sameas([]|sum(start=a))}}|{{b is sameas([]|sum(start=b))}}|"
        "{{true is sameas([]|sum(start=true))}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|True");
    free(output);
  }
  it("sameas_plain_string_passthrough") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=user.name %}{{a is sameas(a*1)}}|{{a is sameas(a+'')}}|{{a is sameas([a]|join)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
    free(output);
  }
  it("sameas_markup_string_creation") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=user.name|safe %}{{a is sameas(a*1)}}|{{a is sameas(a+'')}}|{{a is sameas([a]|join)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False|False");
    free(output);
  }
  it("sameas_batch_elements") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a='Ωβ'|batch(2)|first %}{{a[0] is sameas(a[0])}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }
  it("sameas_constant_reexecution") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set n=namespace(v=none) %}{% for x in [1,2] %}{% set a=1000 %}{% if not loop.first %}{{a is sameas(n.v)}}{% endif %}{% set n.v=a %}{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }
  it("sameas_sum_result") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=1000+user.age %}{% set b=[1]|sum(start=a) %}{{a}}|{{b}}|{{a is sameas b}}|{{a is sameas([]|sum(start=a))}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1001|1002|False|False");
    free(output);
  }
  it("sameas_host_string_conversion") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{user.name is sameas(user.name|string)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }
  it("sameas_generated_characters") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a='Ωβ'|list %}{{a[0] is sameas(a[0])}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }
  it("sameas_generated_dict_key") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set d=dict(abcdefgh=1) %}{% set k=d|list|first %}{{k is sameas(d|list|first)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }
  it("sameas_macro_metadata") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% macro m(foo,bar) %}{% endmacro %}{{m.arguments is sameas(m.arguments)}}|{{m.arguments[0] is sameas(m.arguments[0])}}|{{m.name is sameas(m.name)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
    free(output);
  }
  it("sameas_numeric_passthrough") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=1000+user.age %}{{a is sameas(a|abs)}}|{{a is sameas(+a)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True");
    free(output);
  }
  it("sameas_tuple_passthrough") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=(user.age,) %}{{a is sameas(a*1)}}|{{a is sameas(a+())}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True");
    free(output);
  }
  it("sameas_markup_center") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=user.name|safe %}{{a is sameas(a|center(1))}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False");
    free(output);
  }
  it("sameas_none_iteration") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% for x in [none] %}{{x is none}}|{{x is sameas none}}{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True");
    free(output);
  }
  it("sameas_host_small_integer") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{user.age is sameas 1}}|{{user.age is sameas user.age}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True");
    free(output);
  }
  it("sameas_builtin_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{dict is sameas dict}}|{{namespace is sameas namespace}}|{{range is sameas range}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
    free(output);
  }
  it("sameas_short_circuit") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{false and (none is sameas(1/0))}}|{{[] if false else (none is sameas none)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True");
    free(output);
  }
  it("sameas_slice_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=[1,2] %}{% set t=(1,2) %}{% set s=user.name %}{{a is sameas(a[:])}}|{{t is sameas(t[:])}}|{{s is sameas(s[:])}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|True");
    free(output);
  }
  it("sameas_trim_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set s=user.name %}{{s is sameas(s|trim)}}|{{s is sameas(s|center(1))}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True");
    free(output);
  }
  it("sameas_unknown_keyword") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{none is sameas(value=none)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    free(output);
  }
  it("sameas_duplicate_argument") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{none is sameas(none,other=none)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    free(output);
  }
  it("sameas_scalar_alias") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=1000+user.age %}{% set b=a %}{{a is sameas b}}|{{a is sameas(a+0)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False");
    free(output);
  }
  it("sameas_safe_conversion") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=user.name %}{% set b=a|safe %}{{a is sameas b}}|{{b is sameas(b|string)}}|{{b is sameas(b|safe)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|False");
    free(output);
  }
  it("sameas_loop_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=[[],{}] %}{% for x in a %}{{x is sameas(a[loop.index0])}}{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "TrueTrue");
    free(output);
  }
  it("sameas_batch_neighbor_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=[1,2]|batch(1) %}{% set n=namespace(v=none) %}{% for x in a %}{% if not loop.first %}{{x is sameas(n.v)}}{% endif %}{% set n.v=loop.nextitem %}{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }
  it("sameas_macro_argument_identity") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% macro m(x,y=x) %}{{x is sameas y}}{% endmacro %}{% set a=[] %}{{m(a)}}|{{m(a,a)}}|{{m(a,[])}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|False");
    free(output);
  }
  it("sameas_singleton_literals") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{1 is sameas 1}}|{{2.5 is sameas 2.5}}|{{'abc' is sameas 'abc'}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|False");
    free(output);
  }
  it("sameas_undefined_iteration") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=[missing] %}{% for x in a %}{{x is undefined}}|{{x is sameas(a[0])}}{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True");
    free(output);
  }
  it("sameas_singletons") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{none is sameas none}}|{{true is sameas true}}|{{false is sameas false}}|{{true is sameas 1}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True|False");
    free(output);
  }
  it("sameas_list_alias") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=[1] %}{% set b=a %}{{a is sameas b}}|{{a is sameas [1]}}|{{a == [1]}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True");
    free(output);
  }
  it("sameas_empty_containers") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=[] %}{% set b={} %}{{a is sameas a}}|{{a is sameas []}}|{{b is sameas b}}|{{b is sameas {}}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True|False");
    free(output);
  }
  it("sameas_undefined") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=missing %}{{a is sameas a}}|{{missing is sameas missing}}|{{a is sameas missing}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|False");
    free(output);
  }
  it("sameas_range") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=range(3) %}{{a is sameas a}}|{{a is sameas range(3)}}|{{a == range(3)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True");
    free(output);
  }
  it("sameas_bound_method") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=range(3) %}{% set f=a.count %}{{f is sameas f}}|{{a.count is sameas a.count}}|{{f == a.count}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True");
    free(output);
  }
  it("sameas_namespace") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=namespace(x=1) %}{% set b=a %}{{a is sameas b}}|{{a is sameas namespace(x=1)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False");
    free(output);
  }
  it("sameas_iterator") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=[1,2]|batch(1) %}{% set b=a %}{{a is sameas b}}|{{a is sameas ([1,2]|batch(1))}}|{{b|first}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|[1]");
    free(output);
  }
  it("sameas_macro") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% macro m() %}x{% endmacro %}{% set alias=m %}{{m is sameas alias}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
  }
  it("sameas_binding") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{none is sameas(other=none)}}|{{false is not sameas true}}|{{true is sameas(*[true],**{})}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
    free(output);
  }
  it("sameas_copy_and_element") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set a=[[]] %}{{a[0] is sameas(a[0])}}|{{a is sameas(a|list)}}|{{a[0] is sameas((a|list)[0])}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|True");
    free(output);
  }
  it("sameas_missing_argument") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{none is sameas}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    free(output);
  }
  it("sameas_extra_argument") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{none is sameas(none,none)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    free(output);
  }
}
