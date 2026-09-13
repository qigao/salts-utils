#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta collections and runtime: templates 10") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("recognizes not before a parenthesis without whitespace") {
    static const char source[] = "{% if not(active) %}inactive{% else %}active{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "inactive");
    free(output);
  }

  it("reports unbalanced condition parentheses as syntax errors") {
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr("{% if (active %}open{% endif %}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    check_equal(error.offset, (size_t)0u);
  }

  it("selects escaped or safe interpolation output") {
    static const char source[] =
        "{{ user.name }}|{{ user.name | safe }}|{{ user.name|escape }}|{{ user.name | e }}";
    JinjaTestRoot root = {{vstr_from_cstr("<Ada>"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<Ada>|<Ada>|&lt;Ada&gt;|&lt;Ada&gt;");
    free(output);
  }

  it("groups dotted paths before interpolation filters") {
    static const char source[] = "{{ (user.name) }}|{{ ((user.name)) | safe }}";
    JinjaTestRoot root = {{vstr_from_cstr("<Ada>"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<Ada>|<Ada>");
    free(output);
  }

  it("reports unbalanced interpolation parentheses as syntax errors") {
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{ (user.name }}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    check_equal(error.offset, (size_t)0u);
  }

  it("does not reparse literal delimiters joined by a trimmed comment") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{ {#- gap #}{ user.name }}", &model, &root, NULL,
                                   &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{{ user.name }}");
    free(output);
  }

  it("evaluates a sequence condition once without entering its elements") {
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{% if users %}yes{% else %}no{% endif %}",
                                   &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "yes");
    free(output);
  }

  it("trims whitespace around interpolation and comment tags") {
    static const char source[] = "A \n {{- user.name -}} \n {#- hidden -#} \tB";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "AAdaB");
    free(output);
  }

  it("trims whitespace around control statement tags") {
    static const char source[] = "A \n {%- if active -%} B {%- else -%} X {%- endif -%} C";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "ABC");
    free(output);
  }

  it("resolves the current loop alias in conditional paths") {
    static const char source[] =
        "{% for item in users %}{% if item.age %}{{ item.name }}{% endif %}"
        "{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "AdaLin");
    free(output);
  }

  it("accepts a semantic copy of the sequence descriptor") {
    static const char source[] = "{% for item in users %}{{ item.name }}{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 1u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    cmeta_data_desc sequence_copy = *jinja_cmeta_sequence_data();
    char *output = NULL;

    jinja_test_model_init(&model);
    model.root_fields[2].value = &sequence_copy;
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Ada");
    free(output);
  }

  it("rejects duplicate block declarations across all lexical scopes") {
    static const char *sources[] = {
      "{% block body %}{% endblock %}{% block body scoped %}{% endblock %}",
      "{% if false %}{% block body %}{% endblock %}{% endif %}{% block body scoped %}{% endblock %}",
      "{% block body %}{% block body scoped %}{% endblock %}{% endblock %}",
      "{% macro f() %}{% block body %}{% endblock %}{% endmacro %}{% block body scoped %}{% endblock %}",
      "{% block body required %}{% endblock %}{% block body scoped %}{% endblock %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("source: %s", sources[i]);
      jinja_test_compile_failure(vstr_from_cstr(sources[i]), NULL, JINJA_CMETA_ERR_SYNTAX,
          (size_t)(strstr(sources[i], "{% block body scoped") - sources[i]));
    }
  }

  it("reports the earliest duplicate block in source order rather than name order") {
    static const char source[] =
        "{% block z %}{% endblock %}{% block a %}{% endblock %}"
        "{% block z scoped %}{% endblock %}{% block a scoped %}{% endblock %}";
    jinja_test_compile_failure(vstr_from_cstr(source), NULL, JINJA_CMETA_ERR_SYNTAX,
        (size_t)(strstr(source, "{% block z scoped") - source));
  }

  it("checks duplicate Unicode block names using original byte offsets") {
    static const char source[] = "前{% block 名é %}{% endblock %}\r\n{% block 名é scoped %}{% endblock %}";
    jinja_test_compile_failure(vstr_from_cstr(source), NULL, JINJA_CMETA_ERR_SYNTAX,
        (size_t)(strstr(source, "{% block 名é scoped") - source));
  }

  it("does not merge distinct block names by prefix case or Unicode normalization") {
    static const char *sources[] = {
      "{% block b %}{% endblock %}{% block body %}{% endblock %}",
      "{% block body %}{% endblock %}{% block Body %}{% endblock %}",
      "{% block é %}{% endblock %}{% block é %}{% endblock %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("source: %s", sources[i]);
      JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, NULL);
      check_not_null(compiled);
      jinja_cmeta_release(compiled);
    }
  }

  it("checks duplicate blocks with custom delimiters and line statements") {
    static const char *sources[] = {
      "<% block name %><% endblock %><% block name scoped %><% endblock %>",
      "## block name\r\n## endblock\r\n## block name scoped\r\n## endblock\r\n"
    };
    static const char *duplicates[] = {"<% block name scoped", "## block name scoped"};
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    options.block_start_string = vstr_from_cstr("<%");
    options.block_end_string = vstr_from_cstr("%>");
    options.line_statement_prefix = vstr_from_cstr("##");
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      jinja_test_compile_failure(vstr_from_cstr(sources[i]), &options, JINJA_CMETA_ERR_SYNTAX,
          (size_t)(strstr(sources[i], duplicates[i]) - sources[i]));
    }
  }

  it("ignores block spelling in comments and raw text during duplicate checks") {
    static const char source[] =
        "{# {% block body %}{% endblock %} #}"
        "{% raw %}{% block body %}{% endblock %}{% endraw %}"
        "{% block body scoped %}{% endblock %}";
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_compile(vstr_from_cstr(source), NULL, NULL);
    check_not_null(compiled);
    jinja_cmeta_release(compiled);
  }

  it("checks duplicate blocks within the explicit source view only") {
    static const char source[] =
        "{% block b %}{% endblock %}{% block b scoped %}{% endblock %}{{ broken + }}";
    const size_t length = (size_t)(strstr(source, "{{ broken") - source);
    jinja_test_compile_failure(vstr_from_buf(source, length), NULL, JINJA_CMETA_ERR_SYNTAX,
        (size_t)(strstr(source, "{% block b scoped") - source));
  }

  it("finishes syntax parsing before checking duplicate blocks") {
    static const char source[] =
        "{% block b %}{% endblock %}{% block b %}{% endblock %}{{ 1 + }}";
    jinja_test_compile_failure(vstr_from_cstr(source), NULL, JINJA_CMETA_ERR_SYNTAX,
        (size_t)(strstr(source, "{{ 1 +") - source));
  }

  it("does not retain block declarations across failed compilations") {
    static const char duplicate[] =
        "{% block b %}{% endblock %}{% block b scoped %}{% endblock %}";
    enum { REPEATS = 3 };
    for (size_t i = 0u; i < REPEATS; ++i) {
      jinja_test_compile_failure(vstr_from_cstr(duplicate), NULL, JINJA_CMETA_ERR_SYNTAX,
          (size_t)(strstr(duplicate, "{% block b scoped") - duplicate));
      JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_compile(
          vstr_from_cstr("{% block b %}{% endblock %}"), NULL, NULL);
      check_not_null(compiled);
      jinja_cmeta_release(compiled);
    }
    check_null(jinja_cmeta_compile(vstr_from_cstr(duplicate), NULL, NULL));
  }

  it("accepts template-reference statements at compile time") {
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr("{% include 'child.html' %}"), NULL, &error);

    check_not_null(templ);
    jinja_cmeta_release(templ);

    templ = jinja_cmeta_compile(vstr_from_cstr("{% import 'module.html' as macros %}"), NULL, &error);
    check_not_null(templ);
    jinja_cmeta_release(templ);

    templ = jinja_cmeta_compile(vstr_from_cstr("{% from 'module.html' import helper as helper %}"), NULL, &error);
    check_not_null(templ);
    jinja_cmeta_release(templ);

    templ = jinja_cmeta_compile(vstr_from_cstr("{% extends 'base.html' %}"), NULL, &error);
    check_not_null(templ);
    jinja_cmeta_release(templ);
  }

  it("rejects malformed UTF-8 template input at the invalid byte") {
    static const struct {
      const char *name;
      const unsigned char bytes[7];
      size_t length;
    } cases[] = {{"overlong sequence", {'o', 'k', ' ', 0xc0u, 0xafu}, 5u},
                 {"truncated sequence", {'o', 'k', ' ', 0xe2u, 0x82u}, 5u},
                 {"UTF-16 surrogate", {'o', 'k', ' ', 0xedu, 0xa0u, 0x80u}, 6u},
                 {"above U+10FFFF", {'o', 'k', ' ', 0xf4u, 0x90u, 0x80u, 0x80u}, 7u}};
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ;

      info("malformed UTF-8 case: %s", cases[i].name);
      templ =
          jinja_cmeta_compile(vstr_from_buf((const char *)cases[i].bytes, cases[i].length), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      check_equal(error.offset, (size_t)3u);
    }
  }

  it("preserves valid UTF-8 text around Jinja tags") {
    static const char source[] = "\xe4\xbd\xa0\xe5\xa5\xbd {# \xe9\x9a\x90\xe8\x97\x8f #}"
                                 "{{ user.name }}";
    static const char expected[] = "\xe4\xbd\xa0\xe5\xa5\xbd Ada";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
    free(output);
  }

  it("preserves raw UTF-8 scalars and combining sequences") {
    static const char source[] = "\xc3\xa9|\xe4\xb8\xad|\xf0\x9f\x98\x80|e\xcc\x81|"
                                 "{{ '\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80"
                                 "e\xcc\x81' | safe }}";
    static const char expected[] = "\xc3\xa9|\xe4\xb8\xad|\xf0\x9f\x98\x80|e\xcc\x81|"
                                   "\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80"
                                   "e\xcc\x81";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
    free(output);
  }

  it("uses Unicode whitespace and lexical keyword boundaries for executable statements") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% if\u3000false %}bad{% elif\u00a0true %}yes{% endif %}", "yes"},
      {"{% if(true) %}yes{% endif %}", "yes"},
      {"{% set\u3000名=7 %}{{名}}", "7"},
      {"{% set(名)=8 %}{{名}}", "8"},
      {"{% set\u00a0名 %}值{% endset %}{{名}}", "值"},
      {"{% with\u3000名=9 %}{{名}}{% endwith %}|{{名}}", "9|"},
      {"{% with\u3000%}x{% endwith %}", "x"},
      {"{% filter\u3000trim %}  x  {% endfilter %}", "x"},
      {"{% autoescape\u3000true %}{{'<&>'}}{% endautoescape %}", "&lt;&amp;&gt;"},
      {"{% autoescape(true) %}{{'<&>'}}{% endautoescape %}", "&lt;&amp;&gt;"},
      {"{% print\u3000%}x{% do\u00a0(1) %}", "x"},
      {"{% print\u30001,2\u00a0%}", "12"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects missing statement arguments after Unicode whitespace") {
    static const char *sources[] = {"{% if\u3000%}", "{% set\u3000%}", "{% autoescape\u3000%}",
      "{% filter\u3000%}", "{% do\u3000%}", "{% print 1,\u3000%}",
      "{% with 名=1,\u3000%}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      check_equal(error.offset, (size_t)0u);
    }
  }

  it("does not execute Unicode extensions of builtin statement names") {
    static const char *sources[] = {"{% print名 %}", "{% do名 %}", "{% print\u0301 %}",
      "{% if名 true %}", "{% set名=1 %}", "{% with名=1 %}", "{% filter名 %}", "{% autoescape名 true %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_UNSUPPORTED);
    }
  }
}

spec("Jinja CMeta collections and runtime: templates 12") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("compiles Unicode lookup names and expression whitespace") {
    static const char unicode_identifier[] =
        "{{ \xe7\x94\xa8\xe6\x88\xb7.\xe5\x90\x8d\xe5\xad\x97 }}";
    static const char unicode_whitespace[] = "{{ 1\xc2\xa0==\xc2\xa0"
                                             "1 }}";
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(unicode_identifier), NULL, &error);

    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_cstr(unicode_whitespace), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
  }

  it("decodes adjacent strings separated by Unicode whitespace") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    char *output = NULL;
    check_equal(jinja_test_render("{{ 'a'\xe3\x80\x80'b' }}|{{ {'名字':'值'}.名字 }}|{{ 1\xc2\xa0== 1 }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "ab|值|True");
    free(output);
  }

  it("rejects unsupported interpolation filters") {
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr("{{ user.name | upper }}"), NULL, &error);

    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_cstr("{{ user.name | safe | unknown_filter }}"), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_UNSUPPORTED);
  }

  it("rejects an unavailable named test") {
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr("{% if active is users %}x{% endif %}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_UNSUPPORTED);

  }

  it("does not execute unregistered dotted names as their builtin prefix") {
    static const char *sources[] = {"{{ x is defined.custom }}", "{{ x|safe.custom }}",
        "{{ x|true }}", "{{ x is not custom }}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_UNSUPPORTED);
      jinja_cmeta_release(templ);
    }
  }

  it("renders unary not on borrowed paths and missing names") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ not user.name }}|{{ not active }}|{{ not missing }}|"
                                 "{{ not not user.age }}|{{ not not missing }}", &model, &root,
                                 NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|True|True|True|False");
    free(output);
  }

  it("rejects orphaned elif and elif after else") {
    JINJA_CMETA_TEMPLATE *templ =
        jinja_cmeta_compile(vstr_from_cstr("{% elif active %}x{% endif %}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(
        vstr_from_cstr("{% if active %}a{% else %}b{% elif user.age %}c{% endif %}"), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
  }

  it("bounds native instruction storage independently of expression storage") {
    static const char interpolation[] = "{{ user.name }}";
    JINJA_CMETA_TEMPLATE *templ;
    tstr source = tstr_new();
    size_t i;
    check_not_null(source);
    for (i = 0u; i < JINJA_CMETA_MAX_INSTRUCTIONS; ++i) {
      tstr next = tstr_cat_len(source, interpolation, sizeof(interpolation) - 1u);
      if (next == NULL) break;
      source = next;
    }
    check_equal(i, (size_t)JINJA_CMETA_MAX_INSTRUCTIONS);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
    {
      tstr next = tstr_cat_len(source, interpolation, sizeof(interpolation) - 1u);
      check_not_null(next);
      source = next;
    }
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);
  }

  it("enforces executed control depth and identifies the failing source tag") {
    static const char source[] = "{% if active %}{% if active %}yes{% endif %}{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    options.max_render_depth = 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(error.offset, sizeof("{% if active %}") - 1u);
    options.max_render_depth = 2u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "yes");
    free(output);
    options.max_render_depth = 1u;
    root.active = false;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "");
    free(output);
  }

  it("bounds the number of active conditional branches") {
    JINJA_CMETA_TEMPLATE *templ;
    tstr source = jinja_test_condition_chain(JINJA_CMETA_MAX_CONDITION_BRANCHES);

    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
    tstr_free(source);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    source = jinja_test_condition_chain(JINJA_CMETA_MAX_CONDITION_BRANCHES + 1u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);
  }

  it("bounds the number of compiled boolean expressions") {
    JINJA_CMETA_TEMPLATE *templ;
    tstr source = jinja_test_boolean_conditions(JINJA_CMETA_MAX_CONDITION_BRANCHES);

    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
    tstr_free(source);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    source = jinja_test_boolean_conditions(JINJA_CMETA_MAX_CONDITION_BRANCHES + 1u);
    check_not_null(source);
    templ = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(source);
  }

  it("rejects mismatched control blocks at compile time") {
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{% endif %}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    check_equal(error.offset, (size_t)0u);
  }

  it("fails when the render node budget is exhausted") {
    static const char source[] = "{{ user.name }}";
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

  it("preserves the legacy subset golden outputs") {
    typedef struct JinjaLegacyGoldenCase {
      const char *name;
      const char *source;
      const char *user_name;
      int user_age;
      bool active;
      size_t user_count;
      const char *expected;
    } JinjaLegacyGoldenCase;
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    static const JinjaLegacyGoldenCase cases[] = {
        {"plain multiline text", "Hello\nfrom Jinja", "Ada", 37, true, 2u, "Hello\nfrom Jinja"},
        {"inline comment", "before{# hidden #}after", "Ada", 37, true, 2u, "beforeafter"},
        {"commented template syntax",
         "before{# {{ user.name }} {% if active %}hidden{% endif %} #}after", "Ada", 37, true, 2u,
         "beforeafter"},
        {"default interpolation", "Hello {{ user.name }}", "<Ada>", 37, true, 2u,
         "Hello <Ada>"},
        {"safe interpolation", "{{ user.name | safe }}", "<Ada>", 37, true, 2u, "<Ada>"},
        {"conditional", "{% if active %}active{% else %}inactive{% endif %}", "Ada", 37, false, 2u,
         "inactive"},
        {"elif chain", "{% if active %}active{% elif user.age %}aged{% else %}empty{% endif %}",
         "Ada", 37, false, 2u, "aged"},
        {"negated condition", "{% if not active %}visible{% endif %}", "Ada", 37, false, 2u,
         "visible"},
        {"sequence loop", "{% for item in users %}[{{ item.name }}]{% endfor %}", "Ada", 37, true,
         2u, "[Ada][Lin]"},
        {"empty loop", "{% for item in users %}{{ item.name }}{% else %}empty{% endfor %}", "Ada",
         37, true, 0u, "empty"},
        {"whitespace trim", "A {{- user.name -}} B", "Ada", 37, true, 2u, "AAdaB"}};
    JinjaTestModel model;
    size_t i;

    jinja_test_model_init(&model);
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JinjaTestRoot root = {{vstr_from_cstr(cases[i].user_name), cases[i].user_age},
                            cases[i].active,
                            {cases[i].user_count == 0u ? NULL : users, cases[i].user_count,
                             sizeof(users[0]), &model.user_desc}};
      char *output = NULL;

      info("legacy golden case: %s", cases[i].name);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error),
                  JINJA_CMETA_OK);
      check_not_null(output);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }
}
