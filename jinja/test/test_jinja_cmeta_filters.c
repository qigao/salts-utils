#include "jinja_cmeta_test_support.h"
#include "jinja_cmeta_runtime.h"

spec("Jinja CMeta collections and runtime: filters 1") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("join projects values and preserves delimiter dependent markup safety") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{[1,2,3]|join}}|{{range(3)|join(d=':')}}", "123|0:1:2"},
      {"{{[]|join}}|{{missing|join}}|{{[none,true,false]|join('/')}}", "||None/True/False"},
      {"{{'甲😀乙'|join('-')}}|{{{'a':1,'b':2}|join(',')}}", "甲-😀-乙|a,b"},
      {"{{[{'a':['甲']},{'a':['乙']}]|join('-',attribute='a.0')}}", "甲-乙"},
      {"{{[[1,2],[3,4]]|join(attribute='١',d=':')}}|{{[{}]|join(attribute='missing')}}", "2:4|"},
      {"{% set it={'a':1,'b':2}|items %}{{it|join(',',1)}}|{{it|list}}|{{[1,2]|reverse|join}}", "1,2|[]|21"},
      {"{{['a','b']|join(none)}}|{{['a','b']|join(3)}}", "aNoneb|a3b"},
      {"{% set x=['<'|safe,'>']|join('&'|safe) %}{{x}}|{{x is escaped}}", "<&>|False"},
      {"{% autoescape true %}{{['<','>']|join('&')}}|{{(['<','>']|join('&')) is escaped}}{% endautoescape %}", "&lt;&amp;&gt;|False"},
      {"{% autoescape true %}{{['<'|safe,'>']|join('&')}}|{{(['<'|safe,'>']|join('&')) is escaped}}{% endautoescape %}", "<&amp;&gt;|True"},
      {"{% autoescape true %}{{['<','>']|join('&'|safe)}}|{{([]|join('&'|safe)) is escaped}}{% endautoescape %}", "&lt;&&gt;|True"},
      {"{% set a=[1,2]|join(':') %}{% set b=[a,3]|join('-') %}{{a}}|{{b}}", "1:2|1:2-3"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("join rejects invalid binding and bounds eager recursive arguments") {
    static const char *sources[] = {"{{1|join}}", "{{none|join}}", "{{[]|join(other=1)}}",
      "{{[]|join('',none,1)}}", "{{[]|join('',d=':')}}", "{{[{}]|join(attribute='a.b')}}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{[]|join(other=user.name|list)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% for x in [1] recursive %}{{loop([x])|join}}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("join validates borrowed UTF8 and preserves embedded NUL within budgets") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestUser users[] = {{vstr_from_cstr("甲"), 2}, {vstr_from_cstr("乙"), 3}};
    JinjaTestRoot root = {{vstr_from_cstr("name"), 0}, false,
                         {users, sizeof(users) / sizeof(users[0]), sizeof(users[0]), &model.user_desc}};
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{users|join('-',attribute=user.name)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "甲-乙");
    free(output);
    output = NULL;
    static const char nul_text[] = {'a', '\0', 'b'};
    root.user.name = vstr_from_buf(nul_text, sizeof(nul_text));
    check_equal(jinja_test_render("{{[user.name,'c']|join('-')}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a\0b-c", sizeof("a\0b-c"));
    free(output);
    output = NULL;
    static const unsigned char invalid[] = {'a', 0xffu};
    root.user.name = vstr_from_buf((const char *)invalid, sizeof(invalid));
    check_equal(jinja_test_render("{{[user.name]|join}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
    check_equal(jinja_test_render("{{[]|join(user.name)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
    options.max_nodes = 2u;
    check_equal(jinja_test_render("{{range(3)|join}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{{range(2)|join}}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "01");
    free(output);
    output = NULL;
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render("{{['a','b']|join('-')}}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a-b");
    free(output);
    output = NULL;
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{['a','b']|join('-')}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("sum folds values and projects nested attributes with a caller supplied start") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{[1,2,3]|sum}}|{{range(4)|sum()}}|{{[true,false,true]|sum}}", "6|6|2"},
      {"{{[]|sum}}|{{missing|sum}}|{{[]|sum(start=none)}}|{{[]|sum(start=true)}}", "0|0|None|True"},
      {"{{[1,2]|sum(none,10)}}|{{[1,2]|sum(start=10,attribute=none)}}", "13|13"},
      {"{{[{'价':2},{'价':3}]|sum(attribute='价')}}", "5"},
      {"{{[{'a':[2]},{'a':[3]}]|sum('a.0')}}|{{[[1,2],[3,4]]|sum(attribute=1)}}", "5|6"},
      {"{{[[1,2],[3,4]]|sum(attribute='١')}}|{{[{'':2},{'':3}]|sum('')}}", "6|5"},
      {"{% set it={'a':2,'b':3}|items %}{{it|sum(attribute=1)}}|{{it|list}}", "5|[]"},
      {"{{[1,2,3]|reverse|sum}}|{{{1:'a',2:'b'}|sum}}", "6|3"},
      {"{{[[1],[2,3]]|sum(start=[])}}|{{[(1,),(2,)]|sum(start=())}}", "[1, 2, 3]|(1, 2)"},
      {"{{[1.0e16,1.0,-1.0e16]|sum}}|{{[0.5,1,2]|sum}}", "1.0|3.5"},
      {"{{[]|sum(start=missing) is undefined}}|{{[]|sum(attribute='missing.path',start=2)}}", "True|2"},
#if LONG_MAX < INT64_MAX
      {"{{[1.0e16,1.0,-9007199254740993]|sum}}", "992800745259008.0"},
#else
      {"{{[1.0e16,1.0,-9007199254740993]|sum}}", "992800745259009.0"},
#endif
      {"{{[1.0e16,1.0,-1.0e16]|sum(start=true)}}", "0.0"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("sum rejects invalid binding types and missing projected values") {
    static const char *sources[] = {
      "{{1|sum}}", "{{none|sum}}", "{{['1']|sum}}", "{{[none]|sum}}",
      "{{[]|sum(start='')}}", "{{[1]|sum(start=none)}}", "{{[{}]|sum('missing')}}",
      "{{[]|sum(other=1)}}", "{{[]|sum(none,1,2)}}", "{{[]|sum(none,attribute=none)}}",
      "{{[[1]]|sum(start=())}}", "{{[1]|sum(start=missing)}}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("sum preserves borrowed fields and rejects overflow and invalid Unicode") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestUser users[] = {{vstr_from_cstr("甲"), 2}, {vstr_from_cstr("乙"), 3}};
    JinjaTestRoot root = {{vstr_from_cstr("age"), 0}, false,
                         {users, sizeof(users) / sizeof(users[0]), sizeof(users[0]), &model.user_desc}};
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{users|sum(attribute=user.name,start=10)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "15");
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{{[9223372036854775807,1]|sum}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{{[]|sum(attribute='9223372036854775808')}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = 2u;
    check_equal(jinja_test_render("{{range(3)|sum}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{{range(2)|sum}}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1");
    free(output);
    output = NULL;
    static const unsigned char invalid[] = {0xffu};
    root.user.name = vstr_from_buf((const char *)invalid, sizeof(invalid));
    check_equal(jinja_test_render("{{[]|sum(attribute=user.name)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
    static const unsigned char invalid_tail[] = {'a', 0xf0u, 0x9fu};
    root.user.name = vstr_from_buf((const char *)invalid_tail, sizeof(invalid_tail));
    check_equal(jinja_test_render("{{[]|sum(attribute=user.name)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
  }

  it("sum evaluates invalid arguments eagerly and bounds recursive operands") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{[]|sum(other=user.name|list)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% for x in [1] recursive %}{{loop([x])|sum}}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("sum admits attribute segments against remaining collection workspace") {
    enum { ATTRIBUTE_SEGMENTS = 8 };
    vstr root = vstr_from_cstr("");
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    options.max_nodes = 1u;
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_compile(
        vstr_from_cstr("{% do []|sum(attribute='a.b.c.d.e.f.g.h') %}"), NULL, &error);
    check_not_null(compiled);
    check_equal(jinja_cmeta_render_string(compiled, jinja_cmeta_vstr_data(), &root,
        &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
    output = NULL;
    JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
    check_not_null(config);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_VALUES,
        ATTRIBUTE_SEGMENTS - 1u, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_string_ex(compiled, jinja_cmeta_vstr_data(), &root,
        &options, config, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_VALUES,
        ATTRIBUTE_SEGMENTS, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_string_ex(compiled, jinja_cmeta_vstr_data(), &root,
        &options, config, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
    jinja_cmeta_runtime_config_destroy(config);
    jinja_cmeta_release(compiled);
  }

  it("reverse preserves string safety and consumes sequence aliases once") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{'A😀é'|reverse}}", "́e😀A"},
      {"{{''|reverse()}}|{{'甲乙'|reverse}}", "|乙甲"},
      {"{% set r=[1,2,3]|reverse %}{% set alias=r %}{{r|first}}|{{alias|list}}|{{r|list}}", "3|[2, 1]|[]"},
      {"{{(1,2,3)|reverse|list}}|{{range(1,6,2)|reverse|list}}", "[3, 2, 1]|[5, 3, 1]"},
      {"{{missing|reverse|list}}|{{missing|reverse is sequence}}", "[]|False"},
      {"{% set r={'a':1,'b':2}|items|reverse %}{{r}}|{{r|first}}|{{r|list}}", "[('b', 2), ('a', 1)]|('b', 2)|[('b', 2), ('a', 1)]"},
      {"{% set r=[1,2]|reverse %}{{r is sequence}}|{{r is iterable}}", "False|True"},
      {"{% autoescape true %}{{'<x>'|reverse}}|{{'<x>'|safe|reverse}}|{{('<x>'|safe|reverse) is escaped}}{% endautoescape %}", "&gt;x&lt;|>x<|True"},
      {"{{{'a':1,'b':2,'a':3}|reverse|list}}", "['b', 'a']"},
      {"{% for x in [1,2,3]|reverse %}{{loop.index}}:{{x}}:{{loop.last}};{% endfor %}", "1:3:False;2:2:False;3:1:True;"},
      {"{% set original=[1,2] %}{{original|reverse|reverse}}|{{original}}", "[1, 2]|[1, 2]"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("reverse rejects invalid inputs and evaluates arguments before arity errors") {
    static const char *sources[] = {
      "{{1|reverse}}", "{{none|reverse}}", "{{true|reverse}}",
      "{{'a'|reverse(1)}}", "{{[]|reverse(other=1)}}",
      "{{[1,2]|items|list}}", "{{([1,2]|reverse)|items|list}}",
      "{{[1,2]|reverse|length}}", "{{[1,2]|reverse|last}}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{[]|reverse(user.name|list)}}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("reverse bounds retained strings and validates borrowed Unicode") {
    static const unsigned char invalid[] = {0xf0u, 0x9fu};
    static const char embedded[] = {'A', '\0', 'B'};
    JinjaTestRoot root = {{vstr_from_buf(embedded, sizeof(embedded)), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{user.name|reverse}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "B\0A", sizeof(embedded));
    free(output);
    output = NULL;
    root.user.name = vstr_from_buf((const char *)invalid, sizeof(invalid));
    check_equal(jinja_test_render("{{user.name|reverse}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
    root.user.name = vstr_from_cstr("甲A");
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render("{{user.name|reverse}}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A甲");
    free(output);
    output = NULL;
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render("{{user.name|reverse}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 7u;
    check_equal(jinja_test_render("{{user.name|reverse|reverse}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("reverse bounds materialization and preserves borrowed sequence elements") {
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JinjaTestUser users[] = {{vstr_from_cstr("甲"), 1}, {vstr_from_cstr("乙"), 2}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false,
                         {users, sizeof(users) / sizeof(users[0]), sizeof(users[0]), &model.user_desc}};
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% for u in users|reverse %}{{u.name}}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "乙甲");
    free(output);
    output = NULL;
    options.max_nodes = 2u;
    check_equal(jinja_test_render("{% do range(3)|reverse %}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% do range(2)|reverse %}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
  }

  it("reverse retains recursive expression capacity checks") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    char *output = NULL;
    check_equal(jinja_test_render("{% for x in [1] recursive %}{{loop([x])|reverse}}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("center uses scalar width and Python odd padding placement") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "[{{ 'ab'|center(5) }}]|[{{ 'a'|center(width=4) }}]|"
        "[{{ '\\u4f60'|center(3) }}]|{{ 'e\\u0301'|center(3)|length }}|"
        "{{ 'x'|center|length }}|{{ 12|center(-1) }}|[{{ missing|center(true) }}]",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[  ab ]|[ a  ]|[ \xe4\xbd\xa0 ]|3|80|12|[ ]");
    free(output);
  }

  it("center rejects invalid widths and bounds padding bytes") {
    static const char *sources[] = {"{{ 'a'|center(1.5) }}", "{{ 'a'|center(1.0) }}", "{{ 'a'|center(none) }}",
        "{{ 'a'|center('5') }}", "{{ 'a'|center(4,5) }}", "{{ 'a'|center(chars='x') }}",
        "{{ 'a'|center(4,width=4) }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render("{{ '\\u4f60'|center(3) }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("center preserves retained strings and enforces cumulative padding capacity") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render("{{ 'a'|center(4) }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, " a  ");
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{{ 'a'|center(4)|center(5) }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{{ 'a'|center(4)|center(4) }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, " a  ");
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{{ 'x'|center(9223372036854775807) }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    root.user.name = vstr_from_buf("\x80", 1u);
    check_equal(jinja_test_render("{{ user.name|center(-1) }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_METADATA);
    check_null(output);
  }

  it("trim handles Unicode whitespace custom scalars and string conversion") {
    JinjaTestRoot root = {{vstr_from_cstr("  Ada  "), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{{ user.name|trim }}|{{ '\\u2003\\x1c X \\x1f\\u00a0'|trim }}|"
        "{{ '\\u4f60hi\\u4f60'|trim(chars='\\u4f60') }}|"
        "{{ 'xyhelloxy'|trim('xy') }}|{{ ' x '|trim('') }}|"
        "{{ 123|trim('13') }}|{{ missing|trim }}|{{ ' x '|trim(none) }}|"
        "{{ '\\U0001f600x\\U0001f600'|trim('\\U0001f600') }}|"
        "{{ '\\x00x\\x00'|trim('\\x00') }}|{{ ' \\u200b '|trim|length }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Ada|X|hi|hello| x |2||x|x|x|1");
    free(output);
  }

  it("trim rejects invalid parameter types arity and keywords") {
    static const char *sources[] = {"{{ 'a'|trim(1) }}", "{{ 'a'|trim('a','b') }}",
        "{{ 'a'|trim(other='a') }}", "{{ 'a'|trim('a',chars='a') }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("trim validates borrowed UTF-8 and obeys input byte limits") {
    JinjaTestRoot root = {{vstr_from_buf(" x\xff ", 4u), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ user.name|trim }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_METADATA);
    check_null(output);
    check_equal(jinja_test_render("{{ ''|trim(user.name) }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_METADATA);
    check_null(output);
    root.user.name = vstr_from_cstr("  x  ");
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render("{{ user.name|trim }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("items rejects consuming non-mappings and unsupported sequence operations") {
    static const char *sources[] = {"{{ 1|items|list }}", "{{ {}|items|length }}",
        "{{ {'a':1}|items|last }}", "{{ {}|items(1) }}", "{{ {}|items }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("items loop else retains the original entry decision after exhaustion") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pairs in [{'a':1,'b':2}|items] %}{{ pairs|first|safe }}|"
        "{% for pair in pairs|default %}{{ pair[0] }}{% else %}empty{% endfor %}|"
        "{{ pairs|list }}{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "('a', 1)|b|[]");
    free(output);
  }

  it("items enforces render capacity and starts each render with a fresh cursor") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ {'a':1,'b':2}|items|list|length }}"), NULL, &error);
    check_not_null(templ);
    options.max_nodes = 1u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = 2u;
    for (i = 0u; i < 2u; ++i) {
      check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
                  JINJA_CMETA_OK);
      check_equal(output, "2");
      free(output);
      output = NULL;
    }
    jinja_cmeta_release(templ);
  }

  it("items creates a fresh loop consumer without replaying an exhausted source") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pairs in [{'a':1}|items] %}{% for p in pairs %}{{ p[0] }}"
        "{% else %}E{% endfor %}|{% for q in pairs %}{{ q[0] }}{% else %}E"
        "{% endfor %}{% endfor %}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a|E");
    free(output);
  }

  it("items peeks for last without losing the next tuple") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
        "{% for pair in {'a':1,'b':2}|items %}{{ loop.first }}:{{ loop.last }}:"
        "{{ loop.last }}:{{ pair[0] }};{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True:False:False:a;False:True:True:b;");
    free(output);
  }

  it("items consumption errors stop streaming without discarding earlier bytes") {
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_TEMPLATE *templ;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_cstr("prefix{{ 1|items|first }}suffix"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_ERR_RENDER);
    check_equal(sink.length, sizeof("prefix") - 1u);
    check_equal(sink.bytes, "prefix", sizeof("prefix") - 1u);
    jinja_cmeta_release(templ);
  }

  it("Jinja owns explicit escaping before invoking the byte sink") {
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JinjaTestRoot root = {{vstr_from_cstr("\"'<&>"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_TEMPLATE *templ;
    static const char expected[] = "\"'<&>|&#34;&#39;&lt;&amp;&gt;";
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ user.name }}|{{ user.name|e }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_OK);
    check_equal(sink.length, sizeof(expected) - 1u);
    check_equal(sink.bytes, expected, sizeof(expected) - 1u);
    jinja_cmeta_release(templ);
  }

  it("string output bounds the final escaped bytes and discards partial results") {
    JinjaTestRoot root = {{vstr_from_cstr("<"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render("{{ user.name|e }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "&lt;");
    free(output);
    output = NULL;
    check_equal(jinja_test_render("x{{ user.name|e }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
  }

  it("materializes iterable values through the list filter") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
                    "{{ (1,2)|list }}|{{ range(3)|list }}|{{ ('中😀'|list)[1] }}|"
                    "{{ {'a':1,'b':2,'a':3}|list|length }}|{{ missing|list }}|{{ users|list }}",
                    &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "[1, 2]|[0, 1, 2]|😀|2|[]|[]");
    free(output);
  }

  it("bounds list materialization and rejects noniterable input") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 4u;
    check_equal(jinja_test_render("{{ range(5)|list }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ range(4)|list }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "[0, 1, 2, 3]");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ 1|list }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("binds default filter keyword arguments independently of source order") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
                    "{{ false|default(boolean=true, default_value='yes') }}|"
                    "{{ 0|d('zero', boolean=true) }}|{{ missing|default(boolean=true)|length }}",
                    &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "yes|zero|0");
    free(output);
  }

  it("rejects conflicting filter keywords and preserves keyword evaluation order") {
    static const char *const invalid[] = {"{{ missing|default(unknown=1) }}",
                                          "{{ missing|default(1,default_value=2) }}",
                                          "{{ [1]|length(value=1) }}"};
    static const char *const malformed[] = {"{{ missing|default(boolean=true, 'x') }}",
                                            "{{ missing|default(boolean=true, boolean=false) }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(invalid[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
    {
      char *output = NULL;
      check_equal(
          jinja_test_render("{% for item in [1] %}{{ false|default(boolean=loop.changed(1), "
                            "default_value=loop.changed(1)) }}{% endfor %}",
                            &model, &root, NULL, &output, &error),
          JINJA_CMETA_OK);
      check_equal(output, "False");
      free(output);
    }
  }

  it("converts values to strings before subsequent expression operations") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(
        jinja_test_render("{{ 123|string|length }}|{{ none|string }}|{{ missing|string|length }}|"
                          "{{ ([1,true]|string)[::-1] }}|{{ '<x>'|string }}|{{ user.age|string }}",
                          &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "3|None|0|]eurT ,1[|<x>|37");
    free(output);
  }

  it("computes absolute numeric values and rejects unrepresentable results") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), -37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(
        jinja_test_render("{{ -42|abs }}|{{ (-2.5)|abs() }}|{{ user.age|abs }}|{{ true|abs }}",
                          &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "42|2.5|37|1");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_test_render("{{ -9223372036854775808|abs }}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ '2'|abs }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("selects first and last values from sequences and Unicode strings") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ [1,2]|first }}|{{ (1,2)|last() }}|{{ '中😀'|last }}|"
                                  "{{ range(1,9,2)|last }}|{{ []|first|default('empty') }}|"
                                  "{{ ([user]|first).name }}",
                                  &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "1|2|😀|7|empty|Ada");
    free(output);
  }

  it("selects dictionary edge keys by unique insertion order and rejects invalid operands") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
                    "{{ {'a':1,'b':2,'a':3}|first }}|{{ {'a':1,'b':2,'a':3}|last }}|"
                    "{{ users|last|default('empty') }}|{{ missing|first|default('missing') }}",
                    &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "a|b|empty|missing");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ 1|first }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("applies default filter arguments eagerly and preserves selected types") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(
        jinja_test_render("{{ missing|default('fallback') }}|{{ 0|default('fallback') }}|"
                          "{{ 0|d('fallback',true) }}|{{ missing|default([1,2])|length() }}",
                          &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "fallback|0|fallback|2");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{% for item in [1] %}{{ 'kept'|default(loop.changed(1)) }}|{{ "
                                  "loop.changed(1) }}{% endfor %}",
                                  &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "kept|False");
    free(output);
  }

  it("validates filter arity and propagates unselected default argument errors") {
    static const char *const invalid[] = {"{{ 'a'|length(1) }}", "{{ missing|default(1,2,3) }}",
                                          "{{ 'kept'|default(1 / 0) }}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(invalid[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("evaluates length filters inside interpolation and conditions") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(
        jinja_test_render("{{ 'A中😀é'|length }}|{{ [1,2]|count }}|{{ {'a':1,'a':2}|length }}|"
                          "{{ range(7)|length }}|{% if user.name|length == 3 %}yes{% endif %}|{{ "
                          "missing|length }}",
                          &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "5|2|1|7|yes|0");
    free(output);
  }

  it("composes length filter results and rejects unsized values") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(
        jinja_test_render("{{ 1 + 'ab'|length }}|{{ 'ab'|count | safe }}|{{ users|length }}",
                          &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "3|2|0");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ 'ab'|length|length }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ range(-9223372036854775808,9223372036854775807)|length }}",
                                  &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("repeats Unicode strings with bounded integer counts") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ '中😀' * 2 }}|{{ 2 * user.name }}|{{ 'a' * -1 }}|"
                                  "{{ '' * 9223372036854775807 }}|{{ ('ab' * true)[::-1] }}",
                                  &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "中😀中😀|AdaAda|||ba");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ 'a' * 2.0 }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_test_render("{{ 'ab' * 9223372036854775807 }}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("repeats lists and tuples with integer counts on either side") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(
                    "{{ [1,2] * 2 }}|{{ 3 * (1,) }}|{{ [1] * -1 }}|{{ () * 9223372036854775807 }}|"
                    "{{ ([user] * true)[0].name }}",
                    &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "[1, 2, 1, 2]|(1, 1, 1)|[]|()|Ada");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ [1] * 2.0 }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ [1,2] * 9223372036854775807 }}", &model, &root, NULL, &output,
                                  &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("adds lists and tuples while preserving type and element order") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(
        jinja_test_render("{{ [1,2] + [3] }}|{{ (1,) + (2,3) }}|{{ ([] + [user])[0].name }}|"
                          "{{ ([1] + [2,3])[::-1] }}",
                          &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "[1, 2, 3]|(1, 2, 3)|Ada|[3, 2, 1]");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ [1] + (2,) }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("adds strings without the implicit conversions of tilde") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ user.name + '中😀' }}|{{ ('a' + 'bc')[::-1] }}", &model,
                                  &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "Ada中😀|cba");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ 'a' + 1 }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }
  it("maps full Unicode case while preserving markup") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% autoescape true %}{{'Straße ﬃ'|upper}}|{{'İ'|lower}}|"
        "{{'ΟΣ ΟΣΑ ΟΣ́'|lower}}|{{'<b>ß</b>'|safe|upper}}|{{'<b>ß</b>'|upper}}{% endautoescape %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "STRASSE FFI|i̇|ος οσα ος́|<B>SS</B>|&lt;B&gt;SS&lt;/B&gt;");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{'a'|upper(1)}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }
  it("capitalizes Unicode titlecase while preserving final sigma and markup") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{'foo BAR'|capitalize}}|{{'ǳABC'|capitalize}}|{{'ßA'|capitalize}}|"
        "{{'ΑΣ'|capitalize}}|{% autoescape true %}{{'<B>Ä</B>'|safe|capitalize}}|"
        "{{'<B>Ä</B>'|capitalize}}{% endautoescape %}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "Foo bar|ǲabc|Ssa|Ας|<b>ä</b>|&lt;b&gt;ä&lt;/b&gt;");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{'a'|capitalize(1)}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }
  it("titles words with Jinja separators and preserves markup") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{'foo-bar (BAZ){QUX}[QUUX]<CORGE> GRAULT'|title}}|"
        "{{'ǳABC'|title}}|{{'ßA'|title}}|{{'ΑΣ-Σ'|title}}|{% autoescape true %}"
        "{{'<B>Ä</B>'|safe|title}}|{{'<B>Ä</B>'|title}}{% endautoescape %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Foo-Bar (Baz){Qux}[Quux]<Corge> Grault|Ǳabc|SSa|Ας-Σ|<B>ä</b>|&lt;B&gt;ä&lt;/b&gt;");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{'a'|title(1)}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("selects min and max with casefolded and projected comparison keys") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{[3,1,2]|min}}|{{[3,1,2]|max}}|{{['b','A']|min}}|"
        "{{['a','B']|max}}|{{['a','B']|min(case_sensitive=true)}}|"
        "{{([{'name':'z','score':3},{'name':'a','score':1}]|min(attribute='score')).name}}|"
        "{{([{'name':'z','score':3},{'name':'a','score':1}]|max(attribute='score')).name}}|"
        "{{[]|min is undefined}}|{% autoescape true %}{{['<x>'|safe,'z']|min}}|"
        "{{['<x>','z']|min}}{% endautoescape %}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "1|3|A|B|B|a|z|True|<x>|&lt;x&gt;");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{1|min}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{[1,'a']|max}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{[1]|min(1,case_sensitive=true)}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("sorts stably by casefolded multi-attribute keys") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{[3,1,2]|sort|join}}|{{[3,1,2]|sort(reverse=true)|join}}|"
        "{{['b','A']|sort|join}}|{{['a','B']|sort(case_sensitive=true)|join}}|"
        "{{[{'name':'z','age':2},{'name':'a','age':1},{'name':'b','age':1}]|"
        "sort(attribute='age,name')|map(attribute='name')|join}}|{% autoescape true %}"
        "{{['z','<x>'|safe]|sort|join}}|{{['z','<x>']|sort|join}}{% endautoescape %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "123|321|Ab|Ba|abz|<x>z|&lt;x&gt;z");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{[1,'a']|sort}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{[1]|sort(1,reverse=true)}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("sorts dictionary pairs by casefolded keys or values") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% for key,value in {'b':2,'A':3,'c':1}|dictsort %}{{key}}{{value}}{% endfor %}|"
        "{% for key,value in {'b':2,'A':3,'c':1}|dictsort(reverse=true) %}{{key}}{{value}}{% endfor %}|"
        "{{{'b':2,'a':3,'c':1}|dictsort(by='value')|map(attribute=0)|join}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A3b2c1|c1b2A3|cba");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{[1]|dictsort}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{{}|dictsort(by='other')}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("yields unique hashable values lazily with optional attributes") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% set u=['a','A','b','a']|unique %}{{u is sequence}}|{{u|join}}|"
        "{{u|list|length}}|{{[{'name':'B'},{'name':'b'},{'name':'a'}]|unique(attribute='name')|"
        "map(attribute='name')|join}}|{{[{'name':'B'},{'name':'b'},{'name':'a'}]|"
        "unique(case_sensitive=true,attribute='name')|map(attribute='name')|join}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|ab|0|Ba|Bba");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{[[1],[1]]|unique|list}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("groups stably by named or indexed attributes") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% for group in [{'city':'NY','name':'C'},{'city':'CA','name':'A'},"
        "{'city':'ca','name':'B'}]|groupby('city') %}{{group.grouper}}:{{group.list|"
        "map(attribute='name')|join}}:{{group[0]}}:{{group[1]|length}};{% endfor %}|"
        "{% for city, users in [{'city':'NY'},{'city':'CA'}]|groupby('city') %}{{city}}{{users|length}};"
        "{% endfor %}|{% for group in [{},{'city':'CA'}]|groupby('city',default='NY') %}"
        "{{group.grouper}};{% endfor %}|{{['A','a']|groupby(0)|length}}|"
        "{{['A','a']|groupby(0,case_sensitive=true)|length}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "CA:AB:CA:2;NY:C:NY:1;|CA1;NY1;|CA;NY;|1|2");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{[1]|groupby}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("pretty prints values and selects only from finite sequences") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{{'b':[2,1],'a':true}|pprint}}|{{('x',)|pprint}}|"
        "{{(['a','b','c']|random) in ['a','b','c']}}|{{range(1)|random}}|"
        "{{[]|random is undefined}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{'b': [2, 1], 'a': True}|('x',)|True|0|True");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{range(2)|map('string')|random}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("strips markup comments and normalizes decoded HTML text") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{' Main &raquo;\\t<!-- <b> hidden </b> --> <em>About</em> &amp; &#x41;'|striptags}}|"
        "{% autoescape true %}{{'<b>&lt;x&gt;</b>'|safe|striptags}}{% endautoescape %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Main » About & A|&lt;x&gt;");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{'x'|striptags(1)}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("urlizes escaped text links mailboxes and configured schemes") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% autoescape true %}{{'see (www.example.com/a?x=1&y=2), mail@example.com, <b>'|"
        "urlize(nofollow=true,target='_blank',rel='tag')}}{% endautoescape %}|"
        "{{'http://example.com/abcdef'|urlize(10)}}|"
        "{{'ftp://example.com'|urlize(extra_schemes=['ftp://'])}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "see (<a href=\"https://www.example.com/a?x=1&amp;y=2\" rel=\"nofollow tag\" target=\"_blank\">"
        "www.example.com/a?x=1&amp;y=2</a>), <a href=\"mailto:mail@example.com\">mail@example.com</a>, "
        "&lt;b&gt;|<a href=\"http://example.com/abcdef\">http://exa...</a>|"
        "<a href=\"ftp://example.com\">ftp://example.com</a>");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{'ftp://example.com'|urlize(extra_schemes=['bad'])}}", &model, &root,
        NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("serializes literals and reflected values as HTML-safe JSON") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{{'b':'<&>','a':[true,none,1.0,'甲😀']}|tojson}}|"
        "{% autoescape true %}{{{'x':'<'}|tojson}}|{{{'x':'<'}|tojson is escaped}}{% endautoescape %}|"
        "{{{'a':[1,2]}|tojson(indent=2)}}|{{user|tojson}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{\"a\": [true, null, 1.0, \"\\u7532\\ud83d\\ude00\"], \"b\": \"\\u003c\\u0026\\u003e\"}|"
        "{\"x\": \"\\u003c\"}|True|{\n  \"a\": [\n    1,\n    2\n  ]\n}|"
        "{\"name\": \"Ada\", \"age\": 37}");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{[]|tojson(indent='x')}}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }
}
