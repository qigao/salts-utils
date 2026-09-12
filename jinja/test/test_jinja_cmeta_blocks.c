#include "jinja_cmeta_test_support.h"

spec("Jinja block header colons") {
  static JinjaTestModel model;
  static JinjaTestRoot root;
  static JINJA_CMETA_ERROR error;
  static JINJA_CMETA_TEMPLATE *templ;
  static char *output;
  before_each() {
    jinja_test_model_init(&model);
    root = (JinjaTestRoot){{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = NULL;
    output = NULL;
  }
  after_each() {
    free(output);
    jinja_cmeta_release(templ);
  }

  it("accepts an optional colon after if") {
    check_equal(jinja_test_render("{% if true: %}X{% endif %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "X");
  }

  it("accepts an optional colon after elif") {
    check_equal(jinja_test_render("{% if false %}A{% elif true: %}B{% else %}C{% endif %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "B");
  }

  it("accepts an optional colon after else") {
    check_equal(jinja_test_render("{% if false %}A{% else: %}B{% endif %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "B");
  }

  it("accepts an optional colon after a filtered for header") {
    check_equal(jinja_test_render("{% for x in [1,2,3] if x is odd: %}{{x}}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "13");
  }

  it("accepts an optional colon after for else") {
    check_equal(jinja_test_render("{% for x in []: %}X{% else: %}empty{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "empty");
  }

  it("accepts colons inside recursive loop bodies") {
    check_equal(jinja_test_render("{% for x in [[1],[]] recursive: %}{% if x is iterable: %}{{loop(x)}}{% else: %}{{x}}{% endif %}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1");
  }

  it("accepts a colon without changing autoescape restoration") {
    check_equal(jinja_test_render("{% autoescape true: %}{{'<x>'}}{% endautoescape %}{{'<y>'}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "&lt;x&gt;<y>");
  }

  it("accepts an optional colon after a filter block") {
    check_equal(jinja_test_render("{% filter trim: %} X {% endfilter %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "X");
  }

  it("accepts an optional colon after filtered capture targets") {
    check_equal(jinja_test_render("{% set x | trim: %} X {% endset %}{{x}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "X");
  }

  it("accepts colon branches in macro and call bodies") {
    check_equal(jinja_test_render("{% macro wrap(): %}[{{caller()}}]{% endmacro %}{% call wrap(): %}{% if true: %}X{% endif %}{% endcall %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[X]");
  }

  it("preserves colons inside strings dictionaries and slices") {
    check_equal(jinja_test_render("{% if {'x:':[1,2]}['x:'][1:]: %}{{'a:b'}}{% endif %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a:b");
  }

  it("trims Unicode whitespace around optional colons") {
    check_equal(jinja_test_render("{% if true : %}X{% else : %}Y{% endif %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "X");
  }

  it("matches parsed statements after raw and comment nodes") {
    check_equal(jinja_test_render("A{# if false: #}{% raw %}{% if false: %}{% endraw %}B{% if true: %}C{% endif %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A{% if false: %}BC");
  }

  it("retains whitespace controls beside optional colons") {
    check_equal(jinja_test_render("A \n{%- if true: -%} X {%- else: -%} Y {%- endif -%}\n Z",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "AXZ");
  }

  it("uses normalized block headers with custom delimiters and line statements") {
    const char *source = "# if true:\r\n<% for x in [1,2]: %>[[x]]"
        "<% else: %>E<% endfor %>\r\n# endif\r\n";
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    options.variable_start_string = vstr_from_cstr("[[");
    options.variable_end_string = vstr_from_cstr("]]");
    options.block_start_string = vstr_from_cstr("<%");
    options.block_end_string = vstr_from_cstr("%>");
    options.line_statement_prefix = vstr_from_cstr("#");
    options.newline_sequence = vstr_from_cstr("\r\n");
    templ = jinja_cmeta_compile(vstr_from_cstr(source), &options, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL,
        &output, &error), JINJA_CMETA_OK);
    check_equal(output, "12\r\n");
  }

  it("preserves render error offsets after a colon header") {
    const char *source = "中{% if true: %}{{missing.x}}{% endif %}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{missing.x}}") - source));
  }

  it("rejects colons outside their permitted statement positions") {
    static const char *sources[] = {
      "{% set x=1: %}", "{% with x=1: %}{% endwith %}",
      "{% print 1: %}", "{% do 1: %}", "{% if true:: %}{% endif %}",
      "{% for x in: %}{% endfor %}", "{% if true %}{% else 1: %}{% endif %}",
      "{% if true %}{% endif: %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("invalid colon: %s", sources[i]);
      templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("reports later syntax errors before lowering a valid colon block") {
    const char *source = "{% if true: %}X{% endif %}中{% set x= %}";
    jinja_test_compile_failure(vstr_from_cstr(source), NULL, JINJA_CMETA_ERR_SYNTAX,
        (size_t)(strstr(source, "{% set") - source));
  }
}

spec("Jinja block declaration workspace") {
  static tstr source;
  before_each() { source = NULL; }
  after_each() { tstr_free(source); }

  it("checks late duplicate names beyond a small fixed block table") {
    enum { BLOCKS = 257 };
    source = jinja_test_named_blocks(BLOCKS);
    check_not_null(source);
    size_t duplicate = tstr_len(source);
    tstr next = tstr_cat(source, "{% block b128 %}{% endblock %}");
    check_not_null(next);
    source = next;
    jinja_test_compile_failure(vstr_from_buf(source, tstr_len(source)), NULL,
        JINJA_CMETA_ERR_SYNTAX, duplicate);
  }

  it("admits the block function limit and rejects one additional declaration") {
    source = jinja_test_named_blocks(JINJA_CMETA_MAX_CONDITION_BRANCHES);
    check_not_null(source);
    JINJA_CMETA_TEMPLATE *compiled = jinja_cmeta_compile(vstr_from_buf(source, tstr_len(source)), NULL, NULL);
    check_not_null(compiled);
    jinja_cmeta_release(compiled);
    size_t overflow = tstr_len(source);
    tstr next = tstr_cat(source, "{% block overflow %}{% endblock %}");
    check_not_null(next);
    source = next;
    jinja_test_compile_failure(vstr_from_buf(source, tstr_len(source)), NULL,
        JINJA_CMETA_ERR_CAPACITY, overflow);
  }
}
