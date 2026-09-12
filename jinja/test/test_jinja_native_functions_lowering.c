#include "test_jinja_native_functions_support.h"
spec("Jinja native function lowering") {
  static JINJA_CMETA_TEMPLATE *templ;
  static JINJA_CMETA_ERROR error;
  static tstr input;
  static JINJA_TEMPLATE_TREE tree;

  before_each() { templ = NULL; input = NULL; }
  after_each() { jinja_cmeta_release(templ); jinja_template_tree_destroy(&tree); tstr_free(input); }

  it("owns macro names defaults and native body after source reuse") {
    char source[] = "A{% macro f(a=b,b=2) %}{{a}}:{{b}}{% endmacro %}Z";
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    memset(source, '!', sizeof(source) - 1u);
    check_equal(templ->function_count, (size_t)1u);
    check_equal(templ->parameter_count, (size_t)2u);
    const JINJA_CMETA_FUNCTION *function = &templ->functions[0];
    check_equal(function->parent, SIZE_MAX);
    check_equal(function->parameter_count, (size_t)2u);
    check_equal(function->call_expression, SIZE_MAX);
    check_equal(function->name_length, (size_t)1u);
    check_equal(templ->program_strings[function->name_offset], 'f');
    check_equal(templ->cells[0].name.len, (size_t)1u);
    check_equal(templ->cells[0].name.data[0], 'f');
    check_equal(templ->program_strings[templ->parameters[0].name_offset], 'a');
    check_equal(templ->program_strings[templ->parameters[1].name_offset], 'b');
    const JINJA_CMETA_EXPRESSION_NODE *first = &templ->expressions[templ->parameters[0].default_expression];
    check_equal(first->kind, JINJA_CMETA_EXPRESSION_PATH);
    check_equal(first->path.len, (size_t)1u);
    check_equal(first->path.data[0], 'b');
    check_equal(templ->expressions[templ->parameters[1].default_expression].integer, (int64_t)2);
    check_equal(function->body_begin, (size_t)2u);
    check_equal(function->body_end, (size_t)5u);
    check_equal(templ->instructions[1].opcode, JINJA_CMETA_OP_FUNCTION);
    check_equal(templ->instructions[1].end, function->body_end);
    check_equal(templ->instructions[function->body_begin].opcode, JINJA_CMETA_OP_OUTPUT);
    check_equal(templ->instructions[function->body_end].opcode, JINJA_CMETA_OP_TEXT);
  }

  it("lowers nested macros and caller bodies as distinct functions") {
    const char *source = "{% macro outer(x) %}{% macro inner() %}{{x}}{{kwargs}}{{varargs}}{% endmacro %}"
        "{% call(y='yes') wrap() %}{{y}}{% endcall %}{% endmacro %}";
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(templ->function_count, (size_t)3u);
    check_equal(templ->functions[0].parent, SIZE_MAX);
    check_equal(templ->functions[1].parent, (size_t)0u);
    check_equal(templ->functions[2].parent, (size_t)0u);
    check_equal(templ->functions[1].accepts_kwargs, 1);
    check_equal(templ->functions[1].accepts_varargs, 1);
    check_equal(templ->functions[2].name_length, (size_t)0u);
    check_not_equal(templ->functions[2].call_expression, SIZE_MAX);
    check_equal(templ->expressions[templ->functions[2].call_expression].kind, JINJA_CMETA_EXPRESSION_CALL);
    check_equal(templ->parameters[0].default_expression, SIZE_MAX);
    check_equal(templ->parameter_count, (size_t)2u);
  }

  it("admits function programs publicly and validates render arguments") {
    templ = jinja_cmeta_compile(vstr_from_cstr("{% macro f() %}X{% endmacro %}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, NULL, NULL, NULL, NULL, NULL, &error), JINJA_CMETA_ERR_INVALID_ARGUMENT);
  }

  it("bounds cumulative function and parameter storage") {
    input = tstr_new();
    check_not_null(input);
    const char *definition = "{% macro f() %}{% endmacro %}";
    for (size_t i = 0u; i < JINJA_CMETA_MAX_FUNCTIONS; ++i) {
      tstr next = tstr_cat_len(input, definition, strlen(definition));
      check_not_null(next);
      input = next;
    }
    templ = jinja_cmeta_compile(vstr_from_buf(input, tstr_len(input)), NULL, &error);
    check_not_null(templ);
    check_equal(templ->function_count, (size_t)JINJA_CMETA_MAX_FUNCTIONS);
    jinja_cmeta_release(templ);
    templ = NULL;
    tstr next = tstr_cat_len(input, definition, strlen(definition));
    check_not_null(next);
    input = next;
    templ = jinja_cmeta_compile(vstr_from_buf(input, tstr_len(input)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    const char *parameters = "{% macro f(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,aa,ab,ac,ad,ae,af) %}"
        "{% endmacro %}{% macro g(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,aa,ab,ac,ad,ae,af,ag) %}"
        "{% endmacro %}";
    templ = jinja_cmeta_compile(vstr_from_cstr(parameters), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
  }

  it("uses configured delimiters Unicode names and output newline policy") {
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    options.block_start_string = vstr_from_cstr("[%");
    options.block_end_string = vstr_from_cstr("%]");
    options.variable_start_string = vstr_from_cstr("[[");
    options.variable_end_string = vstr_from_cstr("]]");
    const char *source = "[% macro \xE5\x90\x8D(\xE5\x80\xBC=1) %]A\r\n[[\xE5\x80\xBC]][% endmacro %]";
    templ = jinja_cmeta_compile(vstr_from_cstr(source), &options, &error);
    check_not_null(templ);
    check_equal(templ->functions[0].name_length, (size_t)3u);
    check_equal(memcmp(templ->program_strings + templ->functions[0].name_offset, "\xE5\x90\x8D", 3u), 0);
    check_equal(memcmp(templ->program_strings + templ->parameters[0].name_offset, "\xE5\x80\xBC", 3u), 0);
    const JINJA_CMETA_INSTRUCTION *text = &templ->instructions[templ->functions[0].body_begin];
    check_equal(text->opcode, JINJA_CMETA_OP_TEXT);
    check_equal(text->length, (size_t)2u);
    check_equal(memcmp(templ->program_strings + text->offset, "A\n", 2u), 0);
  }

  it("lowers defaults without evaluating them and permits function local loop control") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% macro f(a=1/0) %}{% for x in [1] %}{% break %}{% endfor %}{% endmacro %}"), NULL, &error);
    check_not_null(templ);
    check_equal(templ->expressions[templ->parameters[0].default_expression].kind,
        JINJA_CMETA_EXPRESSION_BINARY_ARITHMETIC);
    size_t controls = 0u;
    for (size_t i = templ->functions[0].body_begin; i < templ->functions[0].body_end; ++i)
      if (templ->instructions[i].opcode == JINJA_CMETA_OP_LOOP_CONTROL) {
        ++controls;
        check_equal(templ->instructions[templ->instructions[i].target].opcode, JINJA_CMETA_OP_FOR_BEGIN);
      }
    check_equal(controls, (size_t)1u);
  }

  it("rejects loop control across a function boundary and invalid caller parameters") {
    const char *sources[] = {
      "{% for x in [1] %}{% macro f() %}{% break %}{% endmacro %}{% endfor %}",
      "{% for x in [1] %}{% call wrap() %}{% continue %}{% endcall %}{% endfor %}",
      "{% macro f(caller) %}{{caller()}}{% endmacro %}",
      "{% macro f() %}X{% endcall %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      templ = jinja_cmeta_compile(vstr_from_cstr(sources[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("retains the full source diagnostic for an unsupported default expression") {
    const char *source = "prefix{% macro f(a=1|unknown) %}{% endmacro %}";
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_UNSUPPORTED);
    check_equal(error.offset, sizeof("prefix{% macro f(a=") - 1u);
  }

  it("maps lexical captures and shadow initialization to distinct cells") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set x=1 %}{% macro f() %}{{x}}{% endmacro %}"
        "{% with x=2 %}{{f()}}{% endwith %}{% set x=3 %}"), NULL, &error);
    check_not_null(templ);
    const JINJA_CMETA_LEXICAL_SCOPE *root = &templ->lexical_scopes[0];
    const JINJA_CMETA_LEXICAL_SCOPE *macro = &templ->lexical_scopes[templ->functions[0].scope];
    check_equal(macro->parent, (size_t)0u);
    check_equal(macro->binding_count, (size_t)0u);
    size_t root_x = SIZE_MAX, inner_x = SIZE_MAX;
    for (size_t i = 0u; i < templ->cell_count; ++i) {
      if (templ->cells[i].name.len == 1u && templ->cells[i].name.data[0] == 'x') {
        if (templ->cells[i].level == 0u) root_x = i;
        else inner_x = i;
      }
    }
    check_not_equal(root_x, SIZE_MAX);
    check_not_equal(inner_x, SIZE_MAX);
    check_not_equal(root_x, inner_x);
    check_equal(root->owner, (size_t)0u);
    check_equal(templ->cells[root_x].owner, (size_t)0u);
    check_equal(templ->cells[inner_x].owner, (size_t)0u);
  }

  it("shares same-level sibling loop cells without sharing different names or function activations") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% for x in [1] %}{% macro f() %}{{x}}{{loop.index}}{% endmacro %}{% endfor %}"
        "{% for x in [2] %}{{x}}{% endfor %}{% for y in [3] %}{{y}}{% endfor %}"
        "{% macro g(x) %}{{x}}{% endmacro %}"), NULL, &error);
    check_not_null(templ);
    size_t root_x_count = 0u, macro_x_count = 0u, loop_count = 0u;
    for (size_t i = 0u; i < templ->cell_count; ++i) {
      const JINJA_CMETA_CELL *cell = &templ->cells[i];
      if (cell->name.len == 1u && cell->name.data[0] == 'x') {
        if (cell->owner == 0u) ++root_x_count;
        else ++macro_x_count;
      }
      if (cell->name.len == sizeof("loop") - 1u && memcmp(cell->name.data, "loop", cell->name.len) == 0)
        ++loop_count;
    }
    check_equal(root_x_count, (size_t)1u);
    check_equal(macro_x_count, (size_t)1u);
    check_equal(loop_count, (size_t)1u);
    const JINJA_CMETA_LEXICAL_SCOPE *macro = &templ->lexical_scopes[templ->functions[0].scope];
    check_equal(macro->binding_count, (size_t)0u);
    check_not_equal(macro->parent, (size_t)0u);
  }

  it("initializes shadow cells from ancestors without redirecting their identity") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set x=1 %}{% with %}{{x}}{% set x=2 %}{% macro f() %}{{x}}{% endmacro %}{% endwith %}"), NULL, &error);
    check_not_null(templ);
    size_t aliases = 0u;
    for (size_t i = 0u; i < templ->cell_binding_count; ++i) {
      const JINJA_CMETA_CELL_BINDING *binding = &templ->cell_bindings[i];
      if (binding->load != JINJA_CMETA_CELL_ALIAS) continue;
      ++aliases;
      check_not_equal(binding->cell, binding->source_cell);
      check_equal(templ->cells[binding->cell].level, (size_t)1u);
      check_equal(templ->cells[binding->source_cell].level, (size_t)0u);
      check_equal(templ->cells[binding->cell].name.data[0], 'x');
      check_equal(templ->cells[binding->source_cell].name.data[0], 'x');
    }
    check_equal(aliases, (size_t)1u);
  }

  it("separates filter helper activation and retains recursive body else ownership") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% for x in [1] if x recursive %}{{loop.depth}}{% else %}{{outside}}{% endfor %}"), NULL, &error);
    check_not_null(templ);
    check_equal(templ->lexical_scope_count, (size_t)4u);
    const JINJA_CMETA_LEXICAL_SCOPE *body = &templ->lexical_scopes[1];
    const JINJA_CMETA_LEXICAL_SCOPE *test = &templ->lexical_scopes[2];
    const JINJA_CMETA_LEXICAL_SCOPE *alternate = &templ->lexical_scopes[3];
    check_equal(body->owner, (size_t)1u);
    check_equal(test->owner, (size_t)2u);
    check_equal(alternate->owner, body->owner);
    check_equal(test->part, JINJA_CMETA_SCOPE_TEST);
    check_equal(alternate->part, JINJA_CMETA_SCOPE_ELSE);
    check_equal(body->parent, (size_t)0u);
    check_equal(test->parent, (size_t)0u);
    check_equal(alternate->parent, (size_t)0u);
  }

  it("retains an empty root scope without allocating cells") {
    templ = jinja_cmeta_compile(vstr_from_cstr(""), NULL, &error);
    check_not_null(templ);
    check_equal(templ->lexical_scope_count, (size_t)1u);
    check_equal(templ->cell_count, (size_t)0u);
    check_equal(templ->cell_binding_count, (size_t)0u);
    check_null(templ->cells);
  }

  it("bounds lexical binding tables before allocating their value identities") {
    enum { WITH_COUNT_AT_LIMIT = 65 };
    const JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    const char *block = "{% with (a00,a01,a02,a03,a04,a05,a06,a07,a08,a09,a10,a11,a12,a13,a14,a15,"
        "a16,a17,a18,a19,a20,a21,a22,a23,a24,a25,a26,a27,a28,a29,a30,a31,"
        "a32,a33,a34,a35,a36,a37,a38,a39,a40,a41,a42,a43,a44,a45,a46,a47,"
        "a48,a49,a50,a51,a52,a53,a54,a55,a56,a57,a58,a59,a60,a61,a62)=items %}{% endwith %}";
    input = tstr_new();
    check_not_null(input);
    for (size_t i = 0u; i < WITH_COUNT_AT_LIMIT; ++i) {
      tstr next = tstr_cat_len(input, block, strlen(block));
      check_not_null(next);
      input = next;
    }
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_buf(input, tstr_len(input)), &tree, NULL),
        JINJA_EXPRESSION_PARSE_OK);
    JINJA_CMETA_STATUS allocation_status;
    templ = jinja_cmeta_artifact_create(NULL, &allocation_status);
    check_equal(allocation_status, JINJA_CMETA_OK);
    check_not_null(templ);
    check_equal(jinja_cmeta_build_layout(vstr_from_buf(input, tstr_len(input)), &tree, templ, &error), JINJA_CMETA_OK);
    check_equal(templ->cell_binding_count, (size_t)JINJA_CMETA_MAX_LEXICAL_CELLS);
    check_equal(templ->cell_count, (size_t)64u);
    jinja_cmeta_release(templ);
    templ = NULL;
    tstr next = tstr_cat_len(input, block, strlen(block));
    check_not_null(next);
    input = next;
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_buf(input, tstr_len(input)), &tree, NULL),
        JINJA_EXPRESSION_PARSE_OK);
    templ = jinja_cmeta_artifact_create(NULL, &allocation_status);
    check_equal(allocation_status, JINJA_CMETA_OK);
    check_not_null(templ);
    check_equal(jinja_cmeta_build_layout(vstr_from_buf(input, tstr_len(input)), &tree, templ, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_equal(error.offset, tstr_len(input) - strlen(block));
    check_null(templ->cells);
  }
}
