#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta: scopes 6") {
  it("initializes with bindings from the outer scope and restores it afterwards") {
    static const char *sources[] = {
        "{% set a=8 %}{% with a=1,b=a %}{{a}}|{{b}}{% endwith %}|{{a}}",
        "{% with %}{% set x=1 %}{{x}}{% endwith %}{{x is undefined}}",
        "{% with (a,b)=(1,2),c=3 %}{{a}}{{b}}{{c}}{% endwith %}",
        "{% set a=8 %}{% with a=1 %}{% with a=2,b=a %}{{a}}{{b}}{% endwith %}{{a}}{% endwith %}{{a}}",
        "{% set ns=namespace(x=1) %}{% with alias=ns %}{% set alias.x=2 %}{% endwith %}{{ns.x}}"};
    static const char *expected[] = {"1|8|8", "1True", "123", "2118", "2"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("with case %zu", i);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
  }

  it("combines with scopes with loops captures and autoescape") {
    static const char *sources[] = {
        "{% for i in [1] %}{% with i=2,loop=3 %}{{i}}{{loop}}{% endwith %}{{i}}{% endfor %}",
        "{% with a,a=(1,2),b='x,y=z' %}{{a}}{{b}}{% endwith %}",
        "{% with ()=() %}yes{% endwith %}",
        "{% with a=1,a=2 %}{{a}}{% endwith %}",
        "{% autoescape true %}{% with a='<'|safe,b='>' %}{% set text %}{{a}}{{b}}{% endset %}{{text}}{% endwith %}{% endautoescape %}",
        "{% set a=8 %}{% with a=1 %}{% if true %}{% set a=2 %}{% endif %}{{a}}{% endwith %}{{a}}"};
    static const char *expected[] = {"231", "2x,y=z", "yes", "2", "<&gt;", "28"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("with integration case %zu", i);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
  }

  it("does not call hidden loop methods through with bindings") {
    static const char *sources[] = {
        "{% for i in [1] %}{% with loop=3 %}{{loop.cycle(7)}}{% endwith %}{% endfor %}",
        "{% for i in [1] %}{% with loop=3 %}{{loop.changed(7)}}{% endwith %}{% endfor %}"};
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

  it("uses pinned structural loop discovery when a with target precedes loop reads") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% for i in [1] %}{% with loop=3 %}"
        "{{false and loop.cycle(7)}}{{false and loop.changed(7)}}{% endwith %}"
        "{{loop.cycle(7)}}{{loop.changed(7)}}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(jinja_test_render("{% for i in [1] %}{{loop.index}}{% with loop=3 %}"
        "{{false and loop.cycle(7)}}{{false and loop.changed(7)}}{% endwith %}"
        "{{loop.cycle(7)}}{{loop.changed(7)}}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1FalseFalse7True");
    free(output);
  }

  it("rejects malformed with headers and mismatched closers") {
    static const char *sources[] = {"{% with a=1, %}{% endwith %}", "{% with a %}{% endwith %}",
        "{% with ns.x=1 %}{% endwith %}", "{% with a= %}{% endwith %}",
        "{% with a=1,b %}{% endwith %}", "{% with true=1 %}{% endwith %}",
        "{% with a=1 %}{% endif %}", "{% endwith %}", "{% with %}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("with syntax case %zu", i);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_SYNTAX);
      check_null(output);
    }
  }

  it("stops with initialization on unpack errors before later expressions") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% with (a,b)=[1],c=9223372036854775807+1 %}unreachable{% endwith %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("owns with initializer source and recreates bindings for each render") {
    char source[] = "{% with name=user.name %}{{name}}{% set name='local' %}{% endwith %}{{name is undefined}}";
    JinjaTestRoot root = {{vstr_from_cstr("first"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    memset(source, '?', sizeof(source) - 1u);
    static const char *names[] = {"first", "second"};
    static const char *expected[] = {"firstTrue", "secondTrue"};
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
      char *output = NULL;
      root.user.name = vstr_from_cstr(names[i]);
      check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
    jinja_cmeta_release(templ);
  }

  it("bounds with local bindings and reuses slots after exit") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 1u;
    check_equal(jinja_test_render("{% with a=1,b=2 %}{% endwith %}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% with a=1 %}{% endwith %}{% with b=2 %}{% endwith %}",
        &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
  }

  it("shares namespace identity through aliases and loop scopes") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% set ns=namespace(x=1) %}{% set alias=ns %}"
        "{% for i in range(3) %}{% set alias.x=alias.x+1 %}{% endfor %}{{ ns.x }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "4");
    free(output);
  }

  it("reads keyword attribute names without changing following operators") {
    static const char *sources[] = {
        "{% set ns=namespace() %}{% set ns.true=7 %}{{ ns.true }}|{{ (ns).true }}",
        "{{ {'not':8}.not +1 }}|{{ {'not':8}.not-1 }}",
        "{{ {'none':9}.none }}|{{ {'False':4}.False }}",
        "{{ {'not':8}.not in [8] }}|{{ {'true':1}.true is integer }}",
        "{{ not false }}|{{ true }}|{{ none }}"};
    static const char *expected[] = {"7|7", "9|7", "9|4", "True|True", "True|True|None"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("keyword attribute case %zu", i);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
  }

  it("constructs namespace values with dictionary pair and keyword inputs") {
    static const char *sources[] = {
        "{% set ns=namespace({'x':1},x=2) %}{{ ns.x }}|{{ ns['x'] }}|{{ ns.missing is undefined }}",
        "{% set ns=namespace([('x',1),('x',2)]) %}{{ ns.x }}",
        "{% set ns=namespace() %}{{ ns is mapping }}|{{ ns is sequence }}|{{ ns is iterable }}|{{ ns is escaped }}|{{ ns == namespace() }}|{{ ns == ns }}|{{ ns }}",
        "{% set ns=namespace() %}{% set ns.self=ns %}{{ ns }}",
        "{% set ns=namespace({1:2}) %}{{ ns }}|{{ ns[1] is undefined }}",
        "{% set ns=namespace() %}{% set ns.x %}text{% endset %}{{ ns.x }}|{% set ns.x,y=1,2 %}{{ ns.x }}{{ y }}",
        "{% set ns=namespace('') %}{{ ns }}"};
    static const char *expected[] = {"2|2|True", "2", "False|False|False|False|False|True|<Namespace {}>",
        "<Namespace {'self': <Namespace {...}>}>", "<Namespace {1: 2}>|True", "text|12", "<Namespace {}>"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("namespace case %zu", i);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, expected[i]);
      free(output);
    }
  }

  it("rejects invalid namespace targets before evaluating assignment expressions") {
    static const char *sources[] = {
        "{% set ns={} %}{% set ns.x=9223372036854775807+1 %}",
        "{% set ns={} %}{% set a,ns.x=1,9223372036854775807+1 %}",
        "{% set user.age=1 %}", "{% set user.age %}1{% endset %}",
        "{% set ns=namespace(missing) %}", "{% set ns=namespace(1) %}",
        "{% set ns=namespace({}, {}) %}", "{% set ns=namespace([(1,2,3)]) %}",
        "{% set ns=namespace([([],1)]) %}", "{% set namespace=missing %}{{ namespace() }}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("namespace rejection %zu", i);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
      check_equal(root.user.age, 42);
    }
  }

  it("retains namespace identity through collection iteration and safe output") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% set ns=namespace(x='<') %}{% for item in [ns] %}"
        "{% set item.x='>' %}{% endfor %}{{ ns.x }}|{% autoescape true %}{{ ns }}{% endautoescape %}|"
        "{{ [ns,ns] }}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, ">|&lt;Namespace {&#39;x&#39;: &#39;&gt;&#39;}&gt;|[<Namespace {'x': '>'}>, <Namespace {'x': '>'}>]");
    free(output);
  }

  it("bounds namespace object and attribute storage") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 1u;
    check_equal(jinja_test_render("{% set ns=namespace() %}{% set ns=namespace() %}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = 2u;
    check_equal(jinja_test_render("{% set ns=namespace(a=1,b=2,c=3) %}",
        &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = 3u;
    check_equal(jinja_test_render("{% set ns=namespace(a=1,b=2) %}{% set ns.a=3 %}",
        &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
  }

  it("reads namespace attributes using borrowed CMeta string keys") {
    JinjaTestRoot root = {{vstr_from_cstr("x"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% set ns=namespace(x=7) %}{{ ns[user.name] }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7");
    free(output);
  }

  it("owns namespace keyword names and resets shared objects between renders") {
    char source[] = "{% set ns=namespace(x=0) %}{% set ns.x=ns.x+1 %}{{ ns.x }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    memset(source, '?', sizeof(source) - 1u);
    for (size_t i = 0u; i < 2u; ++i) {
      char *output = NULL;
      check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, "1");
      free(output);
    }
    jinja_cmeta_release(templ);
  }
}

spec("Jinja CMeta collections and runtime: scopes 8") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("assigns native values and shares assignments through if blocks") {
    static const char source[] =
        "{% set (x)=1 %}{% set x=x+2 %}{{ x }}|"
        "{% if true %}{% set x=4 %}{% endif %}{{ x }}|"
        "{% set x=missing %}{{ x is undefined }}|{% set x=none %}{{ x is none }}|"
        "{% set and=[user, 7] %}{{ and[0].age }}|{{ and[1] }}|"
        "{% set s='<b>'|safe %}{% autoescape true %}{{ s }}{% endautoescape %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    JINJA_CMETA_STATUS status = jinja_test_render(source, &model, &root, NULL, &output, &error);
    info("status %d offset %zu: %s", status, error.offset, error.message);
    check_equal(status, JINJA_CMETA_OK);
    check_equal(output, "3|4|True|True|42|7|<b>");
    free(output);
  }

  it("captures block output without leaking local assignments") {
    static const char source[] =
        "{% set x=9 %}{% set s %}{% set x=2 %}{{ x }}{% set inner %}I{% endset %}"
        "{{ inner }}{% endset %}{{ s }}|{{ x }}|{{ inner is undefined }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2I|9|True");
    free(output);
  }

  it("filters captures in their local scope and preserves autoescape safety") {
    static const char source[] =
        "{% set x=9 %}{% set y|default(x,true) %}{% set x=7 %}{% endset %}{{ y }}|{{ x }}|"
        "{% autoescape true %}{% set s %}<b>{{ '<' }}</b>{% endset %}{{ s }}|"
        "{{ s is escaped }}{% endautoescape %}|{% set n|length %}abc{% endset %}"
        "{{ n is integer }}:{{ n }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7|9|<b>&lt;</b>|True|True:3");
    free(output);
  }

  it("bounds aggregate captured bytes and retains the capacity error") {
    static const char *sources[] = {
        "{% set x %}abcd{% endset %}",
        "{% set x %}ab{% endset %}{% set y %}cd{% endset %}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    jinja_test_model_init(&model);
    options.max_string_bytes = 3u;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, &options, &output, &error),
                  JINJA_CMETA_ERR_CAPACITY);
      check_null(output);
    }
  }

  it("unpacks captured text and wraps filtered values when autoescape is enabled") {
    static const char source[] =
        "{% set a,b|trim %} ab {% endset %}{{ a }}{{ b }}|{% set c, %}C{% endset %}{{ c }}|"
        "{% autoescape true %}{% set n|length %}abc{% endset %}"
        "{{ n is string }}:{{ n is escaped }}:{{ n }}{% endautoescape %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "ab|C|True:True:3");
    free(output);
  }

  it("streams captured embedded NUL and Unicode bytes unchanged") {
    static const unsigned char expected[] = {'[', 'A', 0u, 0xf0u, 0x9fu, 0x98u, 0x80u, ']'};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    jinja_test_model_init(&model);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set x %}A{{ '\\x00\\U0001f600' }}{% endset %}[{{ x }}]"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_OK);
    check_equal(sink.length, sizeof(expected));
    check_equal(memcmp(sink.bytes, expected, sizeof(expected)), 0);
    jinja_cmeta_release(templ);
  }

  it("does not leak failed captures and resets all capture state between renders") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    jinja_test_model_init(&model);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "P{% set x %}secret{% if active %}{{ 1//0 }}{% endif %}{% endset %}{{ x }}Q"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_ERR_RENDER);
    check_equal(sink.length, (size_t)1u);
    check_equal(sink.bytes[0], (unsigned char)'P');
    root.active = false;
    sink.length = 0u;
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_OK);
    check_equal(sink.length, sizeof("PsecretQ") - 1u);
    check_equal(memcmp(sink.bytes, "PsecretQ", sink.length), 0);
    jinja_cmeta_release(templ);
  }

  it("bounds capture handles depth and exact byte admission") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    options.max_nodes = 1u;
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render("{% set x %}abc{% endset %}", &model, &root, &options,
                                  &output, &error), JINJA_CMETA_OK);
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{% set x %}{% endset %}{% set x %}{% endset %}",
                                  &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = JINJA_CMETA_DEFAULT_MAX_NODES;
    options.max_render_depth = 1u;
    check_equal(jinja_test_render("{% set x %}{% set y %}{% endset %}{% endset %}",
                                  &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("keeps capture values local to loop iterations and rejects filter failures without output") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(
        "{% set x='old' %}{% for i in [1,2] %}{% set x %}{{ i }}{% endset %}{{ x }}"
        "{% endfor %}|{{ x }}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "12|old");
    free(output);
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestByteSink sink = {{0}, 0u};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set x|length(1) %}secret{% endset %}leak"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
                JINJA_CMETA_ERR_RENDER);
    check_equal(sink.length, (size_t)0u);
    jinja_cmeta_release(templ);
  }

  it("rejects malformed capture headers and mismatched block delimiters") {
    static const char *sources[] = {
        "{% endset %}", "{% set x %}", "{% set x %}{% else %}{% endset %}",
        "{% set x %}{% endif %}", "{% if true %}{% endset %}",
        "{% set x|trim+1 %}{% endset %}", "{% set x,|trim %}{% endset %}",
        "{% set x| %}{% endset %}", "{% set x|trim( %}{% endset %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      info("capture source: %s", sources[i]);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      jinja_cmeta_release(templ);
    }
  }

  it("unpacks nested assignments after evaluating all right hand values") {
    static const char source[] =
        "{% set a,b=1,2 %}{% set a,b=b,a %}{{ a }}|{{ b }}|"
        "{% set (a,(b,c))=(3,(4,5)) %}{{ a }}{{ b }}{{ c }}|"
        "{% set a,a=6,7 %}{{ a }}|{% set (b,)=[8] %}{{ b }}|{% set ()=[] %}ok";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2|1|345|7|8|ok");
    free(output);
  }

  it("reports unpacking cardinality and noniterable errors at render time") {
    static const char *sources[] = {
        "{% set a,b=[1] %}", "{% set a,b=[1,2,3] %}", "{% set a,b=none %}",
        "{% set a,(b,c)=[1,[2]] %}", "{% set ()=[1] %}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("unpacks Unicode dictionary range and shared iterator values") {
    static const char source[] =
        "{% set a,b='\u4e2d\U0001f600'|safe %}{{ a }}{{ b }}|{{ a is escaped }}|"
        "{% set a,b={'x':1,'y':2,'x':3} %}{{ a }}{{ b }}|"
        "{% set a,b=range(2) %}{{ a }}{{ b }}|"
        "{% set it={'a':1,'b':2}|items %}{% set (k,v),(l,w)=it %}"
        "{{ k }}{{ v }}{{ l }}{{ w }}|{{ it|first is undefined }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "\u4e2d\U0001f600|False|xy|01|a1b2|True");
    free(output);
  }

  it("unpacks borrowed struct values without changing their owner") {
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% set a,b=users %}{% set a,b=b,a %}{{ a.age }}|{{ b.age }}",
                                  &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "42|37");
    check_equal(users[0].age, 37);
    free(output);
  }

  it("shares nested unpack iterator exhaustion and accepts empty Undefined") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(
        "{% set ()=missing %}{% set it={'x':1}|items %}{% set (a,),()=(it,it) %}{{ a }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "('x', 1)");
    free(output);
    output = NULL;
    check_equal(jinja_test_render(
        "{% set it={'x':1,'y':2}|items %}{% set (a,),(b,)=(it,it) %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }

  it("isolates unpacked bindings in loop and autoescape scopes") {
    static const char source[] =
        "{% set a,b=8,9 %}{% for i in [1,2] %}{% set (i,a)=[7,i] %}{{ i }}{{ a }}"
        "{% endfor %}|{{ a }}{{ b }}|{% autoescape false %}{% set a,b=1,2 %}"
        "{{ a }}{{ b }}{% endautoescape %}|{{ a }}{{ b }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7172|89|12|89");
    free(output);
  }

  it("bounds unpacked binding slots and counts repeated names once") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    options.max_nodes = 2u;
    check_equal(jinja_test_render("{% set a,b=1,2 %}{% set a,a=3,4 %}",
                                  &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{% set x=0 %}{% set a,b=1,2 %}",
                                  &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("rejects illegal tuple targets including nested loop names") {
    static const char *sources[] = {
        "{% set a,=[1] %}", "{% set [a,b]=[1,2] %}", "{% set a,true=1,2 %}",
        "{% set a,(not)=1,2 %}", "{% for i in [] %}{% set a,(loop,b)=1,(2,3) %}{% endfor %}",
        "{% for i in [] %}{% else %}{% set (loop,)=[] %}{% endfor %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      jinja_cmeta_release(templ);
    }
  }

  it("isolates assignments per loop iteration else and autoescape scope") {
    static const char source[] =
        "{% set x=9 %}{% for i in [1,2] %}{{ x }}{% set x=i %}{{ x }}"
        "{% set i=8 %}{{ i }}{% endfor %}|{{ x }}|"
        "{% for i in [] %}unused{% else %}{% set x=2 %}{{ x }}{% endfor %}|{{ x }}|"
        "{% autoescape false %}{% set x=3 %}{{ x }}{% endautoescape %}|{{ x }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    JINJA_CMETA_STATUS status = jinja_test_render(source, &model, &root, NULL, &output, &error);
    info("status %d offset %zu: %s", status, error.offset, error.message);
    check_equal(status, JINJA_CMETA_OK);
    check_equal(output, "918928|9|2|9|3|9");
    free(output);
  }

  it("constructs independent dictionaries from pairs mappings and keywords") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{dict()}}|{{dict([])}}|{{dict('')}}", "{}|{}|{}"},
      {"{{dict(名字='甲', count=2)}}", "{'名字': '甲', 'count': 2}"},
      {"{{dict([('a',1),('b',2),('a',3)], b=4, c=5)}}", "{'a': 3, 'b': 4, 'c': 5}"},
      {"{% set original={'a':1} %}{% set copy=dict(original,a=2) %}{{original}}|{{copy}}", "{'a': 1}|{'a': 2}"},
      {"{{dict(['ab','甲乙'])}}", "{'a': 'b', '甲': '乙'}"},
      {"{{dict([(true,'a'),(1,'b')])}}", "{True: 'b'}"},
      {"{{dict([((1,2),'tuple')])[(1,2)]}}", "tuple"},
      {"{% set it={'a':1,'b':2}|items %}{{dict(it)}}|{{it|list}}", "{'a': 1, 'b': 2}|[]"},
      {"{{dict(a=dict(b=2)).a.b}}|{{not dict()}}|{{dict(a=1) is mapping}}", "2|True|True"},
      {"{% set ns=namespace(n=1) %}{% set d=dict(x=ns) %}{% set ns.n=2 %}{{d.x.n}}", "2"},
      {"{% for k,v in dict(a=1,b=2)|items %}{{k}}{{v}}{% endfor %}", "a1b2"},
      {"{% set it={'a':1,'b':2}|items %}{{dict(first=it|first, second=it|first)}}|{{it|list}}", "{'first': ('a', 1), 'second': ('b', 2)}|[]"},
      {"{{namespace([(true,'a'),(1,'b')])}}", "<Namespace {True: 'b'}>"}
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

  it("rejects invalid dictionary construction without publishing partial output") {
    static const char *sources[] = {
      "{{dict(1)}}", "{{dict(none)}}", "{{dict(missing)}}", "{{dict([1])}}",
      "{{dict([(1,)])}}", "{{dict([(1,2,3)])}}", "{{dict([([],1)])}}",
      "{{dict({}, {})}}", "{% set dict=1 %}{{dict()}}", "{% set dict=missing %}{{dict()}}"
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

  it("evaluates dictionary arguments before rejecting arity or a shadowed builtin") {
    static const char *sources[] = {
      "{{dict({}, {}, value=user.name|list)}}",
      "{% set dict=1 %}{{dict(value=user.name|list)}}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_string_bytes = 2u;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, &options, &output, &error),
                  JINJA_CMETA_ERR_CAPACITY);
      check_null(output);
    }
  }

  it("bounds dictionary keys and recursive constructor evaluation") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    options.max_nodes = 1u;
    check_equal(jinja_test_render("{% do dict(a=1,b=2) %}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_nodes = 2u;
    check_equal(jinja_test_render("{% do dict(a=1,b=2) %}", &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
    output = NULL;
    check_equal(jinja_test_render(
        "{% for x in [1] recursive %}{{dict(a=loop([x]))}}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("retains assigned native collections ranges and shared iterator cursors") {
    static const char source[] =
        "{% set r=range(1,6,2) %}{{ r.start }}:{{ r[-1] }}|"
        "{% set d={'age':user.age} %}{{ d.age }}|"
        "{% set it={'a':1,'b':2}|items %}{% set alias=it %}"
        "{{ it|first }}|{{ alias|first }}|{{ it|first is undefined }}|"
        "{% set user=missing %}{{ user is undefined }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:5|42|('a', 1)|('b', 2)|True|True");
    check_equal(root.user.age, 42);
    free(output);
  }

  it("does not retain assignments between renders of the same template") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% if active %}{% set x=user %}{% endif %}{{ x|default('missing') is string }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "False");
    free(output);
    output = NULL;
    root.active = false;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("bounds binding slots and reuses overwritten or exited scope slots") {
    static const struct { const char *source; JINJA_CMETA_STATUS status; } cases[] = {
      {"{% set x=1 %}{% set x=2 %}", JINJA_CMETA_OK},
      {"{% set x=1 %}{% set y=2 %}", JINJA_CMETA_ERR_CAPACITY},
      {"{% autoescape false %}{% set x=1 %}{% endautoescape %}"
       "{% autoescape false %}{% set y=2 %}{% endautoescape %}", JINJA_CMETA_OK}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_nodes = 1u;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, &options, &output, &error),
                  cases[i].status);
      if (cases[i].status == JINJA_CMETA_OK) check_equal(output, "");
      else check_null(output);
      free(output);
    }
  }

  it("stops on a failing assignment RHS without leaking state into later renders") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set x=1 %}{% if active %}{% set x=1//0 %}{% endif %}{{ x }}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);
    root.active = false;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "1");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("uses implicit loop metadata ahead of an outer loop named binding") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{% set loop={'index':99} %}"
                                 "{% for i in [1,2] %}{{ loop.index }}{% endfor %}|{{ loop.index }}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "12|99");
    free(output);
  }

  it("rejects assignment to the special loop name inside any for frame") {
    static const char *sources[] = {
      "{% for i in [1] %}{% set loop=0 %}{% endfor %}",
      "{% for i in [] %}{% else %}{% set loop=0 %}{% endfor %}",
      "{% for i in [1] %}{% autoescape false %}{% set loop=0 %}{% endautoescape %}{% endfor %}",
      "{% for loop in [] %}{% endfor %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      jinja_cmeta_release(templ);
    }
  }

  it("does not restore builtin range when a variable shadows it with Undefined") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{% set range=missing %}{{ range(2)|list }}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }
}
