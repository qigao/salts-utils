#include "test_jinja_native_functions_support.h"
spec("Jinja stable lexical cells") {
  enum { ACTIVATION_BUDGET = 8, CELL_BUDGET = 32 };
  static JINJA_CMETA_TEMPLATE *templ;
  static JINJA_CMETA_CELL_STORE store, other;
  static JINJA_CMETA_ERROR error;
  before_each() { templ = NULL; store = (JINJA_CMETA_CELL_STORE){0}; other = (JINJA_CMETA_CELL_STORE){0}; }
  after_each() {
    jinja_cmeta_cells_destroy(&other);
    jinja_cmeta_cells_destroy(&store);
    jinja_cmeta_release(templ);
  }

  it("retains defining activation writes without reading a caller activation") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set x=1 %}{% macro f() %}{{x}}{% endmacro %}{% macro caller(x) %}{{x}}{% endmacro %}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_cells_init(&store, templ, ACTIVATION_BUDGET, CELL_BUDGET), JINJA_CMETA_OK);
    JINJA_CMETA_ACTIVATION *root, *macro, *caller;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &root), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_new(&store, root, templ->functions[0].scope, &macro), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_new(&store, root, templ->functions[1].scope, &caller), JINJA_CMETA_OK);
    JINJA_CMETA_CELL_VALUE *defining, *captured, *local;
    size_t root_x = test_native_cell(templ, 0u, "x");
    size_t caller_x = test_native_cell(templ, templ->functions[1].scope, "x");
    check_equal(jinja_cmeta_activation_cell(root, root_x, &defining), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_cell(caller, caller_x, &local), JINJA_CMETA_OK);
    *local = (JINJA_CMETA_CELL_VALUE){.bound = 1, .value = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = 99}};
    *defining = (JINJA_CMETA_CELL_VALUE){.bound = 1, .value = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = 1}};
    check_equal(jinja_cmeta_activation_cell(macro, root_x, &captured), JINJA_CMETA_OK);
    check_true(captured == defining);
    defining->value.integer = 2;
    check_equal(captured->value.integer, (int64_t)2);
    check_equal(jinja_cmeta_activation_cell(macro, caller_x, &captured), JINJA_CMETA_ERR_METADATA);
    check_true(captured == defining);
  }

  it("separates repeated activations and distinguishes missing from explicit undefined") {
    templ = jinja_cmeta_compile(vstr_from_cstr("{% macro f(p=none) %}{{p}}{% endmacro %}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_cells_init(&store, templ, ACTIVATION_BUDGET, CELL_BUDGET), JINJA_CMETA_OK);
    JINJA_CMETA_ACTIVATION *root, *first, *second;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &root), JINJA_CMETA_OK);
    size_t owner = templ->functions[0].scope;
    check_equal(jinja_cmeta_activation_new(&store, root, owner, &first), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_new(&store, root, owner, &second), JINJA_CMETA_OK);
    JINJA_CMETA_CELL_VALUE *a, *b;
    size_t parameter = test_native_cell(templ, owner, "p");
    check_equal(jinja_cmeta_activation_cell(first, parameter, &a), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_cell(second, parameter, &b), JINJA_CMETA_OK);
    check_false(a == b);
    check_equal(a->bound, 0);
    check_equal(b->bound, 0);
    a->value.kind = JINJA_CMETA_VALUE_UNDEFINED;
    a->bound = 1;
    check_equal(a->bound, 1);
    check_equal(b->bound, 0);
    check_equal(jinja_cmeta_activation_clear(first, owner), JINJA_CMETA_OK);
    check_equal(a->bound, 0);
  }

  it("clears scope values without invalidating cells retained by a closure") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% for x in [1] %}{% macro f() %}{{x}}{% endmacro %}{% endfor %}"
        "{% for x in [9] %}{{x}}{% endfor %}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_cells_init(&store, templ, ACTIVATION_BUDGET, CELL_BUDGET), JINJA_CMETA_OK);
    JINJA_CMETA_ACTIVATION *root, *closure;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &root), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_new(&store, root, templ->functions[0].scope, &closure), JINJA_CMETA_OK);
    JINJA_CMETA_CELL_VALUE *cell, *again;
    const size_t x = test_native_cell(templ, 0u, "x");
    const size_t body = templ->lexical_scopes[templ->functions[0].scope].parent;
    check_equal(jinja_cmeta_activation_cell(closure, x, &cell), JINJA_CMETA_OK);
    *cell = (JINJA_CMETA_CELL_VALUE){.bound = 1, .value = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = 1}};
    size_t admitted = store.cell_count;
    check_equal(jinja_cmeta_activation_clear(root, body), JINJA_CMETA_OK);
    check_equal(cell->bound, 0);
    check_equal(jinja_cmeta_activation_cell(closure, x, &again), JINJA_CMETA_OK);
    check_true(again == cell);
    *again = (JINJA_CMETA_CELL_VALUE){.bound = 1, .value = {.kind = JINJA_CMETA_VALUE_INTEGER, .integer = 9}};
    check_equal(cell->value.integer, (int64_t)9);
    check_equal(store.cell_count, admitted);
  }

  it("clears shadow cells without clearing ancestor alias sources") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% set x=1 %}{% with %}{% set x=2 %}{{x}}{% endwith %}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_cells_init(&store, templ, ACTIVATION_BUDGET, CELL_BUDGET), JINJA_CMETA_OK);
    JINJA_CMETA_ACTIVATION *root;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &root), JINJA_CMETA_OK);
    const JINJA_CMETA_CELL_BINDING *shadow = &templ->cell_bindings[templ->lexical_scopes[1].first_binding];
    check_equal(shadow->load, JINJA_CMETA_CELL_ALIAS);
    JINJA_CMETA_CELL_VALUE *source, *local;
    check_equal(jinja_cmeta_activation_cell(root, shadow->source_cell, &source), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_cell(root, shadow->cell, &local), JINJA_CMETA_OK);
    check_false(source == local);
    source->bound = local->bound = 1;
    source->value.kind = JINJA_CMETA_VALUE_INTEGER;
    source->value.integer = 7;
    check_equal(jinja_cmeta_activation_clear(root, 1u), JINJA_CMETA_OK);
    check_equal(local->bound, 0);
    check_equal(source->bound, 1);
    check_equal(source->value.integer, (int64_t)7);
  }

  it("admits exactly the cell quota and preserves cells on invalid scope operations") {
    templ = jinja_cmeta_compile(vstr_from_cstr("{% macro f(p) %}{{p}}{% endmacro %}"), NULL, &error);
    check_not_null(templ);
    size_t owner = templ->functions[0].scope;
    size_t quota = templ->lexical_scopes[0].cell_count + templ->lexical_scopes[owner].cell_count;
    check_equal(jinja_cmeta_cells_init(&store, templ, ACTIVATION_BUDGET, quota), JINJA_CMETA_OK);
    JINJA_CMETA_ACTIVATION *root, *macro, *result;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &root), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_new(&store, root, owner, &macro), JINJA_CMETA_OK);
    check_equal(store.cell_count, quota);
    result = root;
    check_equal(jinja_cmeta_activation_new(&store, root, owner, &result), JINJA_CMETA_ERR_CAPACITY);
    check_true(result == root);
    check_equal(store.activation_count, (size_t)2u);
    JINJA_CMETA_CELL_VALUE *cell, *unchanged;
    check_equal(jinja_cmeta_activation_cell(macro, test_native_cell(templ, owner, "p"), &cell), JINJA_CMETA_OK);
    cell->bound = 1;
    unchanged = cell;
    check_equal(jinja_cmeta_activation_clear(macro, 0u), JINJA_CMETA_ERR_METADATA);
    check_equal(jinja_cmeta_activation_clear(macro, SIZE_MAX), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_equal(cell->bound, 1);
    check_equal(jinja_cmeta_activation_cell(macro, SIZE_MAX, &unchanged), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_true(unchanged == cell);
    check_equal(jinja_cmeta_cells_init(&store, templ, ACTIVATION_BUDGET, quota), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_equal(store.cell_count, quota);
    check_equal(jinja_cmeta_activation_new(&store, macro, owner, &result), JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_true(result == root);
  }

  it("accounts for empty activations and permits reuse only after store destruction") {
    templ = jinja_cmeta_compile(vstr_from_cstr(""), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_cells_init(&store, templ, 1u, 0u), JINJA_CMETA_OK);
    JINJA_CMETA_ACTIVATION *root, *result;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &root), JINJA_CMETA_OK);
    check_equal(store.cell_count, (size_t)0u);
    result = root;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &result), JINJA_CMETA_ERR_CAPACITY);
    check_true(result == root);
    jinja_cmeta_cells_destroy(&store);
    check_null(store.templ);
    check_equal(store.activation_count, (size_t)0u);
    check_equal(jinja_cmeta_cells_init(&store, templ, 0u, CELL_BUDGET), JINJA_CMETA_OK);
    result = NULL;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &result), JINJA_CMETA_ERR_CAPACITY);
    check_null(result);
  }

  it("rejects quota exhaustion and foreign parents without publishing partial activations") {
    templ = jinja_cmeta_compile(vstr_from_cstr("{% macro f(p) %}{{p}}{% endmacro %}"), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_cells_init(&store, templ, 1u, CELL_BUDGET), JINJA_CMETA_OK);
    JINJA_CMETA_ACTIVATION *root, *result;
    check_equal(jinja_cmeta_activation_new(&store, NULL, 0u, &root), JINJA_CMETA_OK);
    result = root;
    size_t admitted = store.cell_count;
    check_equal(jinja_cmeta_activation_new(&store, root, templ->functions[0].scope, &result), JINJA_CMETA_ERR_CAPACITY);
    check_true(result == root);
    check_equal(store.activation_count, (size_t)1u);
    check_equal(store.cell_count, admitted);
    check_equal(jinja_cmeta_cells_init(&other, templ, ACTIVATION_BUDGET, 0u), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_activation_new(&other, NULL, 0u, &result), JINJA_CMETA_ERR_CAPACITY);
    check_true(result == root);
    check_equal(other.activation_count, (size_t)0u);
    check_equal(jinja_cmeta_activation_new(&other, root, templ->functions[0].scope, &result),
        JINJA_CMETA_ERR_INVALID_ARGUMENT);
    check_true(result == root);
    jinja_cmeta_cells_destroy(&other);
    check_equal(jinja_cmeta_cells_init(&other, templ, SIZE_MAX, SIZE_MAX), JINJA_CMETA_ERR_CAPACITY);
    check_null(other.templ);
  }
}
