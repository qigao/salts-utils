#include "parser/jinja_template_parser.h"
#include "jinja_cmeta_memory.h"
#include "tinytest.h"

static const char jinja_memory_template[] =
    "{% set outer = input %}"
    "{% macro show(x=outer) %}{{ x }}{{ kwargs }}{% endmacro %}"
    "{% for item in items if item %}{{ item }}{% else %}none{% endfor %}";

static size_t jinja_memory_find_node(const JINJA_TEMPLATE_TREE *tree, JINJA_TEMPLATE_NODE_KIND kind) {
  for (size_t i = 0u; i < tree->count; ++i)
    if (tree->nodes[i].kind == kind) return i;
  return SIZE_MAX;
}

spec("Jinja template allocation ownership") {
  static JINJA_CMETA_MEMORY memory, other_memory;
  static stl_allocator allocator;
  static JINJA_TEMPLATE_TREE tree;
  static JINJA_EXPRESSION_SCOPE scope;
  static JINJA_TEMPLATE_MACRO_DESCRIPTOR macro;
  static JINJA_TEMPLATE_MACRO_BINDINGS bindings;
  static JINJA_TEMPLATE_DELIMITERS delimiters;

  before_each() {
    delimiters = JINJA_TEMPLATE_DEFAULT_DELIMITERS;
    memory = (JINJA_CMETA_MEMORY){.limit = SIZE_MAX};
    other_memory = (JINJA_CMETA_MEMORY){.limit = SIZE_MAX};
    allocator = jinja_cmeta_memory_allocator(&memory);
    tree = (JINJA_TEMPLATE_TREE){0};
  }
  after_each() {
    jinja_template_tree_destroy(&tree);
    check_equal(memory.used, (size_t)0u);
    check_equal(memory.allocations, (size_t)0u);
    check_equal(other_memory.used, (size_t)0u);
    check_equal(other_memory.allocations, (size_t)0u);
  }

  it("rejects zero quota before publishing a tree") {
    memory.limit = 0u;
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, vstr_from_cstr(jinja_memory_template),
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_null(tree.storage);
    check_null(tree.nodes);
    check_equal(tree.count, (size_t)0u);
  }

  it("copies callbacks while borrowing their context through destruction") {
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, vstr_from_cstr(jinja_memory_template),
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    check_equal(memory.used > 0u, true);
    check_equal(memory.peak > memory.used, true);
    allocator = (stl_allocator){0};
    jinja_template_tree_destroy(&tree);
    check_equal(memory.used, (size_t)0u);
  }

  it("preserves an old tree on quota and syntax failures") {
    const vstr original = vstr_from_cstr("original");
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, original,
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_TEMPLATE_NODE *saved = tree.nodes;
    const size_t count = tree.count, retained = memory.used;
    memory.limit = retained;
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, vstr_from_cstr(jinja_memory_template),
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(tree.nodes == saved, true);
    check_equal(tree.count, count);
    check_equal(memory.used, retained);
    memory.limit = SIZE_MAX;
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, vstr_from_cstr("{% if %}"),
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(tree.nodes == saved, true);
    check_equal(memory.used, retained);
  }

  it("returns the old tree to its original allocator on successful replacement") {
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, vstr_from_cstr("old"),
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    stl_allocator other = jinja_cmeta_memory_allocator(&other_memory);
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, vstr_from_cstr(jinja_memory_template),
        &tree, NULL, &other), JINJA_EXPRESSION_PARSE_OK);
    check_equal(memory.used, (size_t)0u);
    check_equal(memory.allocations, (size_t)0u);
    check_equal(other_memory.used > 0u, true);
  }

  it("admits the exact simultaneous AST and parser workspace peak") {
    const vstr source = vstr_from_cstr(jinja_memory_template);
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, source,
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    const size_t required = memory.peak;
    jinja_template_tree_destroy(&tree);
    memory.limit = required - 1u;
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, source,
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_null(tree.storage);
    check_equal(memory.used, (size_t)0u);
    memory.limit = required;
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, source,
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
  }

  it("shares analysis budgets without consuming the retained input tree") {
    const vstr source = vstr_from_cstr(jinja_memory_template);
    check_equal(jinja_template_parse_allocated(&delimiters, 0u, source,
        &tree, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    const size_t retained = memory.used;
    const size_t macro_node = jinja_memory_find_node(&tree, JINJA_TEMPLATE_MACRO);
    const size_t loop_node = jinja_memory_find_node(&tree, JINJA_TEMPLATE_FOR);
    check_not_equal(macro_node, SIZE_MAX);
    check_not_equal(loop_node, SIZE_MAX);
    memory.limit = retained;
    check_equal(jinja_template_analyze_frame_allocated(source, &tree, SIZE_MAX, NULL,
        &scope, NULL, &allocator), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(jinja_template_describe_macro_allocated(source, &tree, macro_node,
        &macro, NULL, &allocator), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memory.used, retained);
    memory.limit = SIZE_MAX;
    check_equal(jinja_template_analyze_frame_allocated(source, &tree, SIZE_MAX, NULL,
        &scope, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_describe_macro_allocated(source, &tree, macro_node,
        &macro, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_template_analyze_macro_bindings_allocated(source, &tree, macro_node,
        &bindings, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    JINJA_EXPRESSION_SPAN reference = {0};
    check_equal(jinja_template_find_undeclared_allocated(source, &tree, macro_node,
        vstr_from_cstr("kwargs"), &reference, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.length, sizeof("kwargs") - 1u);
    for (JINJA_TEMPLATE_FOR_BRANCH branch = JINJA_TEMPLATE_FOR_BODY;
         branch <= JINJA_TEMPLATE_FOR_ELSE; ++branch) {
      check_equal(jinja_template_analyze_for_frame_allocated(source, &tree, loop_node,
          branch, NULL, &scope, NULL, &allocator), JINJA_EXPRESSION_PARSE_OK);
      check_equal(memory.used, retained);
    }
    check_equal(memory.used, retained);
  }
}
