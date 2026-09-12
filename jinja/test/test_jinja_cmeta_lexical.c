#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta: lexical 2") {
  it("rejects unfinished quoted tags and mismatched brackets without overreading") {
    static const char *const cases[] = {
      "ab{{ '}}'", "ab{{ '}} }}", "ab{{ 'x\\", "ab{{ [1) }}", "ab{% if ('%}' %}"
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      size_t length = strlen(cases[i]);
      char *source = (char *)malloc(length);
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ;
      check_not_null(source);
      memcpy(source, cases[i], length);
      templ = jinja_cmeta_compile(vstr_from_buf(source, length), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      check_equal(error.offset, (size_t)2u);
      free(source);
    }
  }

  it("ignores tag delimiters inside quoted expressions but not comments") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ '}}' }}", "}}"},
      {"{{ {'x':1}}}", "{'x': 1}"},
      {"{{ {'x':{'y':2}}}}", "{'x': {'y': 2}}"},
      {"{{ \"}}\" }}", "}}"},
      {"{{ 'a\\\'}}b' }}", "a'}}b"},
      {"{{ 'a\\\\' }}B", "a\\B"},
      {"{{ '{% raw %}{{ x }}{% endraw %}' }}", "{% raw %}{{ x }}{% endraw %}"},
      {"{% if '%}' == '%}' %}yes{% endif %}", "yes"},
      {"{% for x in ['%}', '}}'] %}{{ x }}{% endfor %}", "%}}}"},
      {"{# ' #}X", "X"},
      {"{# \" #}X", "X"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("quoted delimiter: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("accepts exact-length nonterminated buffers without lexer sentinel reads") {
    static const struct { const char *source; JINJA_CMETA_STATUS status; } cases[] = {
      {"{% raw %}x", JINJA_CMETA_ERR_SYNTAX},
      {"{% raw %}{", JINJA_CMETA_ERR_SYNTAX},
      {"{% raw %}x{% endraw %}", JINJA_CMETA_OK},
      {"plain", JINJA_CMETA_OK},
      {"{", JINJA_CMETA_OK}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      size_t length = strlen(cases[i].source);
      char *source = (char *)malloc(length);
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ;
      check_not_null(source);
      memcpy(source, cases[i].source, length);
      templ = jinja_cmeta_compile(vstr_from_buf(source, length), NULL, &error);
      check_equal(error.status, cases[i].status);
      if (cases[i].status == JINJA_CMETA_OK) check_not_null(templ);
      else check_null(templ);
      jinja_cmeta_release(templ);
      free(source);
    }
  }

  it("normalizes physical template newlines and removes one final newline by default") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"A\r\nB\rC\n", "A\nB\nC"},
      {"A\n\n", "A\n"},
      {"{% raw %}A\rB{% endraw %}\r\n", "A\nB"},
      {"{{ 'A\r\nB' }}\n", "A\nB"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
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

  it("applies compile newline policy before decoding escapes without changing runtime data") {
    static const struct { const char *newline; const char *expected; } cases[] = {
      {"\n", "T\nA\nB|\n|\n|False|AB|R\r\nS\n"},
      {"\r\n", "T\r\nA\r\nB|\n|\n|True|A\\\r\nB|R\r\nS\r\n"},
      {"\r", "T\rA\rB|\n|\n|False|A\\\rB|R\r\nS\r"}
    };
    const char *source = "T\r\n{{ 'A\rB' }}|{{ '\\n' }}|{{ '\\N{LF}' }}|"
        "{{ 'x\r' == 'x\\r\\n' }}|{{ 'A\\\r\nB' }}|{{ user.name }}\n";
    JinjaTestRoot root = {{vstr_from_cstr("R\r\nS"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
      options.keep_trailing_newline = 1;
      options.newline_sequence = vstr_from_cstr(cases[i].newline);
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), &options, &error);
      check_not_null(templ);
      char *output = NULL;
      check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
      jinja_cmeta_release(templ);
    }
  }

  it("compiles custom delimiters and line prefixes using independent options") {
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    options.variable_start_string = vstr_from_cstr("[[");
    options.variable_end_string = vstr_from_cstr("]]");
    options.block_start_string = vstr_from_cstr("<%");
    options.block_end_string = vstr_from_cstr("%>");
    options.comment_start_string = vstr_from_cstr("<#");
    options.comment_end_string = vstr_from_cstr("#>");
    options.line_statement_prefix = vstr_from_cstr("#");
    options.line_comment_prefix = vstr_from_cstr("##");
    options.trim_blocks = 1;
    options.lstrip_blocks = 1;
    const char *source = "  # if true:\r\nA[[ 1 ]]<# hidden #>\r\n"
        "  <% raw %>[[ untouched ]]<% endraw %>\r\n  # endif\r\n"
        "Z ## removed\r\n{{ unchanged }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), &options, &error);
    check_not_null(templ);
    char *output = NULL;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A1[[ untouched ]]Z\n{{ unchanged }}");
    free(output);
    jinja_cmeta_release(templ);
    output = NULL;
    check_equal(jinja_test_render("[[ 1 ]]{{ 2 }}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[ 1 ]]2");
    free(output);
  }

  it("applies block whitespace policies only to their adjacent literal spans") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"A\n  {% if true %}\n  X\n  {% endif %}\nZ", "A\n  X\nZ"},
      {"A\n  {%+ if true +%}\n  X\n  {%+ endif +%}\nZ", "A\n  \n  X\n  \nZ"},
      {"A\n  {{ 1 }}\nZ", "A\n  1\nZ"},
      {"A {# c #}  {#- d #} B", "A  B"},
      {"A\n  {% raw %}\n X\n  {% endraw %}\nZ", "A\n\n X\nZ"},
      {"A\n  # if true:\n X\n  # endif\nZ", "A\n X\nZ"}
    };
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    options.trim_blocks = options.lstrip_blocks = 1;
    options.line_statement_prefix = vstr_from_cstr("#");
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(cases[i].source), &options, &error);
      check_not_null(templ);
      char *output = NULL;
      check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
      jinja_cmeta_release(templ);
    }
  }

  it("consumes right whitespace before recognizing the next delimiter or line prefix") {
    static const struct { const char *source; const char *opening; const char *expected; } cases[] = {
      {"  {{ 1 -}}  {{ 2 }}", "  {{", "1{{ 2 }}"},
      {"{{ 1 -}}\n  # set y=1\n{{y}}", "{{", "1# set y=1\n"},
      {"{% raw %}x{% endraw -%}\n  # set y=1\n{{y}}", "{{", "x# set y=1\n"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
      options.variable_start_string = vstr_from_cstr(cases[i].opening);
      options.line_statement_prefix = vstr_from_cstr("#");
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(cases[i].source), &options, &error);
      check_not_null(templ);
      char *output = NULL;
      check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
      jinja_cmeta_release(templ);
    }
  }

  it("projects syntax errors after consuming configured lexical whitespace") {
    static const struct { const char *source; const char *opening; int trim; } cases[] = {
      {"{% set x=1 -%} {{ unclosed\n{% if %}", " {{", 0},
      {"{% set x=1 %}\n{{ unclosed\n{% if %}", "\n{{", 1},
      {"{% set x=1 -%} {{ unclosed\n{% if %}\r\n", " {{", 0}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
      options.variable_start_string = vstr_from_cstr(cases[i].opening);
      options.trim_blocks = cases[i].trim;
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      check_null(jinja_cmeta_compile(vstr_from_cstr(cases[i].source), &options, &error));
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      check_equal(error.offset, (size_t)(strstr(cases[i].source, "{% if %}") - cases[i].source));
    }
  }

  it("owns configured literal output after source and option storage changes") {
    char source[] = "[[ 'A\nB' ]]\n";
    char opening[] = "[[", closing[] = "]]", newline[] = "\r\n";
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    options.variable_start_string = vstr_from_buf(opening, sizeof(opening) - 1u);
    options.variable_end_string = vstr_from_buf(closing, sizeof(closing) - 1u);
    options.newline_sequence = vstr_from_buf(newline, sizeof(newline) - 1u);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), &options, &error);
    check_not_null(templ);
    memset(source, 'x', sizeof(source));
    memset(opening, 'x', sizeof(opening));
    memset(closing, 'x', sizeof(closing));
    memset(newline, 'x', sizeof(newline));
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    char *output = NULL;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A\r\nB");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("retains original byte error offsets with custom delimiters and CRLF source") {
    const char source[] = {'A', '\r', '\n', '[', '[', ' ', '(', ' ', ']', ']'};
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    options.variable_start_string = vstr_from_cstr("[[");
    options.variable_end_string = vstr_from_cstr("]]");
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    check_null(jinja_cmeta_compile(vstr_from_buf(source, sizeof(source)), &options, &error));
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    check_equal(error.offset, (size_t)3u);
  }

  it("rejects invalid compile policies before publishing a template") {
    JINJA_CMETA_COMPILE_OPTIONS cases[6];
    const JINJA_CMETA_COMPILE_OPTIONS defaults = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) cases[i] = defaults;
    cases[0].trim_blocks = 2;
    cases[1].lstrip_blocks = -1;
    cases[2].keep_trailing_newline = 2;
    cases[3].newline_sequence = vstr_from_cstr("\n\n");
    cases[4].variable_start_string = cases[4].block_start_string;
    cases[5].comment_end_string = vstr_from_cstr("");
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("text"), &cases[i], &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_INVALID_ARGUMENT);
    }
  }

  it("uses shared tag whitespace controls for statements comments and Unicode text") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"A{%+ if true +%}B{%+ endif +%}C", "ABC"},
      {"A\u00a0{{- 1 -}}\u00a0B", "A1B"},
      {"A\u00a0{#- comment -#}\u00a0B", "AB"},
      {"{%+ raw %}{{bad}}{% endraw +%}", "{{bad}}"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      if (output != NULL) check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("compiles more raw segments than the private whole tree node budget") {
    enum { SEGMENTS = 300 };
    static const char part[] = "{% raw %}x{% endraw %}";
    char source[SEGMENTS * (sizeof(part) - 1u)];
    for (size_t i = 0u; i < SEGMENTS; ++i) memcpy(source + i * (sizeof(part) - 1u), part, sizeof(part) - 1u);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source)), NULL, &error);
    check_not_null(templ);
    check_equal(error.status, JINJA_CMETA_OK);
    jinja_cmeta_release(templ);
  }

  it("renders raw blocks without interpreting inner template syntax") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"A{% raw %}{{ missing }}{% if broken {# unterminated{% endraw %}B", "A{{ missing }}{% if broken {# unterminatedB"},
      {"{% raw %}{% raw %}x{% endraw %}", "{% raw %}x"},
      {"{% raw %}{{{% endraw %}{% raw %}}}{% endraw %}", "{{}}"},
      {"A  {%- raw -%}  {{ x }}  {%- endraw -%}  B", "A{{ x }}B"},
      {"{% autoescape true %}{% raw %}<b>{{ x }}</b>{% endraw %}{% endautoescape %}", "<b>{{ x }}</b>"},
      {"{% for x in [1,2] %}{% raw %}{{ x }}{% endraw %}{{ x }}{% endfor %}", "{{ x }}1{{ x }}2"},
      {"A{% raw %}{% endraw %}B", "AB"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("raw: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("recognizes raw terminators amid overlapping braces and Unicode whitespace") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% raw %}{{% endraw %}", "{"},
      {"{% raw %}{#{% endraw %}", "{#"},
      {"{% raw %}{% endrawX %}{% endraw extra %}x{% endraw %}", "{% endrawX %}{% endraw extra %}x"},
      {"{%+\xE3\x80\x80raw\xC2\xA0%}x{%+\xE3\x80\x80" "endraw\xC2\xA0+%}", "x"},
      {"A\xE3\x80\x80{%-raw-%}\xC2\xA0x\xE3\x80\x80{%-endraw-%}\xC2\xA0" "B", "AxB"},
      {"A {% raw %}x {% endraw %}{{- user.name }}", "A x Ada"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("raw boundary: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("reports unterminated raw blocks at their opening tag") {
    static const char *const cases[] = {"ab{% raw %}", "ab{% raw %}{% endrawX %}", "ab{% raw %}{{ 1"};
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(cases[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      check_equal(error.offset, (size_t)2u);
    }
  }
}

spec("Jinja CMeta collections and runtime: lexical 9") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("accepts contextual names as shorthand test arguments") {
    static const char source[] =
        "{{ missing is equalto in }}|{{ missing is equalto if }}|"
        "{{ missing is equalto not }}|{{ missing is not equalto in }}|"
        "{{ missing is equalto in in [True] }}|{{ missing is equalto not in [True] }}|"
        "{{ missing is defined and true }}|{{ missing is defined or true }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    JINJA_CMETA_STATUS status = jinja_test_render(source, &model, &root, NULL, &output, &error);
    info("status %d offset %zu: %s", status, error.offset, error.message);
    check_equal(status, JINJA_CMETA_OK);
    check_equal(output, "True|True|True|False|True|True|False|True");
    free(output);
  }

  it("resolves shorthand keyword arguments through loop bindings and postfix lookups") {
    static const char source[] =
        "{% for in in [41] %}{{ 41 is eq in }}|{{ 42 is eq in }}{% endfor %}|"
        "{% for if in [user] %}{{ 42 is eq if.age }}{% endfor %}|"
        "{% for not in [{'age':42}] %}{{ 42 is eq not \t\n['age'] }}|"
        "{{ 41 is eq not.age }}{% endfor %}|"
        "{{ true or (missing is eq not.absent) }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    JINJA_CMETA_STATUS status = jinja_test_render(source, &model, &root, NULL, &output, &error);
    info("status %d offset %zu: %s", status, error.offset, error.message);
    check_equal(status, JINJA_CMETA_OK);
    check_equal(output, "True|False|True|True|False|True");
    free(output);
  }

  it("preserves shorthand argument errors and explicit test boundaries") {
    static const struct { const char *source; JINJA_CMETA_STATUS status; } cases[] = {
      {"{{ missing is defined if true else false }}", JINJA_CMETA_ERR_SYNTAX},
      {"{{ missing is eq is undefined }}", JINJA_CMETA_ERR_SYNTAX},
      {"{{ missing is defined not }}", JINJA_CMETA_ERR_RENDER},
      {"{{ missing is eq not['absent'] }}", JINJA_CMETA_ERR_RENDER}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error),
                  cases[i].status);
      check_null(output);
    }
  }

  it("uses contextual keyword names without changing boolean operators") {
    static const char source[] =
        "{{ and|default('a') }}{{ or|default('o') }}{{ in|default('i') }}"
        "{{ if|default('f') }}{{ else|default('e') }}{{ is|default('s') }}|"
        "{{ true and false }}|{{ false or true }}|{{ not in }}|"
        "{{ 1 not in [2] }}|{{ true and and is undefined }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    JINJA_CMETA_STATUS keyword_status = jinja_test_render(source, &model, &root, NULL, &output, &error);
    info("keyword status %d offset %zu: %s", keyword_status, error.offset, error.message);
    check_equal(keyword_status, JINJA_CMETA_OK);
    check_equal(output, "aoifes|False|True|True|True|True");
    free(output);
  }

  it("joins adjacent string literals before applying postfix operations") {
    static const char source[] =
        "{{ 'hello' \" world\" }}|{{ 'ab' 'cd'|length }}|{{ 'ab''cd'[2] }}|"
        "{{ '\\1' '23'|length }}|{{ not '' '' }}|"
        "{% autoescape true %}{{ '<' '>' }}{% endautoescape %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "hello world|4|c|3|True|&lt;&gt;");
    free(output);
  }

  it("decodes adjacent literals separately in comparisons and collections") {
    static const char source[] =
        "{{ '\\x41' 'B' == 'AB' }}|{{ '\\1' '23' == '\\123' }}|"
        "{{ ['a' 'b', '' ''][0] }}|{{ {'a' 'b': 7}['ab'] }}|"
        "{{ '\\u4e2d'\r\n'\\U0001f600'|length }}|"
        "{{ '\\'' \"\\\"\" }}|{{ '\\000' 'A'|length }}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|ab|7|2|'\"|2");
    free(output);
  }

  it("rejects incomplete adjacent literals without joining escape digits") {
    static const char *sources[] = {
        "{{ '\\x4' '1' }}", "{{ 'ok' '\\u12' '34' }}", "{{ 'ok' 'open }}",
        "{{ ('a') 'b' }}", "{{ 'a' user.name }}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("charges the combined decoded literal against the byte budget") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render("{{ 'ab' 'cd' }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render("{{ 'ab' 'cd' }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "abcd");
    free(output);
  }

  it("preserves unknown escapes and normalizes multiline string literals") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{ '\\q\\d+\\8' }}", "\\q\\d+\\8"},
      {"{{ '\\\u4e2d' }}|{{ '\\q'|length }}", "\\u4e2d|2"},
      {"{{ '\\\u00e9' }}|{{ '\\\U0001f600' }}", "\\xe9|\\U0001f600"},
      {"{{ 'a\nb' }}|{{ 'a\r\nb' }}|{{ 'a\rb' }}", "a\nb|a\nb|a\nb"},
      {"{{ 'a\\\nb' }}|{{ 'a\\\r\nb' }}|{{ 'a\\\rb' }}", "ab|ab|ab"},
      {"{{ 'a\\rb' }}|{{ 'a\r\nb' == 'a\\nb' }}", "a\rb|True"},
      {"{{ '\\123' }}|{{ '\\777' }}|{{ '\\1234' }}|{{ '\\08'|length }}", "S|\xc7\xbf|S4|2"},
      {"{{ '\\000'|length }}|{{ '\\400' == '\\u0100' }}", "1|True"},
      {"{{ '\\\n' }}|{{ '\\\r\n' == '' }}|{{ \"a\r\nb\" }}", "|True|a\nb"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      info("string literal case %zu", i);
      JINJA_CMETA_STATUS status = jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error);
      info("status %d offset %zu: %s", status, error.offset, error.message);
      check_equal(status, JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("reports unterminated string literals as syntax errors") {
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{ 'open }}"), NULL, &error);

    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    check_equal(error.offset, (size_t)0u);
  }

  it("rejects unterminated multiline strings without sentinel reads") {
    static const char source[] = {'{', '{', ' ', '\'', 'a', '\r', '\n', '\\'};
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile((vstr){source, sizeof(source)}, NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
  }

  it("bounds octal decoded UTF-8 bytes at rendering") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    options.max_string_bytes = 1u;
    check_equal(jinja_test_render("{{ '\\777' }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{ '\\777' }}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "\xc7\xbf");
    free(output);
  }

  it("renders named Unicode escapes aliases and adjacent literals") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render("{{ '\\N{latin capital letter a}' '\\N{LF}' }}"
        "{{ '\\N{CJK UNIFIED IDEOGRAPH-4E00}\\N{HANGUL SYLLABLE GA}\\N{GRINNING FACE}' }}"
        "|{{ '\\\\N{UNKNOWN}' }}|{{ '\\N{NULL}'|length }}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    if (output != NULL) check_equal(output, "A\n\u4e00\uac00\U0001f600|\\N{UNKNOWN}|1");
    free(output);
  }

  it("bounds named escape output by decoded UTF8 bytes") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    options.max_string_bytes = 3u;
    check_equal(jinja_test_render("{{ '\\N{GRINNING FACE}' }}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 4u;
    check_equal(jinja_test_render("{{ '\\N{GRINNING FACE}' }}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    if (output != NULL) check_equal(output, "\U0001f600");
    free(output);
  }

  it("rejects unknown named escapes during compilation") {
    static const char *sources[] = {"{{ '\\N{UNKNOWN}' }}", "{{ '\\N{}' }}", "{{ '\\N{KEYCAP DIGIT ONE}' }}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      jinja_cmeta_release(templ);
    }
  }

  it("uses decoded string truth for continuation-only literals") {
    static const char source[] =
        "{{ not '\\\n' }}|{{ not not '\\\r\n' }}|"
        "{% if '\\\n' %}wrong{% else %}empty{% endif %}|"
        "{{ '\\\n' or 'empty' }}|{{ 'yes' if '\\\n' else 'no' }}|"
        "{{ (not 'x') == false }}|{{ (not 'x') == 'x' }}|"
        "{{ (not '\\\n') == (not '') }}|{{ (not not 'x') == true }}|"
        "{% if not '\\\n' %}yes{% endif %}";
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 42}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|empty|empty|no|True|False|True|True|yes");
    free(output);
  }
}
