#include "parser/jinja_template_parser.h"
#include "tinytest.h"
#include <stdio.h>
#include <string.h>

static const JINJA_EXPRESSION_SCOPE_SYMBOL *template_scope_symbol(
    const JINJA_EXPRESSION_SCOPE *scope, const char *name) {
  for (size_t i = 0u; i < scope->count; ++i)
    if (scope->symbols[i].name.length == strlen(name) &&
        memcmp(scope->source.data + scope->symbols[i].name.offset, name, strlen(name)) == 0)
      return &scope->symbols[i];
  return NULL;
}

spec("Jinja undeclared name discovery") {
  static JINJA_TEMPLATE_TREE tree;
  after_each() { jinja_template_tree_destroy(&tree); }

  it("describes macro defaults capabilities and body using template relative spans") {
    vstr source = vstr_from_cstr("prefix{% macro f(a=b,b=2) %}{{a}}{{kwargs}}{{varargs}}{% endmacro %}suffix");
    JINJA_TEMPLATE_MACRO_DESCRIPTOR descriptor;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_describe_macro(source, &tree, 1u, &descriptor, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(descriptor.signature.parameter_count, (size_t)2u);
    check_equal(descriptor.signature.name.offset, (size_t)(strstr(source.data, "f(") - source.data));
    check_equal(descriptor.signature.parameters[0].default_expression.offset,
        (size_t)(strstr(source.data, "a=b") - source.data) + 2u);
    check_equal(descriptor.signature.default_reference_parameters[0], (size_t)1u);
    check_equal(descriptor.signature.default_references[0].offset, descriptor.signature.parameters[0].default_expression.offset);
    check_equal(descriptor.bindings.accesses, (1u << JINJA_TEMPLATE_MACRO_KWARGS) | (1u << JINJA_TEMPLATE_MACRO_VARARGS));
    check_equal(tree.nodes[descriptor.body_begin].kind, JINJA_TEMPLATE_OUTPUT);
    check_equal(descriptor.body_end, tree.nodes[1u].match);
    check_equal(descriptor.call.length, (size_t)0u);
  }

  it("describes anonymous caller signatures without mixing call and default offsets") {
    vstr source = vstr_from_cstr("prefix{% call(x='a') wrap(*[1]) %}{{x}}{{caller()}}{% endcall %}");
    JINJA_TEMPLATE_MACRO_DESCRIPTOR descriptor;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_describe_macro(source, &tree, 1u, &descriptor, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(descriptor.signature.name.length, (size_t)0u);
    check_equal(descriptor.signature.parameter_count, (size_t)1u);
    check_equal(descriptor.call.offset, (size_t)(strstr(source.data, " wrap(") - source.data));
    check_equal(descriptor.call.length, strlen(" wrap(*[1])"));
    check_equal(descriptor.signature.parameters[0].default_expression.offset,
        (size_t)(strstr(source.data, "'a'") - source.data));
    check_equal(descriptor.bindings.accesses, 1u << JINJA_TEMPLATE_MACRO_CALLER);
  }

  it("preserves macro descriptors on invalid selections and capability failures") {
    vstr source = vstr_from_cstr("prefix{% macro f(caller) %}{{caller()}}{% endmacro %}");
    JINJA_TEMPLATE_MACRO_DESCRIPTOR descriptor = {0}, previous;
    descriptor.body_begin = SIZE_MAX;
    previous = descriptor;
    size_t error = SIZE_MAX;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_describe_macro(source, &tree, 0u, &descriptor, &error), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&descriptor, &previous, sizeof(descriptor)), 0);
    check_equal(jinja_template_describe_macro(source, &tree, 1u, &descriptor, &error), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(error, (size_t)(strstr(source.data, "caller)") - source.data));
    check_equal(memcmp(&descriptor, &previous, sizeof(descriptor)), 0);
    tree.nodes[1u].match = tree.count;
    check_equal(jinja_template_describe_macro(source, &tree, 1u, &descriptor, &error), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&descriptor, &previous, sizeof(descriptor)), 0);
  }

  it("retains Unicode names and canonical empty spans in macro descriptors") {
    vstr source = vstr_from_cstr("前{% macro 问候(名字) %}{{名字}}{% endmacro %}");
    JINJA_TEMPLATE_MACRO_DESCRIPTOR descriptor;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_describe_macro(source, &tree, 1u, &descriptor, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(descriptor.signature.name.length, strlen("问候"));
    check_equal(descriptor.signature.parameters[0].name.length, strlen("名字"));
    check_equal(descriptor.signature.parameters[0].default_expression.offset, (size_t)0u);
    check_equal(descriptor.signature.parameters[0].default_expression.length, (size_t)0u);
    check_equal(descriptor.call.offset, (size_t)0u);
    check_equal(descriptor.bindings.accesses, 0u);
  }

  it("follows Name contexts across closures instead of lexical frame boundaries") {
    static const struct { const char *source; int found; } cases[] = {
      {"{% macro inner() %}{{ caller() }}{% endmacro %}", 1},
      {"{% macro inner(caller) %}{{ caller() }}{% endmacro %}{{ caller }}", 0},
      {"{% macro caller() %}x{% endmacro %}{{ caller }}", 1},
      {"{% block b %}{{ caller }}{% endblock %}", 0},
      {"{{ caller }}{% set caller=0 %}", 1},
      {"{% set caller=caller %}{{ caller }}", 0},
      {"{% set caller.value=1 %}{{ caller }}", 1},
      {"{% import 'm' as caller %}{{ caller }}", 1},
      {"{% from 'm' import x as caller %}{{ caller }}", 1},
      {"{{ obj.caller }}{{ f(caller=1) }}{{ 'caller' }}", 0}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      vstr source = vstr_from_cstr(cases[i].source);
      JINJA_EXPRESSION_SPAN reference = {0};
      info("source: %s", cases[i].source);
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(jinja_template_find_undeclared(source, &tree, SIZE_MAX, vstr_from_cstr("caller"), &reference, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(reference.length != 0u, cases[i].found != 0);
    }
  }

  it("uses structural target value body and deferred filter ordering") {
    static const struct { const char *source; int found; } cases[] = {
      {"{% with x=caller,caller=1 %}{{ caller }}{% endwith %}", 0},
      {"{% for caller in caller %}{{ caller }}{% endfor %}", 0},
      {"{% for x in xs if caller %}{% set caller=0 %}{% endfor %}", 0},
      {"{% for x in xs if caller %}x{% else %}{% set caller=0 %}{% endfor %}", 0},
      {"{% for x in xs if caller %}x{% endfor %}", 1},
      {"{% filter default(caller) %}{% set caller=0 %}{% endfilter %}", 0},
      {"{% set x | default(caller) %}{% set caller=0 %}{% endset %}", 1},
      {"{% set caller | default(caller) %}{{ caller }}{% endset %}", 0},
      {"{% call(caller) caller() %}{{ caller }}{% endcall %}", 1},
      {"{% macro m(x=caller,caller=0) %}x{% endmacro %}", 0},
      {"{% autoescape caller %}x{% endautoescape %}", 1}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      vstr source = vstr_from_cstr(cases[i].source);
      JINJA_EXPRESSION_SPAN reference = {0};
      info("source: %s", cases[i].source);
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(jinja_template_find_undeclared(source, &tree, SIZE_MAX, vstr_from_cstr("caller"), &reference, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(reference.length != 0u, cases[i].found != 0);
    }
  }

  it("queries only selected bodies and preserves original Unicode byte offsets") {
    vstr source = vstr_from_cstr("前{% macro m(caller, x=kwargs) %}{{ caller }}{{ 名称 }}{% endmacro %}");
    JINJA_EXPRESSION_SPAN reference = {0};
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_find_undeclared(source, &tree, 1u, vstr_from_cstr("caller"), &reference, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.offset, (size_t)(strstr(source.data, "{{ caller") - source.data) + 3u);
    check_equal(jinja_template_find_undeclared(source, &tree, 1u, vstr_from_cstr("kwargs"), &reference, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.length, (size_t)0u);
    check_equal(jinja_template_find_undeclared(source, &tree, 1u, vstr_from_cstr("名称"), &reference, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.offset, (size_t)(strstr(source.data, "名称") - source.data));
    check_equal(reference.length, strlen("名称"));
  }

  it("excludes a selected loop target test and else from body discovery") {
    vstr source = vstr_from_cstr("{% for loop in items if predicate %}{{ loop }}{% else %}{{ fallback }}{% endfor %}");
    JINJA_EXPRESSION_SPAN reference = {0};
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_find_undeclared(source, &tree, 0u, vstr_from_cstr("loop"), &reference, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.length, strlen("loop"));
    check_equal(jinja_template_find_undeclared(source, &tree, 0u, vstr_from_cstr("predicate"), &reference, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.length, (size_t)0u);
    check_equal(jinja_template_find_undeclared(source, &tree, 0u, vstr_from_cstr("fallback"), &reference, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.length, (size_t)0u);
  }

  it("leaves the reference unchanged on invalid selections and unsupported extensions") {
    vstr source = vstr_from_cstr("{% debug %}");
    JINJA_EXPRESSION_SPAN reference = {7u, 9u}, previous = reference;
    size_t error = SIZE_MAX;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DEBUG, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_find_undeclared(source, &tree, SIZE_MAX, vstr_from_cstr("self"), &reference, &error), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
    check_equal(error, (size_t)0u);
    check_equal(memcmp(&reference, &previous, sizeof(reference)), 0);
    check_equal(jinja_template_find_undeclared(source, &tree, tree.count, vstr_from_cstr("self"), &reference, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_template_find_undeclared(source, &tree, SIZE_MAX, vstr_from_cstr(""), &reference, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&reference, &previous, sizeof(reference)), 0);
  }
}

spec("Jinja template lexical frames") {
  static JINJA_TEMPLATE_TREE tree;
  after_each() { jinja_template_tree_destroy(&tree); }
  static JINJA_EXPRESSION_SCOPE scope, parent;
  before_each() {
    memset(&tree, 0, sizeof(tree));
    memset(&scope, 0, sizeof(scope));
    memset(&parent, 0, sizeof(parent));
  }

  it("binds implicit macro arguments locally after explicit parameters and before defaults") {
    vstr source = vstr_from_cstr("{% set kwargs=1 %}{% macro m(x=kwargs) %}{{ caller() }}{{ kwargs }}{{ varargs }}{% endmacro %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 1u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)4u);
    for (size_t i = 0u; i < scope.count; ++i) check_equal(scope.symbols[i].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[0].name.length, strlen("x"));
    check_equal(scope.symbols[1].name.length, strlen("caller"));
    check_equal(scope.symbols[2].name.length, strlen("kwargs"));
    check_equal(scope.symbols[3].name.length, strlen("varargs"));
    JINJA_TEMPLATE_MACRO_BINDINGS bindings;
    check_equal(jinja_template_analyze_macro_bindings(source, &tree, 1u, &bindings, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(bindings.accesses, (1u << JINJA_TEMPLATE_MACRO_CALLER) | (1u << JINJA_TEMPLATE_MACRO_KWARGS) | (1u << JINJA_TEMPLATE_MACRO_VARARGS));
    check_equal(bindings.implicit[JINJA_TEMPLATE_MACRO_KWARGS].offset, (size_t)(strstr(source.data, "{{ kwargs") - source.data) + 3u);
  }

  it("distinguishes explicit caller from extra keyword and positional parameters") {
    vstr source = vstr_from_cstr("{% macro m(kwargs,varargs,caller=none) %}{{ caller }}{{ kwargs }}{{ varargs }}{% endmacro %}");
    JINJA_TEMPLATE_MACRO_BINDINGS bindings;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_macro_bindings(source, &tree, 0u, &bindings, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(bindings.accesses, 1u << JINJA_TEMPLATE_MACRO_CALLER);
    for (size_t i = 0u; i < JINJA_TEMPLATE_MACRO_SPECIAL_COUNT; ++i) check_equal(bindings.implicit[i].length, (size_t)0u);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    for (size_t i = 0u; i < scope.count; ++i) check_equal(scope.symbols[i].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
  }

  it("rejects a used explicit caller without a default in macros and call blocks atomically") {
    static const char *const sources[] = {
      "前{% macro m(caller) %}{{ caller }}{% endmacro %}",
      "前{% call(caller) wrap() %}{{ caller }}{% endcall %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      vstr source = vstr_from_cstr(sources[i]);
      JINJA_TEMPLATE_MACRO_BINDINGS bindings, previous;
      memset(&bindings, 0x5a, sizeof(bindings));
      previous = bindings;
      JINJA_EXPRESSION_SCOPE prior_scope = scope;
      size_t error = SIZE_MAX;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(jinja_template_analyze_macro_bindings(source, &tree, 1u, &bindings, &error), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(error, (size_t)(strstr(source.data, "caller") - source.data));
      check_equal(memcmp(&bindings, &previous, sizeof(bindings)), 0);
      check_equal(jinja_template_analyze_frame(source, &tree, 1u, NULL, &scope, &error), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(memcmp(&scope, &prior_scope, sizeof(scope)), 0);
    }
  }

  it("does not infer macro extras from defaults labels or structurally shadowed names") {
    static const char *const sources[] = {
      "{% macro m(x=kwargs) %}{{ x }}{% endmacro %}",
      "{% macro m(caller) %}x{% endmacro %}",
      "{% macro m() %}{% set caller=caller %}{% set kwargs=1 %}{% set varargs=1 %}{{ kwargs }}{{ varargs }}{% endmacro %}",
      "{% call kwargs() %}{{ obj.varargs }}{% endcall %}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      vstr source = vstr_from_cstr(sources[i]);
      JINJA_TEMPLATE_MACRO_BINDINGS bindings;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(jinja_template_analyze_macro_bindings(source, &tree, 0u, &bindings, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(bindings.accesses, 0u);
      for (size_t j = 0u; j < JINJA_TEMPLATE_MACRO_SPECIAL_COUNT; ++j) check_equal(bindings.implicit[j].length, (size_t)0u);
      check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    }
  }

  it("discovers macro capabilities through nested closures but not through blocks") {
    vstr source = vstr_from_cstr("{% call(x=kwargs) wrap() %}{% macro inner() %}{{ kwargs }}{% endmacro %}"
        "{% block b %}{{ caller }}{{ varargs }}{% endblock %}{% endcall %}");
    JINJA_TEMPLATE_MACRO_BINDINGS bindings;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_macro_bindings(source, &tree, 0u, &bindings, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(bindings.accesses, 1u << JINJA_TEMPLATE_MACRO_KWARGS);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_SCOPE_SYMBOL *kwargs = template_scope_symbol(&scope, "kwargs");
    check_not_null(kwargs);
    if (kwargs != NULL) check_equal(kwargs->load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
  }

  it("preserves macro metadata on invalid selection and capacity errors") {
    vstr source = vstr_from_cstr("{{ caller }}");
    JINJA_TEMPLATE_MACRO_BINDINGS bindings, previous;
    memset(&bindings, 0x5a, sizeof(bindings));
    previous = bindings;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_macro_bindings(source, &tree, 0u, &bindings, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&bindings, &previous, sizeof(bindings)), 0);
    check_equal(jinja_template_analyze_macro_bindings(source, &tree, SIZE_MAX, &bindings, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&bindings, &previous, sizeof(bindings)), 0);
    tree.count = JINJA_TEMPLATE_MAX_NODES + 1u;
    check_equal(jinja_template_analyze_macro_bindings(source, &tree, 0u, &bindings, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&bindings, &previous, sizeof(bindings)), 0);
  }

  it("does not publish partial special arguments when nested discovery is unsupported") {
    vstr source = vstr_from_cstr("{% macro m() %}{{ caller }}{% macro inner() %}{% debug %}{% endmacro %}{% endmacro %}");
    JINJA_TEMPLATE_MACRO_BINDINGS bindings, previous;
    memset(&bindings, 0x5a, sizeof(bindings));
    previous = bindings;
    JINJA_EXPRESSION_SCOPE prior_scope = scope;
    size_t error = SIZE_MAX;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DEBUG, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_macro_bindings(source, &tree, 0u, &bindings, &error), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
    check_equal(error, (size_t)(strstr(source.data, "{% debug") - source.data));
    check_equal(memcmp(&bindings, &previous, sizeof(bindings)), 0);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, &error), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
    check_equal(memcmp(&scope, &prior_scope, sizeof(scope)), 0);
  }

  it("analyzes block bodies as independent scopes even when marked scoped") {
    vstr source = vstr_from_cstr("{% set outside=1 %}{% block content scoped %}"
        "{{ outside }}{% set local=1 %}{% endblock %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.count, (size_t)1u);
    check_equal(jinja_template_analyze_frame(source, &tree, 1u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)2u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_UNDEFINED);
    check(scope.parent == NULL);
    JINJA_EXPRESSION_SCOPE previous = scope;
    check_equal(jinja_template_analyze_frame(source, &tree, 1u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
  }

  it("keeps nested blocks independent while nested macros can borrow their enclosing block") {
    vstr source = vstr_from_cstr("{% block outer %}{% set value=1 %}"
        "{% block inner %}{{ value }}{% endblock %}"
        "{% macro m() %}{{ value }}{% endmacro %}{% endblock %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.count, (size_t)2u);
    check_equal(jinja_template_analyze_frame(source, &tree, 2u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)1u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(jinja_template_analyze_frame(source, &tree, 5u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)0u);
    check(scope.parent == &parent);
  }

  it("does not turn block names or required modifiers into variable bindings") {
    vstr source = vstr_from_cstr("{% block 内容 scoped required %} {# placeholder #}{% endblock 内容 %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)0u);
  }

  it("binds self and super as block parameters when discovered in its body") {
    vstr source = vstr_from_cstr("{% block content %}{{ self.content() }}{{ super() }}{% endblock %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)2u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
  }

  it("injects root self discovered through a nested macro but not through a block") {
    vstr source = vstr_from_cstr("{% macro m() %}{{ self.b() }}{% endmacro %}{% block b %}{{ super() }}{% endblock %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_SCOPE_SYMBOL *self = template_scope_symbol(&parent, "self");
    check_not_null(self);
    if (self != NULL) check_equal(self->load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_null(template_scope_symbol(&parent, "super"));
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)0u);
  }

  it("does not inject a special parameter when an earlier structural store shadows it") {
    vstr source = vstr_from_cstr("{% set self=self %}{{ self }}{% block b %}{% set super=super %}{{ super }}{% endblock %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.symbols[0].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(jinja_template_analyze_frame(source, &tree, 2u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
  }

  it("separates loop body test and else bindings from the iterable parent") {
    vstr source = vstr_from_cstr("{% for x,y in items if x and predicate %}{{ y }}"
        "{% set body=1 %}{% else %}{{ x }}{% set fallback=1 %}{% endfor %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.count, (size_t)1u);
    check_equal(jinja_template_analyze_for_frame(source, &tree, 0u, JINJA_TEMPLATE_FOR_BODY, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check(template_scope_symbol(&scope, "body") != NULL);
    check(template_scope_symbol(&scope, "predicate") == NULL);
    check_equal(jinja_template_analyze_for_frame(source, &tree, 0u, JINJA_TEMPLATE_FOR_TEST, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    check(template_scope_symbol(&scope, "predicate") != NULL);
    check(template_scope_symbol(&scope, "body") == NULL);
    check_equal(jinja_template_analyze_for_frame(source, &tree, 0u, JINJA_TEMPLATE_FOR_ELSE, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)2u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check(template_scope_symbol(&scope, "fallback") != NULL);
    check(template_scope_symbol(&scope, "y") == NULL);
  }

  it("deduplicates Unicode tuple parameters and treats absent loop branches as empty") {
    vstr source = vstr_from_cstr("前缀{% for 名称,名称 in items %}{{ 名称 }}{% endfor %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 1u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)1u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[0].name.offset, (size_t)(strstr(source.data, "名称") - source.data));
    check_equal(jinja_template_analyze_for_frame(source, &tree, 1u, JINJA_TEMPLATE_FOR_TEST, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)1u);
    check_equal(jinja_template_analyze_for_frame(source, &tree, 1u, JINJA_TEMPLATE_FOR_ELSE, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)0u);
  }

  it("keeps nested loops and conditional stores inside the selected body boundary") {
    vstr source = vstr_from_cstr("{% for x in items %}{% if flag %}{% set local=1 %}"
        "{% else %}{% set local=2 %}{% endif %}{% for y in nested %}{{ hidden }}"
        "{% else %}{{ hidden_else }}{% endfor %}{% else %}{{ outer_else }}{% endfor %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)4u);
    const JINJA_EXPRESSION_SCOPE_SYMBOL *local = template_scope_symbol(&scope, "local");
    check(local != NULL);
    if (local != NULL) check_equal(local->load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check(template_scope_symbol(&scope, "nested") != NULL);
    check(template_scope_symbol(&scope, "outer_else") == NULL);
    check(template_scope_symbol(&scope, "hidden") == NULL);
  }

  it("does not borrow loop-body parameters when resolving the else frame") {
    vstr source = vstr_from_cstr("{% set x=1 %}{% for x in items if x %}{{ x }}{% else %}{{ x }}{% endfor %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_for_frame(source, &tree, 1u, JINJA_TEMPLATE_FOR_BODY, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(jinja_template_analyze_for_frame(source, &tree, 1u, JINJA_TEMPLATE_FOR_ELSE, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)0u);
    check(scope.parent == &parent);
  }

  it("rejects invalid loop branch selections and broken else links atomically") {
    vstr source = vstr_from_cstr("{% for x in items %}{% else %}{{ other }}{% endfor %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    scope.count = 1u;
    JINJA_EXPRESSION_SCOPE previous = scope;
    check_equal(jinja_template_analyze_for_frame(source, &tree, 0u, (JINJA_TEMPLATE_FOR_BRANCH)-1, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_template_analyze_for_frame(source, &tree, 1u, JINJA_TEMPLATE_FOR_BODY, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    tree.nodes[0].branch = tree.count;
    check_equal(jinja_template_analyze_for_frame(source, &tree, 0u, JINJA_TEMPLATE_FOR_ELSE, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
  }

  it("binds all macro parameters before defaults and resolves against the completed parent") {
    vstr source = vstr_from_cstr("{% macro m(a=b,b=outside) %}{{ a }}"
        "{% set outside=outside %}{% set local=1 %}{{ local }}{% endmacro %}"
        "{% set outside=1 %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.count, (size_t)2u);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)4u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    const JINJA_EXPRESSION_SCOPE_SYMBOL *alias = template_scope_symbol(&scope, "outside");
    check(alias != NULL);
    if (alias != NULL) {
      check_equal(alias->load, JINJA_EXPRESSION_SCOPE_ALIAS);
      check_equal(alias->parent_depth, (size_t)1u);
      check_equal(alias->parent_symbol, (size_t)1u);
    }
    check_equal(scope.symbols[3].load, JINJA_EXPRESSION_SCOPE_UNDEFINED);
  }

  it("keeps with initializers in the parent and declares only targets in the child") {
    vstr source = vstr_from_cstr("{% with x=outside, (y,z)=pair, x=again %}{{ x }}{{ y }}{{ z }}{{ body }}{% endwith %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.count, (size_t)3u);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)4u);
    for (size_t i = 0u; i < 3u; ++i) {
      check_equal(scope.symbols[i].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
      check_equal(scope.symbols[i].stored, 1);
    }
    check(template_scope_symbol(&scope, "outside") == NULL);
    check(template_scope_symbol(&scope, "pair") == NULL);
    check(template_scope_symbol(&scope, "again") == NULL);
    check(template_scope_symbol(&scope, "body") != NULL);
  }

  it("initializes a shadowing with target as a parameter instead of a parent alias") {
    vstr source = vstr_from_cstr("{% with 名称=名称 %}{{ 名称 }}{% endwith %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)1u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[0].parent_depth, (size_t)0u);
    check_equal(scope.symbols[0].parent_symbol, SIZE_MAX);
    check_equal(scope.symbols[0].name.offset, (size_t)(strstr(source.data, "名称") - source.data));
  }

  it("analyzes filter arguments after the body and preserves parent-frame references") {
    vstr source = vstr_from_cstr("{% filter replace(x,external) %}{% set x=1 %}{% endfilter %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)2u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_UNDEFINED);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.count, (size_t)2u);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)1u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ALIAS);
  }

  it("analyzes capture bodies without importing their outer targets or tail filters") {
    vstr source = vstr_from_cstr("{% set result|replace(filter_only,other) %}"
        "{{ body }}{% set local=1 %}{% endset %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.count, (size_t)1u);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)2u);
    check(template_scope_symbol(&scope, "body") != NULL);
    check(template_scope_symbol(&scope, "local") != NULL);
    check(template_scope_symbol(&scope, "result") == NULL);
    check(template_scope_symbol(&scope, "filter_only") == NULL);
  }

  it("reads autoescape policy inside its isolated frame before any body stores") {
    vstr source = vstr_from_cstr("{% autoescape policy %}{% set policy=true %}"
        "{% with x=inner_input %}{{ hidden }}{% endwith %}{{ visible }}{% endautoescape %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(parent.count, (size_t)0u);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, &parent, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(scope.symbols[0].stored, 1);
    check(template_scope_symbol(&scope, "inner_input") != NULL);
    check(template_scope_symbol(&scope, "hidden") == NULL);
  }

  it("does not publish an isolated frame when its body has unsupported semantics") {
    static const char *sources[] = {"{% with x=value %}{% debug %}{% endwith %}",
        "{% filter upper %}{% debug %}{% endfilter %}",
        "{% set result %}{% debug %}{% endset %}",
        "{% autoescape true %}{% debug %}{% endautoescape %}"};
    scope.count = 1u;
    JINJA_EXPRESSION_SCOPE previous = scope;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      vstr source = vstr_from_cstr(sources[i]);
      size_t error = SIZE_MAX;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DEBUG, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, &error), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
      check_equal(error, tree.nodes[1].source.offset);
      check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
    }
  }

  it("reads assignment RHS and namespace owners without binding attributes") {
    vstr source = vstr_from_cstr("{% set x=x %}{% set y=1 %}{{ y }}{% set ns.value=x %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(scope.symbols[0].stored, 1);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_UNDEFINED);
    check_equal(scope.symbols[2].stored, 0);
    check(template_scope_symbol(&scope, "value") == NULL);
  }

  it("initializes conditional stores without leaking branch state past endif") {
    vstr source = vstr_from_cstr("{% if flag %}{% set x=1 %}"
        "{% if inner %}{% set y=1 %}{% endif %}{% elif other %}"
        "{% macro m() %}{{ hidden }}{% endmacro %}{% else %}{% set x=2 %}"
        "{% endif %}{% set z=1 %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)7u);
    const JINJA_EXPRESSION_SCOPE_SYMBOL *x = template_scope_symbol(&scope, "x");
    const JINJA_EXPRESSION_SCOPE_SYMBOL *m = template_scope_symbol(&scope, "m");
    check(x != NULL); check(m != NULL);
    if (x != NULL) check_equal(x->load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    if (m != NULL) check_equal(m->load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(scope.symbols[6].load, JINJA_EXPRESSION_SCOPE_UNDEFINED);
    check(template_scope_symbol(&scope, "hidden") == NULL);
  }

  it("preserves target order when tuple stores and namespace owner reads share a name") {
    static const char *sources[] = {"{% set ns, ns.value = 1, 2 %}",
        "{% set ns.value, ns = 1, 2 %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      vstr source = vstr_from_cstr(sources[i]);
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(scope.count, (size_t)1u);
      check_equal(scope.symbols[0].load, i == 0u ? JINJA_EXPRESSION_SCOPE_UNDEFINED : JINJA_EXPRESSION_SCOPE_RESOLVE);
      check_equal(scope.symbols[0].stored, 1);
    }
  }

  it("visits only parent-frame headers of nested scope boundaries") {
    vstr source = vstr_from_cstr("{% for item in items if predicate %}{{ loop_hidden }}{% else %}{{ else_hidden }}{% endfor %}"
        "{% with x=first,y=second %}{{ with_hidden }}{% endwith %}"
        "{% filter replace(old,new) %}{{ filter_hidden }}{% endfilter %}"
        "{% set captured|replace(capture_old,capture_new) %}{{ capture_hidden }}{% endset %}"
        "{% macro child(p=default_hidden) %}{{ macro_hidden }}{% endmacro %}"
        "{% call(p=call_default_hidden) render(arg) %}{{ call_hidden }}{% endcall %}"
        "{% block content %}{{ block_hidden }}{% endblock %}"
        "{% autoescape escape_hidden %}{{ auto_hidden }}{% endautoescape %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    static const char *expected[] = {"items", "first", "second", "old", "new", "captured", "child", "render", "arg"};
    check_equal(scope.count, sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i)
      check(template_scope_symbol(&scope, expected[i]) != NULL);
  }

  it("analyzes call body parameters and defaults separately from its call expression") {
    vstr source = vstr_from_cstr("{% call(p=fallback) render(arg) %}{{ p }}{{ body }}{% endcall %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check(template_scope_symbol(&scope, "fallback") != NULL);
    check(template_scope_symbol(&scope, "body") != NULL);
    check(template_scope_symbol(&scope, "render") == NULL);
  }

  it("reads template references before declaring imported aliases") {
    vstr source = vstr_from_cstr("{% extends base %}{% include templates ignore missing %}"
        "{% import module as module %}{% from library import first as local,second %}"
        "{% print local,second %}{% do action(local) %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DO, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)7u);
    check_equal(scope.symbols[2].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(scope.symbols[2].stored, 1);
    check_equal(scope.symbols[4].load, JINJA_EXPRESSION_SCOPE_UNDEFINED);
    check(template_scope_symbol(&scope, "first") == NULL);
  }

  it("retains full original byte spans for Unicode macro parameters and body names") {
    vstr source = vstr_from_cstr("前缀\r\n{% macro 显示(名字=外部) %}{% set 本地=名字 %}{{本地}}{% endmacro %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, 1u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    check(template_scope_symbol(&scope, "名字") != NULL);
    check(template_scope_symbol(&scope, "外部") != NULL);
    check(template_scope_symbol(&scope, "本地") != NULL);
    check_equal(scope.symbols[0].name.offset, (size_t)(strstr(source.data, "名字") - source.data));
  }

  it("rejects unhandled extension semantics atomically and reports source offsets") {
    vstr source = vstr_from_cstr("{% set x=1 %}{% debug %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DEBUG, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    scope.count = 1u;
    JINJA_EXPRESSION_SCOPE previous = scope;
    size_t error = SIZE_MAX;
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, &error), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
    check_equal(error, tree.nodes[1].source.offset);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, &error), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
    check_equal(jinja_template_analyze_frame(source, &tree, tree.count, NULL, &scope, &error), JINJA_EXPRESSION_PARSE_INVALID);
    tree.count = JINJA_TEMPLATE_MAX_NODES + 1u;
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, &error), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
  }

  it("enforces the scope symbol budget across separate template expressions") {
    enum { OUTPUT_BYTES = 24 };
    char source[(JINJA_EXPRESSION_MAX_REFERENCES + 1u) * OUTPUT_BYTES];
    size_t length = 0u;
    for (size_t i = 0u; i < JINJA_EXPRESSION_MAX_REFERENCES; ++i) {
      int written = snprintf(source + length, sizeof(source) - length, "{{ name%zu }}", i);
      check(written > 0 && (size_t)written < sizeof(source) - length);
      if (written <= 0 || (size_t)written >= sizeof(source) - length) break;
      length += (size_t)written;
    }
    vstr input = vstr_from_buf(source, length);
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, input, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(input, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)JINJA_EXPRESSION_MAX_REFERENCES);
    JINJA_EXPRESSION_SCOPE previous = scope;
    int written = snprintf(source + length, sizeof(source) - length, "{{ overflow }}");
    check(written > 0 && (size_t)written < sizeof(source) - length);
    if (written > 0 && (size_t)written < sizeof(source) - length) {
      input.len += (size_t)written;
      size_t error = SIZE_MAX;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, input, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(jinja_template_analyze_frame(input, &tree, SIZE_MAX, NULL, &scope, &error), JINJA_EXPRESSION_PARSE_CAPACITY);
      check_equal(error, length + sizeof("{{ ") - 1u);
      check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
    }
  }

  it("rejects broken metadata and parent output aliasing without publishing partial scopes") {
    vstr source = vstr_from_cstr("{% macro m() %}{{ x }}{% endmacro %}");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    JINJA_EXPRESSION_SCOPE previous = scope;
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, &scope, &scope, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    tree.nodes[1].header.length = SIZE_MAX;
    check_equal(jinja_template_analyze_frame(source, &tree, 0u, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    tree.nodes[1].header.length = 1u;
    tree.nodes[0].match = 0u;
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_template_analyze_frame(source, NULL, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_template_analyze_frame(vstr_from_buf(source.data, (size_t)JINJA_TEMPLATE_MAX_BYTES + 1u),
        &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
    source = vstr_from_cstr("");
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, source, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_frame(source, &tree, SIZE_MAX, NULL, &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)0u);
  }
}

spec("Jinja template structure parser") {
  static JINJA_TEMPLATE_TREE tree;
  before_each() { memset(&tree, 0, sizeof(tree)); }
  after_each() { jinja_template_tree_destroy(&tree); }

  it("preserves duplicate block declarations for compile time validation") {
    static const char source[] =
        "{% block body %}{% endblock %}{% block body %}{% endblock %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u,
        vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)4u);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_BLOCK);
    check_equal(tree.nodes[2].kind, JINJA_TEMPLATE_BLOCK);
    check_equal(tree.nodes[0].name.length, strlen("body"));
    check_equal(tree.nodes[2].name.length, strlen("body"));
    check_equal(memcmp(source + tree.nodes[0].name.offset,
        source + tree.nodes[2].name.offset, strlen("body")), 0);
    check_equal(tree.nodes[2].source.offset, strlen("{% block body %}{% endblock %}"));
  }

  it("retains block links across node storage growth and releases replaced trees") {
    enum { BLOCKS = 300, NODES_PER_BLOCK = 3 };
    static const char block[] = "{% if true %}x{% endif %}";
    char source[BLOCKS * (sizeof(block) - 1u) + 1u];
    for (size_t i = 0u; i < BLOCKS; ++i)
      memcpy(source + i * (sizeof(block) - 1u), block, sizeof(block) - 1u);
    source[sizeof(source) - 1u] = '\0';
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u,
        vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)(BLOCKS * NODES_PER_BLOCK));
    for (size_t i = 0u; i < BLOCKS; ++i) {
      size_t begin = i * NODES_PER_BLOCK;
      check_equal(tree.nodes[begin].match, begin + NODES_PER_BLOCK - 1u);
      check_equal(tree.nodes[begin + 1u].parent, begin);
      check_equal(tree.nodes[begin + NODES_PER_BLOCK - 1u].match, begin);
    }
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u,
        vstr_from_cstr(""), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)0u);
    jinja_template_tree_destroy(&tree);
    check_null(tree.storage);
    check_null(tree.nodes);
    jinja_template_tree_destroy(&tree);
  }

  it("preserves previous node contents when a growing parse fails") {
    enum { SEGMENTS = 300 };
    static const char segment[] = "a{#c#}";
    static const char tail[] = "{% if true %}";
    char source[SEGMENTS * (sizeof(segment) - 1u) + sizeof(tail)];
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u,
        vstr_from_cstr("{% if true %}ok{% endif %}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    JINJA_TEMPLATE_NODE previous[3];
    memcpy(previous, tree.nodes, sizeof(previous));
    JINJA_TEMPLATE_NODE *retained = tree.nodes;
    for (size_t i = 0u; i < SEGMENTS; ++i)
      memcpy(source + i * (sizeof(segment) - 1u), segment, sizeof(segment) - 1u);
    memcpy(source + SEGMENTS * (sizeof(segment) - 1u), tail, sizeof(tail));
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u,
        vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(tree.count, (size_t)3u);
    check_equal((const void *)tree.nodes, (const void *)retained);
    check_equal(tree.nodes, previous, sizeof(previous));
  }

  it("matches newline delimiters against normalized source while retaining byte spans") {
    static const char *sources[] = {"<\n42\n>", "<\r42\r>", "<\r\n42\r\n>"};
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[0] = vstr_from_cstr("<\n");
    config.tokens[1] = vstr_from_cstr("\n>");
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(tree.count, (size_t)1u);
      check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_OUTPUT);
      check_equal(tree.nodes[0].source.length, strlen(sources[i]));
      check_equal(tree.nodes[0].header.length, (size_t)2u);
      check_equal(memcmp(sources[i] + tree.nodes[0].header.offset, "42", 2u), 0);
    }
  }

  it("uses normalized newline matching in raw endings and line prefixes") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[2] = vstr_from_cstr("<\n");
    config.tokens[3] = vstr_from_cstr("\n>");
    const char *source = "<\r\nraw\r\n>X<\rendraw\r>";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[0].source.length, strlen(source));
    check_equal(tree.nodes[0].content.length, (size_t)1u);
    check_equal(source[tree.nodes[0].content.offset], 'X');
    config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.line_statement_prefix = vstr_from_cstr("#\n");
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("#\r\nset x=1\n"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_SET);
    check_equal(tree.nodes[0].header.offset, (size_t)7u);
  }

  it("does not normalize configuration strings containing carriage return") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[0] = vstr_from_cstr("<\r");
    config.tokens[1] = vstr_from_cstr(">");
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("<\r42>"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_TEXT);
  }

  it("bounds normalized delimiter matching on nonterminated and truncated sources") {
    const char source[] = {'<', '\r', '\n', '4', '2', '\r', '\n', '>'};
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[0] = vstr_from_cstr("<\n");
    config.tokens[1] = vstr_from_cstr("\n>");
    config.keep_trailing_newline = 1;
    check_equal(jinja_template_parse(&config, 0u, vstr_from_buf(source, sizeof(source)), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    static JINJA_TEMPLATE_TREE previous;
    check_equal(tree.count, (size_t)1u);
    JINJA_TEMPLATE_NODE previous_node = tree.nodes[0];
    previous = tree;
    for (size_t length = 2u; length < sizeof(source); ++length) {
      size_t failure = SIZE_MAX;
      check_equal(jinja_template_parse(&config, 0u, vstr_from_buf(source, length), &tree, &failure), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
      check_equal(tree.nodes, &previous_node, sizeof(previous_node));
      check_equal(failure, (size_t)0u);
    }
  }

  it("matches signed normalized comment and raw endings without splitting CRLF") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[4] = vstr_from_cstr("<\n#");
    config.tokens[5] = vstr_from_cstr("\n>");
    const char *source = "<\r\n#comment-\r\n> X";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_COMMENT);
    check_equal(tree.nodes[0].right_control, -1);
    check_equal(tree.nodes[1].content.length, (size_t)1u);
    check_equal(source[tree.nodes[1].content.offset], 'X');
    config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[2] = vstr_from_cstr("<\n");
    config.tokens[3] = vstr_from_cstr("\n>");
    source = "<\r\nraw-\r\n> X <\r\n-endraw+\r\n>";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[0].content.length, (size_t)1u);
    check_equal(source[tree.nodes[0].content.offset], 'X');
    check_equal(tree.nodes[0].right_control, 1);
    config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[0] = vstr_from_cstr("\n\n");
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("\r\n42}}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_TEXT);
  }

  it("validates named escapes in headers but leaves raw and comment bodies alone") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.line_statement_prefix = vstr_from_cstr("#");
    static const char *bad[] = {"{{ '\\N{bad}' }}", "{% set x='\\N{}' %}", "# set x='\\N{bad}'\n"};
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
      tree.count = 1u;
      check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(bad[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)1u);
    }
    const char *source = "{% raw %}{{ '\\N{bad}' }}{% endraw %}{# '\\N{}' #}"
        "{{ '\\N{LF}' }}\n# set x='\\N{LATIN CAPITAL LETTER A}'\n";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("keeps Unicode integer tokens intact at configured template endings") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[1] = vstr_from_cstr(".0");
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("{{ x.1\u0662.0"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].header.length, strlen("x.1\u0662"));
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u,
        vstr_from_cstr("{% set x=0x\u0661f %}{{ 1\uff12 }}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }
  it("rejects Unicode floats instead of reinterpreting them as integer item lookups") {
    static const char *sources[] = {"{{ 1\u0662.3 }}", "{{ x.\u0661.0 }}", "{% set x=1e\u0662 %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      tree.count = 1u;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)1u);
    }
  }

  it("accepts calls on filtered results in output macro defaults and call blocks") {
    const char *source = "{{x|f()(1)}}{% macro m(x=y is f()(1)) %}{% endmacro %}"
        "{% call x|f()() %}body{% endcall %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("rejects out of range Unicode escapes in ordinary and line statement headers") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.line_statement_prefix = vstr_from_cstr("#");
    static const char *sources[] = {"{{ '\\U00110000' }}", "{% set x='\\UFFFFFFFF' %}",
        "# set x='\\U00110000'\n"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      tree.count = 1u;
      check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)1u);
    }
    const char *raw = "{% raw %}{{ '\\UFFFFFFFF' }}{% endraw %}{# '\\U00110000' #}";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(raw), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("rejects malformed string escapes in output and statement headers") {
    static const char *sources[] = {"{{ '\\xZ1' }}", "{% set x='\\u12' %}",
        "{% macro f(x='\\U123') %}{% endmacro %}", "{% include '\\x' %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      tree.count = 1u;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)1u);
    }
  }

  it("does not interpret malformed escapes in raw bodies or comments") {
    const char *source = "{% raw %}{{ '\\xZ1' }}{% endraw %}{# '\\u12' #}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[1].kind, JINJA_TEMPLATE_COMMENT);
  }

  it("rejects malformed quoted tokens before counting their internal brackets") {
    enum { BRACKETS = JINJA_EXPRESSION_MAX_NODES + 1u, SOURCE_CAPACITY = BRACKETS + 32u };
    char source[SOURCE_CAPACITY];
    const char *prefix = "{{ '\\xZ1";
    size_t length = strlen(prefix), offset = SIZE_MAX;
    memcpy(source, prefix, length);
    memset(source + length, '[', BRACKETS);
    length += BRACKETS;
    memcpy(source + length, "' }}", sizeof("' }}") - 1u);
    length += sizeof("' }}") - 1u;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf(source, length), &tree, &offset), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)3u);
  }

  it("recognizes custom endings after a numeric dot index token") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[1] = vstr_from_cstr(".1");
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("{{ x.0.1"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].header.length, strlen("x.0"));
  }

  it("projects explicit whitespace controls without rewriting original spans") {
    const char *source = "A \xc2\xa0{{- x -}} \r\nB";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].source.length, strlen("A \xc2\xa0"));
    check_equal(tree.nodes[0].content.length, (size_t)1u);
    check_equal(tree.nodes[2].content.length, (size_t)1u);
    check_equal(source[tree.nodes[2].content.offset], 'B');
  }

  it("projects raw inner controls independently of its outer controls") {
    const char *source = "A {%+ raw -%} \r\nBODY \xc2\xa0{%- endraw +%} B";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[1].content.length, strlen("BODY"));
    check_equal(memcmp(source + tree.nodes[1].content.offset, "BODY", strlen("BODY")), 0);
    check_equal(tree.nodes[1].left_control, 1);
    check_equal(tree.nodes[1].right_control, 1);
    check_equal(tree.nodes[1].raw_left_control, -1);
    check_equal(tree.nodes[1].raw_right_control, -1);
    check_equal(tree.nodes[1].header.length, strlen(" \r\nBODY \xc2\xa0"));
  }

  it("removes only the last physical newline unless explicitly retained") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    const char *source = "text\r\n\r\n";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].content.length, strlen("text\r\n"));
    config.keep_trailing_newline = 1;
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].content.length, strlen(source));
  }

  it("applies block trim and Unicode line indentation without stripping variables") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.trim_blocks = config.lstrip_blocks = config.keep_trailing_newline = 1;
    const char *source = "A\r\n \xc2\xa0{% if true %}\r\n B\r\n  {% endif %}\r\n  {{ x }}\n";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].content.length, strlen("A\r\n"));
    check_equal(tree.nodes[2].content.length, strlen(" B\r\n"));
    check_equal(tree.nodes[4].content.length, (size_t)2u);
    check_equal(tree.nodes[6].content.length, (size_t)1u);
  }

  it("lets explicit plus controls preserve block whitespace") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.trim_blocks = config.lstrip_blocks = config.keep_trailing_newline = 1;
    const char *source = "  {%+ if true +%}\nX\n  {%+ endif +%}\n";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].content.length, (size_t)2u);
    check_equal(tree.nodes[2].content.length, strlen("\nX\n  "));
    check_equal(tree.nodes[4].content.length, (size_t)1u);
  }

  it("rejects nonboolean whitespace configuration atomically") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.trim_blocks = -1;
    tree.count = 1u;
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("x"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(tree.count, (size_t)1u);
  }

  it("projects whitespace within lexical data boundaries across tag families") {
    static const struct {
      const char *source, *expected;
      int trim, lstrip, keep;
      const char *statement, *comment;
    } cases[] = {
      {"{% raw %}\nX\n{% endraw %}\n", "\nX\n", 1, 0, 0, NULL, NULL},
      {"{% raw %}\nX\n  {% endraw %}\n", "\nX\n\n", 0, 1, 1, NULL, NULL},
      {"{{x}}  {% if true %}X{% endif %}", "  X", 0, 1, 0, NULL, NULL},
      {" A {# a #} \t{#- b #} B ", " A  B ", 0, 0, 0, NULL, NULL},
      {"  {#+ a +#}\nX", "  \nX", 1, 1, 0, NULL, NULL},
      {"  {# a #}\r\nX", "X", 1, 1, 0, NULL, NULL},
      {"A ## c\nB", "A\nB", 1, 0, 0, NULL, "##"},
      {"A \n  #+ set x=1\nB", "A \nB", 0, 1, 0, "#", NULL},
      {"A \n  #- set x=1\nB", "AB", 0, 0, 0, "#", NULL},
      {"{% raw %}X  {% endraw %}  {{-x}}", "X  ", 0, 0, 0, NULL, NULL},
      {"{% set x=1 %}\r\n\r\n", "", 1, 0, 0, NULL, NULL},
      {"A\r\r", "A\r", 0, 0, 0, NULL, NULL}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      info("whitespace source: %s", cases[i].source);
      JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
      config.trim_blocks = cases[i].trim;
      config.lstrip_blocks = cases[i].lstrip;
      config.keep_trailing_newline = cases[i].keep;
      if (cases[i].statement != NULL) config.line_statement_prefix = vstr_from_cstr(cases[i].statement);
      if (cases[i].comment != NULL) config.line_comment_prefix = vstr_from_cstr(cases[i].comment);
      check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(cases[i].source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      size_t offset = 0u, length = strlen(cases[i].expected);
      for (size_t j = 0u; j < tree.count; ++j) {
        if (tree.nodes[j].kind != JINJA_TEMPLATE_TEXT && tree.nodes[j].kind != JINJA_TEMPLATE_RAW) continue;
        JINJA_EXPRESSION_SPAN span = tree.nodes[j].content;
        check_less_equal(span.length, length - offset);
        check_equal(memcmp(cases[i].source + span.offset, cases[i].expected + offset, span.length), 0);
        offset += span.length;
      }
      check_equal(offset, length);
    }
  }

  it("applies right strip before recognizing subsequent line statements") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.line_statement_prefix = vstr_from_cstr("#");
    const char *source = "{{ x -}}\n  # set y=1\n{{y}}";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)3u);
    check_equal(tree.nodes[1].kind, JINJA_TEMPLATE_TEXT);
    check_equal(tree.nodes[1].content.length, strlen("# set y=1\n"));
    check_equal(memcmp(source + tree.nodes[1].content.offset, "# set y=1\n", tree.nodes[1].content.length), 0);
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("{{ x -}}\n# set y=1\n{{y}}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[2].kind, JINJA_TEMPLATE_SET);
  }

  it("applies right strip before matching delimiters with leading whitespace") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[0] = vstr_from_cstr("  {{");
    const char *source = "  {{ x -}}  {{ y }}";
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)2u);
    check_equal(tree.nodes[1].kind, JINJA_TEMPLATE_TEXT);
    check_equal(tree.nodes[1].content.length, strlen("{{ y }}"));
  }

  it("removes the trailing newline before matching a newline closing delimiter") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[1] = vstr_from_cstr("\n");
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("{{ x\n"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    config.keep_trailing_newline = 1;
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("{{ x\n"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("consumes raw opening strip before matching an indented endraw delimiter") {
    JINJA_TEMPLATE_DELIMITERS config = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    config.tokens[2] = vstr_from_cstr("  {%");
    check_equal(jinja_template_parse(&config, 0u, vstr_from_cstr("  {% raw -%}  {% endraw %}"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
  }

  it("parses indented line statements and inline comments") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.line_statement_prefix = vstr_from_cstr("#");
    delimiters.line_comment_prefix = vstr_from_cstr("##");
    const char *source = "  # if true:\nYES ## comment\n  # endif";
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_IF);
    check_equal(tree.count, (size_t)5u);
    check_equal(tree.nodes[0].line_statement, 1);
    check_equal(tree.nodes[0].match, (size_t)4u);
    check_equal(tree.nodes[2].kind, JINJA_TEMPLATE_COMMENT);
    check_equal(tree.nodes[3].source.length, (size_t)1u);
  }

  it("continues line statements across balanced containers and retains CRLF bytes") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.line_statement_prefix = vstr_from_cstr("#");
    const char source[] = "# set xs = [\r\n1,\r\n2\r\n]\r\n{{ xs }}";
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_buf(source, sizeof(source) - 1u), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)2u);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_SET);
    check_equal(tree.nodes[0].line_statement, 1);
    check_equal(tree.nodes[0].header.length, strlen("xs = [\r\n1,\r\n2\r\n]"));
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("# set x = 'a\nb'\n{{x}}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("keeps line prefixes out of ordinary text strings and raw bodies") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.line_statement_prefix = vstr_from_cstr("#");
    delimiters.line_comment_prefix = vstr_from_cstr("##");
    const char *source = "text # if true\n{% raw %}\n# invalid\n## data\n{% endraw %}{{ '## data' }}";
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)3u);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_TEXT);
    check_equal(tree.nodes[1].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[2].kind, JINJA_TEMPLATE_OUTPUT);
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("# raw\n# endraw"), &tree, NULL), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
  }

  it("captures inline Unicode whitespace with a comment but leaves the newline as text") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.line_comment_prefix = vstr_from_cstr("##");
    const char source[] = "字\xc2\xa0## ignored\r\nnext";
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)3u);
    check_equal(tree.nodes[1].source.offset, strlen("字"));
    check_equal(tree.nodes[2].source.length, strlen("\r\nnext"));
  }

  it("respects line prefix priority and nonempty or empty enabled prefixes") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.line_statement_prefix = vstr_from_cstr("#");
    delimiters.tokens[0] = vstr_from_cstr("#");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("# 1 }}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_OUTPUT);
    delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.line_statement_prefix = vstr_from_cstr("");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("set x=1"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_SET);
    delimiters.line_statement_prefix = (vstr){0};
    delimiters.line_comment_prefix = vstr_from_cstr("");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("ignored\n\nnext"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("ranks Unicode line prefixes by scalar count rather than byte length") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.tokens[0] = vstr_from_cstr("  ");
    delimiters.line_comment_prefix = vstr_from_cstr("字");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("  字 }}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_OUTPUT);
    delimiters.tokens[0] = vstr_from_cstr(" ");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr(" 字 }}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_OUTPUT);
  }

  it("rejects malformed line statements and invalid prefix configuration atomically") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.line_statement_prefix = vstr_from_cstr("#");
    static const char *sources[] = {"# if true", "# if true\n# endif extra", "# set x=([)]", "# set x=[\n1"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      tree.count = 1u;
      check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)1u);
    }
    delimiters.line_statement_prefix = vstr_from_cstr("\xff");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("x"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    delimiters.line_statement_prefix = (vstr){.data = NULL, .len = 1u};
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("x"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
  }

  it("recognizes configured variable statement and comment delimiters") {
    JINJA_TEMPLATE_DELIMITERS delimiters = {.tokens = {
        {.data = "<%=", .len = 3u}, {.data = "%>", .len = 2u},
        {.data = "<%", .len = 2u}, {.data = "%>", .len = 2u},
        {.data = "<#", .len = 2u}, {.data = "#>", .len = 2u}}};
    const char *source = "<% if true %><%= 'ok' %><# ignored #><% endif %>";
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)4u);
  }

  it("parses translation blocks with context bindings and plural forms") {
    const char *source = "{% trans 'menu' n=count, trimmed %}{{ n }} item{% pluralize n %}{{ n }} items{% endtrans %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_I18N, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)7u);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_TRANS);
    check_equal(tree.nodes[0].translation_trim, 1);
    check_equal(tree.nodes[0].match, (size_t)6u);
    check_equal(tree.nodes[0].branch, (size_t)3u);
    check_equal(tree.nodes[3].kind, JINJA_TEMPLATE_PLURALIZE);
    check_equal(tree.nodes[4].parent, (size_t)3u);
  }

  it("preserves Unicode delimiters and quoted endings with exact byte spans") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.tokens[JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN] = vstr_from_cstr("【");
    delimiters.tokens[JINJA_TEMPLATE_TOKEN_VARIABLE_CLOSE - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN] = vstr_from_cstr("】");
    const char source[] = "【 {'名称': '】'} 】";
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_buf(source, sizeof(source) - 1u), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)1u);
    check_equal(tree.nodes[0].header.offset, strlen("【 "));
    check_equal(tree.nodes[0].header.length, strlen("{'名称': '】'}"));
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_buf(source, sizeof(source) - 2u), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("{{ untouched }}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_TEXT);
  }

  it("finds overlapping raw endings using configured statement delimiters") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.tokens[JINJA_TEMPLATE_TOKEN_STATEMENT_OPEN - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN] = vstr_from_cstr("aaa");
    delimiters.tokens[JINJA_TEMPLATE_TOKEN_STATEMENT_CLOSE - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN] = vstr_from_cstr("ZZ");
    const char source[] = "aaarawZZaaaaendrawZZ";
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)1u);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[0].header.length, (size_t)1u);
    check_equal(source[tree.nodes[0].header.offset], 'a');
  }

  it("rejects invalid delimiter configuration before publishing nodes") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    tree.count = 1u;
    check_equal(jinja_template_parse(NULL, 0u, vstr_from_cstr("x"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    delimiters.tokens[0] = vstr_from_cstr("");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("x"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    delimiters.tokens[0] = vstr_from_cstr("{%" );
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("x"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    delimiters.tokens[0] = vstr_from_cstr("\xff");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("x"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    char oversized[JINJA_TEMPLATE_MAX_DELIMITER_BYTES + 1u];
    memset(oversized, 'x', sizeof(oversized));
    delimiters.tokens[0] = vstr_from_buf(oversized, sizeof(oversized));
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("x"), &tree, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(tree.count, (size_t)1u);
  }

  it("does not split expression tokens at an embedded end delimiter") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.tokens[0] = vstr_from_cstr("<<");
    delimiters.tokens[1] = vstr_from_cstr("bc");
    const char *source = "<< abc bc";
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)1u);
    check_equal(tree.nodes[0].header.length, (size_t)3u);
  }

  it("gives raw opening precedence over longer ordinary delimiters") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.tokens[0] = vstr_from_cstr("< raw");
    delimiters.tokens[1] = vstr_from_cstr("!");
    delimiters.tokens[2] = vstr_from_cstr("<");
    delimiters.tokens[3] = vstr_from_cstr(">");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("< raw >hello< endraw >"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[0].header.length, (size_t)5u);
  }

  it("distinguishes raw end controls from delimiter leading signs") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.tokens[2] = vstr_from_cstr("<%");
    delimiters.tokens[3] = vstr_from_cstr("-%>");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("<% raw-%>hello<% endraw-%>"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[0].right_control, 0);
  }

  it("allows raw ending delimiters to retain their leading whitespace") {
    JINJA_TEMPLATE_DELIMITERS delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    delimiters.tokens[2] = vstr_from_cstr("<%");
    delimiters.tokens[3] = vstr_from_cstr(" %>");
    check_equal(jinja_template_parse(&delimiters, 0u, vstr_from_cstr("<% raw %>hello<% endraw %>"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[0].header.length, (size_t)5u);
  }


  it("accepts translation lexical names modifiers and implicit singular counts") {
    static const char *sources[] = {"{% trans %}hello{% endtrans %}",
        "{% trans : %}hello{% endtrans %}", "{% trans trimmed n=1 %}{{ n }}{% endtrans %}",
        "{% trans n, : %}{{ n }}{% endtrans %}", "{% trans notrimmed %}x{% endtrans %}",
        "{% trans %}{{ 数量 }}{% pluralize %}{{ 数量 }}{% endtrans %}",
        "{% trans true=1, not=f(1,2) %}{{ true }}{{ not }}{% pluralize not %}many{% endtrans %}",
        "{% trans %}{# hidden #}{% raw %}{{ raw }}{% endraw %}{% endtrans %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("translation: %s", sources[i]);
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_I18N, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    }
  }

  it("rejects invalid translation bindings bodies and pluralization atomically") {
    static const char *sources[] = {"{% trans x,x %}{% endtrans %}", "{% trans x: %}{% endtrans %}",
        "{% trans x, %}{% endtrans %}", "{% trans x= %}{% endtrans %}",
        "{% trans %}{{ x.y }}{% endtrans %}", "{% trans %}{{ x+1 }}{% endtrans %}",
        "{% trans %}{% if x %}{% endif %}{% endtrans %}", "{% trans %}{% trans %}{% endtrans %}{% endtrans %}",
        "{% trans %}one{% pluralize %}many{% endtrans %}",
        "{% trans %}{{ x }}{% pluralize x %}many{% endtrans %}",
        "{% trans x %}one{% pluralize %}many{% pluralize %}more{% endtrans %}",
        "{% trans x %}one{% pluralize x,y %}many{% endtrans %}",
        "{% trans %}x", "{% pluralize %}", "{% trans 'a' 'b' %}x{% endtrans %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("invalid translation: %s", sources[i]);
      tree.count = 1u;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_I18N, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)1u);
    }
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr("{% trans %}x{% endtrans %}"), &tree, NULL), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
  }

  it("parses expression statement extension syntax") {
    static const char *sources[] = {"{% do f() %}", "{% do 1,2, %}", "{% do x|custom %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DO, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("bounds translation bindings and preserves the complete previous tree on failure") {
    enum { SOURCE_CAPACITY = 2048, TRANSLATION_NODES = 5 };
    JINJA_TEMPLATE_NODE previous_nodes[TRANSLATION_NODES];
    char source[SOURCE_CAPACITY] = "{% trans ";
    size_t used = strlen(source);
    static JINJA_TEMPLATE_TREE previous;
    for (size_t i = 0u; i <= JINJA_EXPRESSION_MAX_NODES; ++i) {
      int written = snprintf(source + used, sizeof(source) - used, "%sn%zu", i == 0u ? "" : ",", i);
      check_greater(written, 0);
      check_less((size_t)written, sizeof(source) - used);
      used += (size_t)written;
      if (i + 1u < JINJA_EXPRESSION_MAX_NODES) continue;
      const char suffix[] = " %}one{% pluralize %}many{% endtrans %}";
      memcpy(source + used, suffix, sizeof(suffix));
      if (i < JINJA_EXPRESSION_MAX_NODES) {
        check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_I18N, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
        previous = tree;
        check_equal(tree.count, (size_t)TRANSLATION_NODES);
        memcpy(previous_nodes, tree.nodes, sizeof(previous_nodes));
      } else {
        check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_I18N, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
        check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
        check_equal(tree.nodes, previous_nodes, sizeof(previous_nodes));
      }
    }
  }

  it("retains translation context and trim policy without rewriting message bytes") {
    const char source[] = "{% trans 'menu' notrimmed %}  100%\n{% endtrans %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_I18N, vstr_from_buf(source, sizeof(source) - 1u), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].translation_trim, -1);
    check_equal(tree.nodes[0].name.offset, (size_t)9u);
    check_equal(tree.nodes[0].name.length, (size_t)6u);
    check_equal(tree.nodes[1].source.length, strlen("  100%\n"));
    check_equal(memcmp(source + tree.nodes[1].source.offset, "  100%\n", tree.nodes[1].source.length), 0);
  }

  it("requires explicit extension selection and rejects unsupported configuration bits") {
    static const char *sources[] = {"{% do f() %}", "{% break %}", "{% continue %}", "{% debug %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      tree.count = 1u;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
      check_equal(tree.count, (size_t)1u);
    }
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, ~0u, vstr_from_cstr(""), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(tree.count, (size_t)1u);
  }

  it("isolates extension switches and retains exact expression source") {
    const char source[] = "{% do 名称(1) %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DO, vstr_from_buf(source, sizeof(source) - 1u),
        &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_DO);
    check_equal(tree.nodes[0].header.offset, (size_t)6u);
    check_equal(tree.nodes[0].header.length, strlen("名称(1)"));
    check_equal(memcmp(source + tree.nodes[0].header.offset, "名称(1)", tree.nodes[0].header.length), 0);
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DO, vstr_from_cstr("{% break %}"), &tree, NULL),
        JINJA_EXPRESSION_PARSE_UNSUPPORTED);
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_LOOP_CONTROLS, vstr_from_cstr("{% debug %}"), &tree, NULL),
        JINJA_EXPRESSION_PARSE_UNSUPPORTED);
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, JINJA_TEMPLATE_EXTENSION_DEBUG, vstr_from_cstr("{% do 1 %}"), &tree, NULL),
        JINJA_EXPRESSION_PARSE_UNSUPPORTED);
  }

  it("retains loop control and debug nodes independently of lowering scope checks") {
    const unsigned extensions = JINJA_TEMPLATE_EXTENSION_LOOP_CONTROLS | JINJA_TEMPLATE_EXTENSION_DEBUG;
    const char *source = "{% for x in xs %}{% if x %}{% break %}{% endif %}{% continue %}{% endfor %}{% debug %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, extensions, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)7u);
    check_equal(tree.nodes[2].kind, JINJA_TEMPLATE_BREAK);
    check_equal(tree.nodes[2].parent, (size_t)1u);
    check_equal(tree.nodes[4].kind, JINJA_TEMPLATE_CONTINUE);
    check_equal(tree.nodes[4].parent, (size_t)0u);
    check_equal(tree.nodes[6].kind, JINJA_TEMPLATE_DEBUG);
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, extensions, vstr_from_cstr("{% break %}{% continue %}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].parent, SIZE_MAX);
  }

  it("rejects extension arguments malformed expressions and fake block endings") {
    const unsigned extensions = JINJA_TEMPLATE_EXTENSION_DO | JINJA_TEMPLATE_EXTENSION_LOOP_CONTROLS | JINJA_TEMPLATE_EXTENSION_DEBUG;
    static const char *sources[] = {"{% break 1 %}", "{% continue: %}", "{% debug x %}",
        "{% do x=1 %}", "{% do %}", "{% do f( %}", "{% do 1 %}{% enddo %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      tree.count = 1u;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, extensions, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)1u);
    }
  }

  it("accepts unregistered names in filter blocks captures and nested headers") {
    static const char *sources[] = {
        "{% filter true %}x{% endfilter %}",
        "{% filter group . 名称(1)|not %}x{% endfilter %}",
        "{% set x | group.false %}x{% endset %}",
        "{% macro m(x=value is custom) %}{{ x|group.foo }}{% endmacro %}",
        "{% if x is not group.true %}yes{% endif %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("template: %s", sources[i]);
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    }
  }

  it("pairs nested macro call loop and conditional blocks and records branches") {
    const char *source = "{% macro m(x) %}{% call f(x) %}{% for a,b in xs if a recursive %}"
        "{% if a %}yes{% elif b %}maybe{% else %}no{% endif %}"
        "{% else %}empty{% endfor %}{% endcall %}{% endmacro %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)15u);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_MACRO);
    check_equal(tree.nodes[0].match, (size_t)14u);
    check_equal(tree.nodes[1].match, (size_t)13u);
    check_equal(tree.nodes[2].match, (size_t)12u);
    check_equal(tree.nodes[3].match, (size_t)9u);
    check_equal(tree.nodes[3].branch, (size_t)5u);
    check_equal(tree.nodes[5].branch, (size_t)7u);
    check_equal(tree.nodes[8].parent, (size_t)7u);
    check_equal(tree.nodes[2].branch, (size_t)10u);
    check_equal(tree.nodes[12].match, (size_t)2u);
  }

  it("rejects mismatched missing and repeated branch terminators atomically") {
    static const char *sources[] = {"{% macro m() %}{% endcall %}", "{% for x in xs %}",
        "{% else %}", "{% if x %}{% else %}{% elif y %}{% endif %}",
        "{% for x in xs %}{% else %}{% else %}{% endfor %}",
        "{% block body %}{% endblock other %}", "{% call x %}{% endcall %}",
        "{% if %}{% endif %}", "{% macro m() %}{% endmacro extra %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("template: %s", sources[i]);
      tree.count = 1u;
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)1u);
    }
  }

  it("keeps comment raw and quoted delimiters out of the block stack") {
    const char *source = "{% block body %}{# {% endif %} #}{% raw %}{{ broken {% endif %}"
        "{% endraw %}{{ {'x': '}}'} }}{% endblock body %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)5u);
    check_equal(tree.nodes[1].kind, JINJA_TEMPLATE_COMMENT);
    check_equal(tree.nodes[2].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[3].kind, JINJA_TEMPLATE_OUTPUT);
    check_equal(tree.nodes[0].match, (size_t)4u);
  }

  it("permits only whitespace and comments in required block bodies") {
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr("{% block body scoped required %}\n{# ok #}\t{% endblock %}"),
        &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    static const char *sources[] = {"{% block b required %}x{% endblock %}",
        "{% block b required %}{{ ' ' }}{% endblock %}",
        "{% block b required %}{% if false %}{% endif %}{% endblock %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
  }

  it("validates reference assignment capture and scoped block headers") {
    const char *source = "{% extends 'base' %}{% include name ignore missing %}"
        "{% import 'm' as m %}{% from 'm' import f as g %}{% set x=1 %}"
        "{% set y | trim %}text{% endset %}{% with a=1,b=2 %}"
        "{% autoescape true %}{% filter upper %}{% print a,b %}{% endfilter %}"
        "{% endautoescape %}{% endwith %}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[5].kind, JINJA_TEMPLATE_CAPTURE);
    check_equal(tree.nodes[5].match, (size_t)7u);
  }

  it("rejects statement specific expression suffixes") {
    static const char *sources[] = {"{% print 1, %}",
        "{% autoescape true,false %}{% endautoescape %}",
        "{% if x if y else z %}{% endif %}", "{% with a=1, %}{% endwith %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("invalid statement: %s", sources[i]);
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    }
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr("{% if (x if y else z): %}{% else: %}{% endif %}"),
        &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr("{% if1 %}{% endif %}"), &tree, NULL), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr("{% raw+%}x{% endraw %}"), &tree, NULL), JINJA_EXPRESSION_PARSE_UNSUPPORTED);
  }

  it("rejects truncated tags mismatched brackets and invalid UTF8 with offsets") {
    static const char *sources[] = {"{{ x", "{# comment", "{% raw %}text", "{{ ([)] }}", "{{ 'x }}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    const char invalid[] = "x\xff";
    size_t offset = SIZE_MAX;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf(invalid, sizeof(invalid) - 1u), &tree, &offset), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)1u);
    const char exact[] = {'{','{','1','}','}'};
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf(exact, sizeof(exact)), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[0].header.offset, (size_t)2u);
    check_equal(tree.nodes[0].header.length, (size_t)1u);
  }

  it("grows owned nodes and bounds input and nesting without changing the previous tree") {
    enum { COMMENT_COUNT = 300, BUFFER_BYTES = COMMENT_COUNT * 32u };
    char source[BUFFER_BYTES];
    size_t length = 0u;
    for (size_t i = 0u; i < COMMENT_COUNT; ++i) {
      memcpy(source + length, "{#x#}", sizeof("{#x#}") - 1u);
      length += sizeof("{#x#}") - 1u;
    }
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf(source, length), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)COMMENT_COUNT);
    JINJA_TEMPLATE_NODE *retained = tree.nodes;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u,
        vstr_from_buf(source, (size_t)JINJA_TEMPLATE_MAX_BYTES + 1u), &tree, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(tree.count, (size_t)COMMENT_COUNT);
    check_equal((const void *)tree.nodes, (const void *)retained);
    length = 0u;
    for (size_t i = 0u; i < JINJA_TEMPLATE_MAX_DEPTH; ++i) {
      memcpy(source + length, "{% if x %}", sizeof("{% if x %}") - 1u);
      length += sizeof("{% if x %}") - 1u;
    }
    for (size_t i = 0u; i < JINJA_TEMPLATE_MAX_DEPTH; ++i) {
      memcpy(source + length, "{% endif %}", sizeof("{% endif %}") - 1u);
      length += sizeof("{% endif %}") - 1u;
    }
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf(source, length), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    memmove(source + sizeof("{% if x %}") - 1u, source, length);
    memcpy(source, "{% if x %}", sizeof("{% if x %}") - 1u);
    length += sizeof("{% if x %}") - 1u;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf(source, length), &tree, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(tree.count, (size_t)JINJA_TEMPLATE_MAX_DEPTH * 2u);
  }

  it("bounds with bindings independently of template node count") {
    char source[JINJA_EXPRESSION_MAX_NODES * 8u];
    size_t length = sizeof("{% with ") - 1u;
    memcpy(source, "{% with ", length);
    for (size_t i = 0u; i < JINJA_EXPRESSION_MAX_NODES; ++i) {
      memcpy(source + length, "a=1,", sizeof("a=1,") - 1u);
      length += sizeof("a=1,") - 1u;
    }
    const char *suffix = "%}{% endwith %}";
    memcpy(source + length - 1u, suffix, strlen(suffix));
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf(source, length - 1u + strlen(suffix)), &tree, NULL),
        JINJA_EXPRESSION_PARSE_OK);
    memcpy(source + length - 1u, ",a=1", sizeof(",a=1") - 1u);
    length += sizeof(",a=1") - 2u;
    memcpy(source + length, suffix, strlen(suffix));
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf(source, length + strlen(suffix)), &tree, NULL),
        JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(tree.count, (size_t)2u);
  }

  it("preserves all previous nodes after a late parse failure") {
    enum { CONDITIONAL_NODES = 3 };
    JINJA_TEMPLATE_NODE previous_nodes[CONDITIONAL_NODES];
    static JINJA_TEMPLATE_TREE previous;
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr("{% if x %}ok{% endif %}"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    previous = tree;
    check_equal(tree.count, (size_t)CONDITIONAL_NODES);
    memcpy(previous_nodes, tree.nodes, sizeof(previous_nodes));
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr("{% macro m() %}a{% if x %}b{% endfor %}"), &tree, NULL),
        JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
    check_equal(tree.nodes, previous_nodes, sizeof(previous_nodes));
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(""), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)0u);
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_buf("x", (size_t)JINJA_TEMPLATE_MAX_BYTES + 1u), &tree, NULL),
        JINJA_EXPRESSION_PARSE_CAPACITY);
  }

  it("retains whitespace controls and ignores overlapping raw opening braces") {
    const char *source = "{%- raw -%}{{{% endraw +%}{{- x -}}{#+x+#}";
    check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count, (size_t)3u);
    check_equal(tree.nodes[0].kind, JINJA_TEMPLATE_RAW);
    check_equal(tree.nodes[0].left_control, -1);
    check_equal(tree.nodes[0].right_control, 1);
    check_equal(tree.nodes[0].header.length, (size_t)2u);
    check_equal(tree.nodes[1].left_control, -1);
    check_equal(tree.nodes[1].right_control, -1);
    check_equal(tree.nodes[2].left_control, 1);
    check_equal(tree.nodes[2].right_control, 1);
  }

  it("allows conditional expressions inside containers in if tests") {
    static const char *sources[] = {"{% if [x if y else z] %}{% endif %}",
        "{% if obj.if %}{% endif %}", "{% if value is equalto if %}{% endif %}",
        "{% if a,if %}{% endif %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("separates statement keywords from Unicode whitespace") {
    static const char *sources[] = {"{% if\xc2\xa0x %}{% endif %}",
        "{% if\xe3\x80\x80x %}{% endif %}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }

  it("parses Unicode names and whitespace throughout template headers") {
    static const char *sources[] = {"{{ 用户.名字 }}", "{{ e\xcc\x81 }}",
        "{% macro 显示(名字) %}{{名字}}{% endmacro %}",
        "{% for 键,值 in 数据 %}{{值}}{% endfor %}",
        "{% set 名称=1 %}{% block 内容 %}{{名称}}{% endblock 内容 %}",
        "{{ not\xc2\xa0 false }}", "{{ 1\xe3\x80\x80== 1 }}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("Unicode template: %s", sources[i]);
      check_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    }
  }

  it("rejects nonidentifier Unicode scalars and join controls") {
    static const char *sources[] = {"{{ \xcc\x81x }}", "{{ a\xc2\xb2 }}", "{{ a\xe2\x80\x8c" "b }}",
        "{{ a\xe2\x80\x8d" "b }}", "{{ \xf0\x9f\x98\x80 }}"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_not_equal(jinja_template_parse(&JINJA_TEMPLATE_DEFAULT_DELIMITERS, 0u, vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }
}
