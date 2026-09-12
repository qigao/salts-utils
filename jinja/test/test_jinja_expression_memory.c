#include "parser/jinja_expression_parser.h"
#include "jinja_cmeta_memory.h"
#include "tinytest.h"

typedef struct JINJA_TEST_PARSE_MEMORY {
  JINJA_CMETA_MEMORY ledger;
  size_t calls;
  size_t fail_call;
} JINJA_TEST_PARSE_MEMORY;

static stl_status jinja_test_parse_allocate(void *opaque, size_t bytes, void **out) {
  JINJA_TEST_PARSE_MEMORY *memory = (JINJA_TEST_PARSE_MEMORY *)opaque;
  *out = NULL;
  if (++memory->calls == memory->fail_call) return STL_OUT_OF_MEMORY;
  return jinja_cmeta_memory_allocate(&memory->ledger, bytes, out);
}

static void jinja_test_parse_deallocate(void *opaque, void *data, size_t bytes) {
  JINJA_TEST_PARSE_MEMORY *memory = (JINJA_TEST_PARSE_MEMORY *)opaque;
  jinja_cmeta_memory_deallocate(&memory->ledger, data, bytes);
}

spec("Jinja expression allocation admission") {
  static JINJA_TEST_PARSE_MEMORY memory;
  static stl_allocator allocator;
  static JINJA_EXPRESSION_TREE tree;
  static size_t error_offset;

  before_each() {
    memory = (JINJA_TEST_PARSE_MEMORY){.ledger = {.limit = SIZE_MAX}};
    allocator = (stl_allocator){&memory, jinja_test_parse_allocate, jinja_test_parse_deallocate};
    tree = (JINJA_EXPRESSION_TREE){.root = SIZE_MAX};
    error_offset = SIZE_MAX;
  }
  after_each() {
    check_equal(memory.ledger.used, (size_t)0u);
    check_equal(memory.ledger.allocations, (size_t)0u);
  }

  it("rejects the source copy under a zero quota") {
    memory.ledger.limit = 0u;
    check_equal(jinja_expression_parse_tree_allocated(vstr_from_cstr("value"),
        &tree, &error_offset, &allocator), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(tree.root, SIZE_MAX);
    check_equal(error_offset, SIZE_MAX);
    check_equal(memory.calls, (size_t)1u);
  }

  it("releases the copied source when the parser workspace cannot fit") {
    const vstr source = vstr_from_cstr("value");
    memory.ledger.limit = source.len + 1u;
    check_equal(jinja_expression_parse_tree_allocated(source, &tree, &error_offset, &allocator),
        JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(tree.root, SIZE_MAX);
    check_equal(error_offset, SIZE_MAX);
    check_equal(memory.calls, (size_t)2u);
    check_equal(memory.ledger.peak, source.len + 1u);
  }

  it("admits exactly the simultaneous source and fixed parser workspace") {
    const vstr source = vstr_from_cstr("value + 1");
    check_equal(jinja_expression_parse_tree_allocated(source, &tree, &error_offset, &allocator),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(memory.calls, (size_t)2u);
    const size_t required = memory.ledger.peak;
    memory.ledger.limit = required - 1u;
    tree.root = SIZE_MAX;
    check_equal(jinja_expression_parse_tree_allocated(source, &tree, &error_offset, &allocator),
        JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(tree.root, SIZE_MAX);
    memory.ledger.limit = required;
    check_equal(jinja_expression_parse_tree_allocated(source, &tree, &error_offset, &allocator),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.count > 0u, true);
  }

  it("preserves allocation failure identity and does not retry") {
    memory.fail_call = 1u;
    check_equal(jinja_expression_parse_tree_allocated(vstr_from_cstr("value"),
        &tree, &error_offset, &allocator), JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY);
    check_equal(memory.calls, (size_t)1u);
    check_equal(tree.root, SIZE_MAX);
    memory.calls = 0u;
    memory.fail_call = 2u;
    check_equal(jinja_expression_parse_tree_allocated(vstr_from_cstr("value"),
        &tree, &error_offset, &allocator), JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY);
    check_equal(memory.calls, (size_t)2u);
    check_equal(tree.root, SIZE_MAX);
  }

  it("cleans up syntax failures without publishing a partial tree") {
    check_equal(jinja_expression_parse_tree_allocated(vstr_from_cstr("("),
        &tree, &error_offset, &allocator), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(tree.root, SIZE_MAX);
    check_equal(error_offset != SIZE_MAX, true);
    check_equal(memory.calls, (size_t)2u);
  }

  it("rejects incomplete allocator callbacks before allocation") {
    allocator.deallocate = NULL;
    check_equal(jinja_expression_parse_tree_allocated(vstr_from_cstr("value"),
        &tree, &error_offset, &allocator), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memory.calls, (size_t)0u);
    check_equal(tree.root, SIZE_MAX);
  }
}
