#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta replacement") {
  it("replace rejects invalid binding after eager evaluation and retains recursion limits") {
    static const char *sources[] = {"{{'a'|replace}}", "{{'a'|replace('a')}}",
      "{{'a'|replace(new='x')}}", "{{'a'|replace('a','b',1,2)}}",
      "{{'a'|replace('a','b',old='a')}}", "{{'a'|replace('a','b',1,count=2)}}",
      "{{'a'|replace('a','b',other=1)}}", "{{'a'|replace('a','b','1')}}",
      "{{'a'|replace('a','b',1.0)}}", "{{'a'|replace('a','b',missing)}}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{'a'|replace(other=user.name|list)}}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% for x in [1] recursive %}{{loop([x])|replace('a','b')}}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("replace preserves borrowed NUL bytes and rejects malformed Unicode in every operand") {
    static const char nul_text[] = {'a', '\0', 'b'};
    JinjaTestRoot root = {{vstr_from_buf(nul_text, sizeof(nul_text)), 1}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{user.name|replace('\\x00','X',user.age)}}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "aXb");
    free(output);
    output = NULL;
    check_equal(jinja_test_render("{{user.name|replace('','-')}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    static const char expected[] = {'-', 'a', '-', '\0', '-', 'b', '-', '\0'};
    check_equal(memcmp(output, expected, sizeof(expected)), 0);
    free(output);
    output = NULL;
    static const unsigned char invalid[] = {'a', 0xffu};
    static const char *sources[] = {"{{user.name|replace('a','b',0)}}",
      "{{'a'|replace(user.name,'b',0)}}", "{{'a'|replace('x',user.name,0)}}"};
    root.user.name = vstr_from_buf((const char *)invalid, sizeof(invalid));
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
      check_null(output);
    }
  }

  it("replace checks cumulative result and escaped temporary byte budgets") {
    JinjaTestRoot root = {{vstr_from_cstr("abc"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render("{{'aa'|replace('a','bb')}}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "bbbb");
    free(output);
    output = NULL;
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render("{{'aa'|replace('a','bb')}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% set a='a'|replace('a','bb') %}{{a|replace('b','c')}}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{user.name|replace('a','',0)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{{'a'|replace(user.name,'',0)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{{'a'|replace('a',user.name,0)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render("{% autoescape true %}{{'<'|replace('<','x'|safe,0)}}{% endautoescape %}",
                                 &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("replace applies counts and Unicode boundaries with context dependent markup") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{'aaaaa'|replace('aa','x')}}|{{'aaaaa'|replace('aa','x',1)}}", "xxa|xaaa"},
      {"{{'甲😀乙'|replace('','-',2)}}|{{''|replace('','x')}}", "-甲-😀乙|x"},
      {"{{'aba'|replace(new='X',old='a',count=none)}}|{{'aaa'|replace('a','b',false)}}|{{'aaa'|replace('a','b',true)}}", "XbX|aaa|baa"},
      {"{{'aaa'|replace('a','b',-2)}}|{{'甲乙甲'|replace('甲','😀')}}", "bbb|😀乙😀"},
      {"{{none|replace('None',true)}}|{{12321|replace(2,none)}}|{{missing|replace('','x')}}", "True|1None3None1|x"},
      {"{% set r='<'|safe|replace('<','>') %}{{r}}|{{r is escaped}}", ">|False"},
      {"{% autoescape true %}{% set r='<'|replace('<','>') %}{{r}}|{{r is escaped}}{% endautoescape %}", "&gt;|False"},
      {"{% autoescape true %}{% set r='<'|safe|replace('<','>') %}{{r}}|{{r is escaped}}{% endautoescape %}", "&gt;|True"},
      {"{% autoescape true %}{% set r='<'|replace('<','>'|safe) %}{{r}}|{{r is escaped}}{% endautoescape %}", "&lt;|True"},
      {"{% autoescape true %}{% set r='<'|replace('&lt;'|safe,'>') %}{{r}}|{{r is escaped}}{% endautoescape %}", "&gt;|True"},
      {"{% autoescape true %}{% set r='<'|safe|replace('<'|safe,'>'|safe) %}{{r}}|{{r is escaped}}{% endautoescape %}", ">|True"},
      {"{% set r='甲😀乙'|replace('','-',0) %}{{r}}|{{'aaaa'|replace('aa','')}}", "甲😀乙|"},
      {"{% set r='a'|replace('a','b')|replace('b','<') %}{{r}}|{% filter replace('a','<') %}a{% endfilter %}", "<|<"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }
}

spec("Jinja CMeta collections and runtime: strings 7") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("renders plain single and double quoted string literals") {
    static const char source[] = "{{ 'Ada' }}|{{ \"Lin\" }}|{{ '' }}|{{ '<Ada>' | safe }}|"
                                 "{{ '\xe4\xbd\xa0\xe5\xa5\xbd' }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Ada|Lin||<Ada>|\xe4\xbd\xa0\xe5\xa5\xbd");
    free(output);
  }

  it("preserves filter separators inside string literal content") {
    static const char source[] = "{{ 'a|b' }}|{{ \"c|d\" | safe }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a|b|c|d");
    free(output);
  }

  it("leaves string literals unescaped by default") {
    static const char source[] = "{{ '<Ada>&' }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<Ada>&");
    free(output);
  }

  it("uses string literal truthiness in conditions") {
    static const char source[] = "{% if '' %}bad{% else %}empty{% endif %}:"
                                 "{% if 'x' %}text{% else %}bad{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "empty:text");
    free(output);
  }

  it("applies not to string literals") {
    static const char source[] = "{{ not '' }}|{{ not 'x' }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False");
    free(output);
  }

  it("copies string literal bytes into the compiled template") {
    char source[] = "{{ 'Ada' }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    check_not_null(templ);
    source[4] = 'X';
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "Ada");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("applies the render string byte limit to string literals") {
    static const char source[] = "{{ 'Ada' }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("charges string literal results to the render node budget") {
    static const char source[] = "{{ 'Ada' }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
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

  it("decodes newline and tab string escapes from the Jinja2Cpp scenario") {
    static const char source[] = "{{ 'Hello\\t\\nWorld\\n\\twith\\nescape\\tcharacters!' | safe }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Hello\t\nWorld\n\twith\nescape\tcharacters!");
    free(output);
  }

  it("decodes simple control quote and backslash escapes") {
    static const char source[] = "{{ '\\a\\b\\f\\r\\v\\\\\\\'\\\"' | safe }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "\a\b\f\r\v\\'\"");
    free(output);
  }

  it("keeps escaped quotes and filter separators inside string literals") {
    static const char source[] = "{{ 'Ada\\'s | value' | safe }}|{{ \"a\\\"b\" | safe }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "Ada's | value|a\"b");
    free(output);
  }

  it("applies render string limits to decoded string bytes") {
    static const char source[] = "{{ '\\n' }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_string_bytes = 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "\n");
    free(output);
  }

  it("decodes fixed-width hex and Unicode string escapes") {
    static const char source[] = "{{ '\\x41\\x80\\xff\\u00e9\\u4f60\\U0001f600' | safe }}";
    static const char expected[] = "A\xc2\x80\xc3\xbf\xc3\xa9\xe4\xbd\xa0\xf0\x9f\x98\x80";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
    free(output);
  }

  it("reports malformed fixed-width string escapes as syntax errors") {
    static const char *const sources[] = {"{{ '\\x4' }}", "{{ '\\xGG' }}", "{{ '\\u123' }}",
                                          "{{ '\\uZZZZ' }}", "{{ '\\U0001F60Z' }}"};
    size_t index;

    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      JINJA_CMETA_TEMPLATE *templ;

      info("malformed fixed-width escape: %s", sources[index]);
      templ = jinja_cmeta_compile(vstr_from_cstr(sources[index]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("rejects escaped surrogate and out-of-range Unicode values") {
    static const char *const sources[] = {"{{ '\\uD800' }}", "{{ '\\uDFFF' }}",
                                          "{{ '\\U0000D800' }}", "{{ '\\U00110000' }}"};
    size_t index;

    for (index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
      JINJA_CMETA_TEMPLATE *templ;

      info("invalid Unicode scalar escape: %s", sources[index]);
      templ = jinja_cmeta_compile(vstr_from_cstr(sources[index]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("preserves decoded NUL through the streaming renderer") {
    static const char source[] = "{{ '\\x00A' | safe }}";
    static const unsigned char expected[] = {0x00u, 0x41u};
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JinjaTestByteSink sink = {{0}, 0u};
    JINJA_CMETA_TEMPLATE *templ;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    if (templ != NULL) {
      check_equal(
          jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
          JINJA_CMETA_OK);
      check_equal(sink.length, sizeof(expected));
      check_equal(sink.bytes, expected, sizeof(expected));
    }
    jinja_cmeta_release(templ);
  }

  it("applies render byte limits to decoded non-BMP scalars") {
    static const char source[] = "{{ '\\U0001f600' | safe }}";
    static const char expected[] = "\xf0\x9f\x98\x80";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 37}, true, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, expected);
    free(output);
  }
}
