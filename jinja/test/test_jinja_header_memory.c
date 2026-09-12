#include "parser/jinja_expression_parser.h"
#include "jinja_cmeta_memory.h"
#include "tinytest.h"

typedef enum JINJA_TEST_HEADER_KIND {
  HEADER_ASSIGNMENT, HEADER_WITH, HEADER_MACRO, HEADER_CALL, HEADER_FOR,
  HEADER_EXTENDS, HEADER_INCLUDE, HEADER_IMPORT, HEADER_FROM,
  HEADER_BLOCK, HEADER_ENDBLOCK, HEADER_FILTER, HEADER_CONDITION,
  HEADER_LEGACY_CONDITION, HEADER_LEGACY_PATH
} JINJA_TEST_HEADER_KIND;

typedef struct JINJA_TEST_HEADER_CASE {
  JINJA_TEST_HEADER_KIND kind;
  const char *source;
} JINJA_TEST_HEADER_CASE;

static const JINJA_TEST_HEADER_CASE jinja_header_cases[] = {
    {HEADER_ASSIGNMENT, "item = value"},
    {HEADER_WITH, "item = value, next = other"},
    {HEADER_MACRO, "macro(value=outer)"},
    {HEADER_CALL, "wrapper(value)"},
    {HEADER_FOR, "item in items if item"},
    {HEADER_EXTENDS, "'base'"},
    {HEADER_INCLUDE, "'leaf' with context"},
    {HEADER_IMPORT, "'leaf' as module"},
    {HEADER_FROM, "'leaf' import value as item"},
    {HEADER_BLOCK, "body scoped required"},
    {HEADER_ENDBLOCK, "body"},
    {HEADER_FILTER, "default(value)"},
    {HEADER_CONDITION, "value"},
    {HEADER_LEGACY_CONDITION, "enabled"},
    {HEADER_LEGACY_PATH, "object"}};

/* Tests run serially. Static result storage keeps large AST fixtures off the
 * test thread's stack; none of these results owns allocated storage. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_test_header_parse(
    const JINJA_TEST_HEADER_CASE *test, const stl_allocator *allocator) {
  static JINJA_EXPRESSION_TREE tree, targets;
  static JINJA_EXPRESSION_MACRO_SIGNATURE signature;
  static JINJA_TEMPLATE_FOR_HEADER loop;
  static JINJA_TEMPLATE_REFERENCE reference;
  static JINJA_TEMPLATE_BLOCK_HEADER block;
  static JINJA_EXPRESSION_CONDITION condition;
  const vstr source = vstr_from_cstr(test->source);
  vstr name, rhs;
  JINJA_EXPRESSION_SPAN call;
  size_t offset = 0u, consumed;
  int capture;
  char truth;
  switch (test->kind) {
    case HEADER_ASSIGNMENT:
      return jinja_expression_parse_assignment_allocated(source, &name, &rhs, &tree,
          &targets, &capture, &offset, allocator);
    case HEADER_WITH:
      return jinja_expression_parse_with_binding_allocated(source, &consumed, &rhs,
          &tree, &targets, allocator);
    case HEADER_MACRO:
      return jinja_expression_parse_macro_signature_allocated(source, &signature, &offset, allocator);
    case HEADER_CALL:
      return jinja_expression_parse_call_header_allocated(source, &signature, &call, &tree, &offset, allocator);
    case HEADER_FOR:
      return jinja_expression_parse_for_header_allocated(source, &loop, &offset, allocator);
    case HEADER_EXTENDS: case HEADER_INCLUDE: case HEADER_IMPORT: case HEADER_FROM: {
      const JINJA_TEMPLATE_REFERENCE_KIND kind = test->kind == HEADER_EXTENDS ? JINJA_TEMPLATE_REFERENCE_EXTENDS
          : test->kind == HEADER_INCLUDE ? JINJA_TEMPLATE_REFERENCE_INCLUDE
          : test->kind == HEADER_IMPORT ? JINJA_TEMPLATE_REFERENCE_IMPORT : JINJA_TEMPLATE_REFERENCE_FROM;
      return jinja_expression_parse_template_reference_allocated(source, kind, &reference, &offset, allocator);
    }
    case HEADER_BLOCK:
      return jinja_expression_parse_block_header_allocated(source, &block, &offset, allocator);
    case HEADER_ENDBLOCK:
      return jinja_expression_parse_endblock_allocated(source, vstr_from_cstr("body"), &offset, allocator);
    case HEADER_FILTER:
      return jinja_expression_parse_filter_block_allocated(source, &tree, &offset, allocator);
    case HEADER_CONDITION:
      return jinja_expression_parse_condition_allocated(source, &condition, &offset, allocator);
    case HEADER_LEGACY_CONDITION:
      return jinja_expression_parse_legacy_condition_allocated(source, &name, &truth, &offset, allocator);
    case HEADER_LEGACY_PATH:
      return jinja_expression_parse_legacy_path_allocated(source, &name, &offset, allocator);
    default: return JINJA_EXPRESSION_PARSE_INVALID;
  }
}

typedef struct JINJA_TEST_HEADER_MEMORY {
  JINJA_CMETA_MEMORY ledger;
  size_t calls;
  size_t fail_call;
} JINJA_TEST_HEADER_MEMORY;

static stl_status jinja_test_header_allocate(void *opaque, size_t bytes, void **out) {
  JINJA_TEST_HEADER_MEMORY *memory = (JINJA_TEST_HEADER_MEMORY *)opaque;
  *out = NULL;
  if (++memory->calls == memory->fail_call) return STL_OUT_OF_MEMORY;
  return jinja_cmeta_memory_allocate(&memory->ledger, bytes, out);
}

static void jinja_test_header_deallocate(void *opaque, void *data, size_t bytes) {
  JINJA_TEST_HEADER_MEMORY *memory = (JINJA_TEST_HEADER_MEMORY *)opaque;
  jinja_cmeta_memory_deallocate(&memory->ledger, data, bytes);
}

spec("Jinja header allocation propagation") {
  static JINJA_TEST_HEADER_MEMORY memory;
  static stl_allocator allocator;
  before_each() {
    memory = (JINJA_TEST_HEADER_MEMORY){.ledger = {.limit = SIZE_MAX}};
    allocator = (stl_allocator){&memory, jinja_test_header_allocate, jinja_test_header_deallocate};
  }
  after_each() {
    check_equal(memory.ledger.used, (size_t)0u);
    check_equal(memory.ledger.allocations, (size_t)0u);
  }

  it("routes every header family through the supplied allocator") {
    for (size_t i = 0u; i < sizeof(jinja_header_cases) / sizeof(jinja_header_cases[0]); ++i) {
      memory.calls = 0u;
      check_equal(jinja_test_header_parse(&jinja_header_cases[i], &allocator), JINJA_EXPRESSION_PARSE_OK);
      check_equal(memory.calls > 0u, true);
      check_equal(memory.ledger.used, (size_t)0u);
      check_equal(memory.ledger.allocations, (size_t)0u);
    }
  }

  it("rejects every family under a zero byte quota") {
    memory.ledger.limit = 0u;
    for (size_t i = 0u; i < sizeof(jinja_header_cases) / sizeof(jinja_header_cases[0]); ++i) {
      memory.calls = 0u;
      check_equal(jinja_test_header_parse(&jinja_header_cases[i], &allocator), JINJA_EXPRESSION_PARSE_CAPACITY);
      check_equal(memory.calls, (size_t)1u);
      check_equal(memory.ledger.used, (size_t)0u);
    }
  }

  it("propagates each nested allocation failure and releases earlier work") {
    for (size_t i = 0u; i < sizeof(jinja_header_cases) / sizeof(jinja_header_cases[0]); ++i) {
      memory.calls = 0u;
      memory.fail_call = 0u;
      check_equal(jinja_test_header_parse(&jinja_header_cases[i], &allocator), JINJA_EXPRESSION_PARSE_OK);
      const size_t allocations = memory.calls;
      for (size_t failure = 1u; failure <= allocations; ++failure) {
        memory.calls = 0u;
        memory.fail_call = failure;
        check_equal(jinja_test_header_parse(&jinja_header_cases[i], &allocator),
            JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY);
        check_equal(memory.calls, failure);
        check_equal(memory.ledger.used, (size_t)0u);
        check_equal(memory.ledger.allocations, (size_t)0u);
      }
    }
  }

  it("enforces simultaneous header and nested expression peak storage") {
    for (size_t i = 0u; i < sizeof(jinja_header_cases) / sizeof(jinja_header_cases[0]); ++i) {
      memory.ledger.limit = SIZE_MAX;
      memory.ledger.peak = 0u;
      check_equal(jinja_test_header_parse(&jinja_header_cases[i], &allocator), JINJA_EXPRESSION_PARSE_OK);
      const size_t required = memory.ledger.peak;
      memory.ledger.limit = required - 1u;
      check_equal(jinja_test_header_parse(&jinja_header_cases[i], &allocator), JINJA_EXPRESSION_PARSE_CAPACITY);
      check_equal(memory.ledger.used, (size_t)0u);
      memory.ledger.limit = required;
      check_equal(jinja_test_header_parse(&jinja_header_cases[i], &allocator), JINJA_EXPRESSION_PARSE_OK);
      check_equal(memory.ledger.used, (size_t)0u);
    }
  }
}
