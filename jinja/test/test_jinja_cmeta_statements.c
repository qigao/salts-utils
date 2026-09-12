#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta: statements 5") {
  it("filters block output in a local capture scope") {
    static const char *sources[] = {
        "{% filter trim %} x {% endfilter %}",
        "{% set x=1 %}{% filter trim %}{% set x=2 %} {{x}} {% endfilter %}{{x}}",
        "{% filter trim|escape %} <x> {% endfilter %}",
        "{% filter trim %} A{% filter trim %} B {% endfilter %}C {% endfilter %}",
        "{% autoescape true %}{% filter default('<',true) %}{% endfilter %}{% endautoescape %}",
        "{% filter default(x,true) %}{% set x='local' %}{% endfilter %}",
        "{% autoescape true %}{% set text %}{% filter default('<',true) %}{% endfilter %}{% endset %}{{text}}{% endautoescape %}"};
    static const char *expected[] = {"x", "21", "&lt;x&gt;", "ABC", "<", "local", "<"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("filter block case %zu", i);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
  }

  it("restores outer iteration and capture state after nested filter execution") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% set text %}{% for x in [1,2] %}"
        "{{loop.index}}:{% filter trim %}{% for y in [3,4] %}{{x}}{{y}}{% endfor %}"
        "{% filter trim %} {{x}} {% endfilter %}{% endfilter %}:{{loop.index}};"
        "{% endfor %}{% endset %}{{text}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:13141:1;2:23242:2;");
    free(output);
  }

  it("rejects noncallable filter and test results during execution") {
    static const char *sources[] = {"{{x|default()(1)}}", "{{x is defined()(1)}}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("prints separate expressions without flattening parenthesized tuples") {
    static const char *sources[] = {"A{% print %}B", "{% print 1,2 %}|{% print (1,2) %}",
        "{% print(1) %}|{% print[1,2] %}", "{% print 'a,b', (1,), (), 3 if false else 4 %}",
        "A \n{%- print 'x' -%}\n B"};
    static const char *expected[] = {"AB", "12|(1, 2)", "1|[1, 2]", "a,b(1,)()4", "AxB"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
  }

  it("prints with per expression safety and ordered side effects") {
    static const char *sources[] = {
        "{% autoescape true %}{% print '<'|safe, '>' %}{% endautoescape %}|{% print '<>' %}",
        "{% for x in [1,2] %}{% print loop.changed(x),loop.changed(x) %}{% endfor %}",
        "{% set text %}{% print 'a','b' %}{% endset %}{{text}}"};
    static const char *expected[] = {"<&gt;|<>", "TrueFalseTrueFalse", "ab"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
  }

  it("rejects malformed print lists and propagates expression errors") {
    static const char *sources[] = {"{% print 1, %}", "{% print ,1 %}", "{% print 1 2 %}", "{% print x=1 %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("ab{% print 1,1/0 %}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(error.offset, (size_t)2u);
    check_null(output);
  }

  it("evaluates do statements without formatting or emitting their results") {
    static const char *sources[] = {
        "A{% do 1, 'text', none, missing %}B", "A{% do(namespace(x=1)) %}B",
        "A \n{%- do '<'|escape -%}\n B", "A{% do[1,2] %}B",
        "A{% autoescape true %}{% do '<' %}{% endautoescape %}B"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, "AB");
      free(output);
    }
  }

  it("retains do side effects and respects branch short circuiting") {
    static const char *sources[] = {
        "{% set xs={'a':1,'b':2}|items %}{% do xs|first %}{{xs|list}}",
        "{% set xs={'a':1}|items %}{% do false and xs|list %}{{xs|list}}",
        "{% for x in [1,2] %}{% do loop.changed(x) %}{{loop.changed(x)}}{% endfor %}",
        "{% if false %}{% do 1/0 %}{% endif %}ok",
        "{% set text %}a{% filter trim %} b{% do 1 %} {% endfilter %}c{% endset %}{{text}}"};
    static const char *expected[] = {"[('b', 2)]", "[('a', 1)]", "FalseFalse", "ok", "abc"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
  }

  it("does not suppress expression failures in do statements") {
    static const char *sources[] = {"ab{% do 1/0 %}", "ab{% do 1,1/0 %}", "ab{% do missing.name %}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_equal(error.offset, (size_t)2u);
      check_null(output);
    }
  }

  it("does not materialize discarded do results but retains expression resource limits") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 1u;
    options.max_string_bytes = 1u;
    check_equal(jinja_test_render("{% do 42 %}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{% do 'a'~'b' %}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("rejects empty or malformed do expressions") {
    static const char *sources[] = {"{% do %}", "{% do 1+ %}", "{% do x=1 %}", "{% do (1,] %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("rejects continued lookup through missing path segments but allows missing leaves") {
    static const char *sources[] = {"{{missing.name}}", "{{user.missing.name}}",
        "{{user.age.missing.name}}", "{{missing.name|default('x')}}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{missing}}|{{user.missing}}|{{user.age.missing}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "||");
    free(output);
  }

  it("restores parent autoescape after switches inside a filter execution region") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% autoescape true %}{% filter trim %}"
        "{% autoescape false %}{{'<'}}{% autoescape true %}{{'>'}}{% endautoescape %}"
        "{% endautoescape %}{% endfilter %}|{{'<>'}}{% endautoescape %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<&gt;|&lt;&gt;");
    free(output);
  }

  it("bounds nested filter execution by the configured render depth") {
    /* Each filter also consumes two nodes of the compiled expression budget. */
    enum { FILTER_DEPTH = 32 };
    static const char opening[] = "{% filter trim %}";
    static const char closing[] = "{% endfilter %}";
    char source[FILTER_DEPTH * (sizeof(opening) + sizeof(closing) - 2u) + 2u];
    size_t length = 0u;
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < FILTER_DEPTH; ++i) {
      memcpy(source + length, opening, sizeof(opening) - 1u);
      length += sizeof(opening) - 1u;
    }
    source[length++] = 'x';
    for (size_t i = 0u; i < FILTER_DEPTH; ++i) {
      memcpy(source + length, closing, sizeof(closing) - 1u);
      length += sizeof(closing) - 1u;
    }
    source[length] = '\0';
    options.max_render_depth = FILTER_DEPTH - 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(error.offset, (size_t)(FILTER_DEPTH - 1u) * (sizeof(opening) - 1u));
    options.max_render_depth = FILTER_DEPTH;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "x");
    free(output);
  }

  it("preserves the failing instruction offset inside filter execution regions") {
    static const char *sources[] = {
        "{% filter trim %}{% filter trim %}{{1/0}}{% endfilter %}{% endfilter %}",
        "{% filter trim %}{% filter length %}x{% endfilter %}{% endfilter %}"};
    static const char *markers[] = {"{{1/0}}", "{% endfilter %}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
      check_equal(error.offset, (size_t)(strstr(sources[i], markers[i]) - sources[i]));
    }
  }

  it("rejects nonstring filter block results rather than implicitly formatting them") {
    static const char *sources[] = {"{% filter length %}abcd{% endfilter %}",
        "{% autoescape true %}{% filter length %}abcd{% endfilter %}{% endautoescape %}",
        "{% filter default([1],true) %}{% endfilter %}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("rejects invalid filter block headers and closers") {
    static const char *sources[] = {"{% filter %}{% endfilter %}",
        "{% filter |trim %}{% endfilter %}", "{% filter trim+1 %}{% endfilter %}",
        "{% filter trim %}{% endset %}", "{% endfilter %}", "{% filter trim %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      jinja_cmeta_release(templ);
    }
  }

  it("shares filter block capture budgets and retains immutable source") {
    char source[] = "{% filter trim %}abc{% endfilter %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    memset(source, '?', sizeof(source) - 1u);
    options.max_string_bytes = 2u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 3u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "abc");
    free(output);
    jinja_cmeta_release(templ);
  }
}
