#include "jinja_cmeta_test_support.h"

spec("Jinja value traversal budgets") {
  static JinjaTestModel model;
  static JinjaTestRoot root;
  static JINJA_CMETA_ERROR error;
  static JINJA_CMETA_RENDER_OPTIONS options;
  static JINJA_CMETA_TEMPLATE *templ;
  static char *output;
  before_each() {
    jinja_test_model_init(&model);
    root = (JinjaTestRoot){{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    options = (JINJA_CMETA_RENDER_OPTIONS)JINJA_CMETA_RENDER_OPTIONS_INIT;
    templ = NULL;
    output = NULL;
  }
  after_each() { free(output); jinja_cmeta_release(templ); }

  it("accepts dynamic tuple keys independently of expression count") {
    check_equal(jinja_test_render("{% set ns=namespace(a=0) %}"
        "{% for i in range(12) %}{% set ns.a=(ns.a,) %}{% endfor %}{{{ns.a:1}[ns.a]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1");
  }

  it("bounds repeated comparisons of shared containers") {
    check_equal(jinja_test_render("{% set ns=namespace(a=[0]) %}"
        "{% for i in range(20) %}{% set ns.a=[ns.a,ns.a] %}{% endfor %}{{ns.a==ns.a}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("bounds comparison depth built by a shallow loop") {
    check_equal(jinja_test_render("{% set ns=namespace(a=0) %}"
        "{% for i in range(70) %}{% set ns.a=[ns.a] %}{% endfor %}{{ns.a==ns.a}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("reports tuple key depth exhaustion as capacity") {
    check_equal(jinja_test_render("{% set ns=namespace(a=0) %}"
        "{% for i in range(70) %}{% set ns.a=(ns.a,) %}{% endfor %}{{{ns.a:1}}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("accepts the same tuple key after unrelated expressions are added") {
    check_equal(jinja_test_render("{% set unused=1+2 %}{% set ns=namespace(a=0) %}"
        "{% for i in range(12) %}{% set ns.a=(ns.a,) %}{% endfor %}{{{ns.a:1}[ns.a]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1");
  }

  it("charges the container and its equal leaf against exact visit limits") {
    options.max_value_visits = 1u;
    check_equal(jinja_test_render("{{[1]==[1]}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_value_visits = 2u;
    check_equal(jinja_test_render("{{[1]==[1]}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
  }

  it("charges identity matched leaves inside container aliases") {
    options.max_value_visits = 1u;
    check_equal(jinja_test_render("{% set a=[1] %}{{a==a}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("accumulates visits across output expressions and preserves the streaming prefix") {
    static const char source[] = "A{{user.age==1}}B{{user.age==2}}C";
    JinjaTestByteSink sink = {0};
    JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    options.max_value_visits = 1u;
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, &options, &renderer,
        &sink, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(sink.length, sizeof("ATrueB") - 1u);
    check_equal(sink.bytes, "ATrueB", sink.length);
    check_equal(error.offset, (size_t)(strstr(source, "{{user.age==2") - source));
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("does not reset the shared visit budget on macro entry") {
    options.max_value_visits = 1u;
    check_equal(jinja_test_render("{{user.age==1}}{% macro f() %}{{user.age==2}}{% endmacro %}{{f()}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("starts each render with a fresh visit budget") {
    enum { RENDER_COUNT = 3 };
    options.max_value_visits = 2u;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{[1]==[1]}}"), NULL, &error);
    check_not_null(templ);
    for (unsigned i = 0u; i < RENDER_COUNT; ++i) {
      check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options,
          &output, &error), JINJA_CMETA_OK);
      check_equal(output, "True");
      free(output);
      output = NULL;
    }
  }

  it("counts equal leaves in traversal depth") {
    options.max_value_depth = 1u;
    check_equal(jinja_test_render("{{[1]==[1]}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_value_depth = 2u;
    check_equal(jinja_test_render("{{[1]==[1]}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
  }

  it("bounds tuple key validation depth separately from expression recursion") {
    options.max_value_depth = 1u;
    check_equal(jinja_test_render("{{{(1,):'x'}|length}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_value_depth = 2u;
    check_equal(jinja_test_render("{{{(1,):'x'}|length}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1");
  }

  it("rejects a depth configuration beyond the supported stack bound before output") {
    options.max_value_depth = JINJA_CMETA_MAX_VALUE_DEPTH + 1u;
    check_equal(jinja_test_render("plain text", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_null(output);
    options.max_value_depth = UINT_MAX;
    check_equal(jinja_test_render("plain text", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_null(output);
  }

  it("accepts the largest visit budget without overflow") {
    options.max_value_visits = SIZE_MAX;
    check_equal(jinja_test_render("{{user.age==1}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
  }

  it("keeps control flow depth independent of value traversal depth") {
    options.max_value_depth = 1u;
    check_equal(jinja_test_render("{% if true %}{% if true %}{% if true %}X"
        "{% endif %}{% endif %}{% endif %}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "X");
  }

  it("keeps value traversal depth independent of control flow depth") {
    options.max_render_depth = 1u;
    options.max_value_depth = 4u;
    check_equal(jinja_test_render("{{[[[1]]]==[[[1]]]}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
  }

  it("does not charge comparisons in short circuited expressions") {
    options.max_value_visits = 1u;
    check_equal(jinja_test_render("{{false and ([1]==[1])}}|{{user.age==1}}|"
        "{{true or ([2]==[2])}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|True");
  }

  it("stops before an unneeded deep collection when an earlier element differs") {
    options.max_value_visits = 2u;
    options.max_value_depth = 2u;
    check_equal(jinja_test_render("{{[0,[1]]==[1,[2]]}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False");
  }

  it("bounds repeated validation of a shared tuple key graph") {
    options.max_value_visits = 2048u;
    check_equal(jinja_test_render("{% set ns=namespace(a=0) %}"
        "{% for i in range(12) %}{% set ns.a=(ns.a,ns.a) %}{% endfor %}{{{ns.a:1}}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("retains invalid borrowed metadata errors inside identical containers") {
    double number = NAN;
    cmeta_data_desc invalid = cmeta_data_double;
    cmeta_data_float_shape wrong_width = {32u};
    invalid.shape = &wrong_width;
    root.users = (JINJA_CMETA_SEQUENCE_VIEW){&number, 1u, sizeof(number), &invalid};
    options.max_value_visits = 2u;
    options.max_value_depth = 2u;
    check_equal(jinja_test_render("{% set a=[users[0]] %}{{a==a}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
  }

  it("shares the visit budget with repeated loop comparisons") {
    options.max_value_visits = 2u;
    check_equal(jinja_test_render("{% for x in [1,2,3] %}{{x==1}}{% endfor %}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("allows independent equal container graphs within the budget") {
    check_equal(jinja_test_render("{% set ns=namespace(a=[0],b=[0]) %}"
        "{% for i in range(12) %}{% set ns.a=[ns.a,ns.a] %}"
        "{% set ns.b=[ns.b,ns.b] %}{% endfor %}{{ns.a==ns.b}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
  }

  it("bounds independent equal container graphs as well as aliases") {
    options.max_value_visits = 4096u;
    check_equal(jinja_test_render("{% set ns=namespace(a=[0],b=[0]) %}"
        "{% for i in range(12) %}{% set ns.a=[ns.a,ns.a] %}"
        "{% set ns.b=[ns.b,ns.b] %}{% endfor %}{{ns.a==ns.b}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("retains unhashable key errors within the traversal budget") {
    options.max_value_visits = 2u;
    options.max_value_depth = 2u;
    check_equal(jinja_test_render("{{{([],):1}}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("does not leak failed render budgets into the next render") {
    templ = jinja_cmeta_compile(vstr_from_cstr("{{[1]==[1]}}"), NULL, &error);
    check_not_null(templ);
    options.max_value_visits = 1u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_value_visits = 2u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True");
  }

  it("counts the membership test root like the membership operator") {
    options.max_value_visits = 1u;
    check_equal(jinja_test_render("{{user.age is in [1]}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("charges string membership tests even without container elements") {
    options.max_value_visits = 1u;
    check_equal(jinja_test_render("{{user.name is in 'Ada'}}{{user.name is in 'Ada'}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("shares membership test root depth with its element probes") {
    options.max_value_depth = 1u;
    check_equal(jinja_test_render("{{user.age is in(seq=[1])}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("accepts the deepest tuple key and rejects one additional level") {
    check_equal(jinja_test_render("{% set ns=namespace(a=0) %}"
        "{% for i in range(64) %}{% set ns.a=(ns.a,) %}{% endfor %}{{{ns.a:1}[ns.a]}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% set ns=namespace(a=0) %}"
        "{% for i in range(63) %}{% set ns.a=(ns.a,) %}{% endfor %}{{{ns.a:1}[ns.a]}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1");
  }

  it("does not charge identity queries or comparisons already folded by the compiler") {
    options.max_value_visits = 1u;
    check_equal(jinja_test_render("{{user.age==1}}|{% set x=[1] %}"
        "{{x is sameas(x)}}|{{1==1}}", &model, &root, &options,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
  }
}
