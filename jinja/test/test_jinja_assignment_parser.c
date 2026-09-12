#include "parser/jinja_expression_parser.h"
#include "jinja_expression_grammar_gen.h"
#include "tinytest.h"

#include <stdio.h>
#include <string.h>

spec("Jinja lexical binding facts") {
  static JINJA_EXPRESSION_TREE expression, targets;
  static JINJA_EXPRESSION_BINDING_ACCESSES accesses;

  it("collects expression reads without treating attribute filter and keyword labels as bindings") {
    const char *source = "fn(user.name, label=items[key:stop:step])|default(other) if enabled else fallback";
    const char *expected[] = {"fn", "user", "items", "key", "stop", "step", "other", "enabled", "fallback"};
    check_equal(jinja_expression_parse_tree(vstr_from_cstr(source), &expression, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_reads(vstr_from_cstr(source), &expression, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(accesses.read_count, sizeof(expected) / sizeof(expected[0]));
    check_equal(accesses.write_count, (size_t)0u);
    for (size_t i = 0; i < accesses.read_count; ++i) {
      check_equal(accesses.reads[i].length, strlen(expected[i]));
      check_equal(memcmp(source + accesses.reads[i].offset, expected[i], strlen(expected[i])), 0);
    }
  }

  it("separates tuple binding writes from namespace owner reads") {
    const char *source = "x, ns.value, (y,x) = values";
    vstr name, rhs;
    int capture = 0;
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr(source), &name, &rhs,
        &expression, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_targets(vstr_from_cstr(source), &targets, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(accesses.read_count, (size_t)1u);
    check_equal(accesses.write_count, (size_t)2u);
    check_equal(accesses.reads[0].offset, (size_t)3u);
    check_equal(accesses.reads[0].length, (size_t)2u);
    check_equal(accesses.writes[0].offset, (size_t)0u);
    check_equal(accesses.writes[1].offset, (size_t)14u);
  }

  it("keeps reads in comparisons and short circuit branches in lexical order") {
    const char *source = "a < b and (c == a or d is sameas(e))";
    const char *expected[] = {"a", "b", "c", "d", "e"};
    check_equal(jinja_expression_parse_tree(vstr_from_cstr(source), &expression, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_reads(vstr_from_cstr(source), &expression, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(accesses.read_count, sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < accesses.read_count; ++i) {
      check_equal(accesses.reads[i].length, (size_t)1u);
      check_equal(memcmp(source + accesses.reads[i].offset, expected[i], 1u), 0);
    }
    check_equal(accesses.reads[0].offset, (size_t)0u);
  }

  it("collects capture filter arguments without an artificial capture variable") {
    const char *source = "replace(old, new)|default(fallback)";
    check_equal(jinja_expression_parse_filter_block(vstr_from_cstr(source), &expression, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_reads(vstr_from_cstr(source), &expression, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(accesses.read_count, (size_t)3u);
    check_equal(accesses.reads[0].offset, (size_t)8u);
    check_equal(accesses.reads[1].offset, (size_t)13u);
    check_equal(accesses.reads[2].offset, (size_t)26u);
  }

  it("keeps reads and writes of the same target owner distinct") {
    const char *source = "ns, ns.value, ns.other = values";
    vstr name, rhs;
    int capture = 0;
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr(source), &name, &rhs,
        &expression, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_targets(vstr_from_cstr(source), &targets, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(accesses.read_count, (size_t)1u);
    check_equal(accesses.write_count, (size_t)1u);
    check_equal(accesses.reads[0].offset, (size_t)4u);
    check_equal(accesses.writes[0].offset, (size_t)0u);
    check_equal(jinja_expression_collect_reads(rhs, &expression, &accesses, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(accesses.read_count, (size_t)1u);
    check_equal(memcmp(rhs.data + accesses.reads[0].offset, "values", accesses.reads[0].length), 0);
  }

  it("handles empty targets and exact length source without a terminator") {
    const char source[] = {'u','s','e','r'};
    check_equal(jinja_expression_parse_tree(vstr_from_buf(source, sizeof(source)), &expression, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_reads(vstr_from_buf(source, sizeof(source)), &expression, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(accesses.read_count, (size_t)1u);
    check_equal(accesses.reads[0].length, sizeof(source));
    vstr name, rhs;
    int capture = 0;
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr("()=()"), &name, &rhs,
        &expression, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_targets(vstr_from_cstr("()=()"), &targets, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    check_equal(accesses.read_count, (size_t)0u);
    check_equal(accesses.write_count, (size_t)0u);
  }

  it("rejects invalid counts spans and target kinds without publishing partial facts") {
    static JINJA_EXPRESSION_BINDING_ACCESSES previous;
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x"), &expression, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_reads(vstr_from_cstr("x"), &expression, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    previous = accesses;
    check_equal(jinja_expression_collect_reads(vstr_from_cstr("x"), NULL, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_INVALID);
    expression.count = JINJA_EXPRESSION_MAX_NODES + 1u;
    check_equal(jinja_expression_collect_reads(vstr_from_cstr("x"), &expression, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&accesses, &previous, sizeof(accesses)), 0);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x+y"), &expression, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_collect_targets(vstr_from_cstr("x+y"), &expression, &accesses, NULL),
                JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&accesses, &previous, sizeof(accesses)), 0);
    expression.nodes[1].path.length = SIZE_MAX;
    size_t offset = SIZE_MAX;
    check_equal(jinja_expression_collect_reads(vstr_from_cstr("x+y"), &expression, &accesses, &offset),
                JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)2u);
    check_equal(memcmp(&accesses, &previous, sizeof(accesses)), 0);
  }
}

spec("Jinja lexical scope bindings") {
  static JINJA_EXPRESSION_SCOPE scope, parent;

  it("locates inherited reads but keeps alias initialization separate from local identity") {
    static JINJA_EXPRESSION_SCOPE middle;
    const JINJA_EXPRESSION_SCOPE_EVENT declarations[] = {
        {JINJA_EXPRESSION_SCOPE_PARAMETER, {0u, 1u}},
        {JINJA_EXPRESSION_SCOPE_PARAMETER, {2u, 1u}}};
    const JINJA_EXPRESSION_SCOPE_EVENT writes = {JINJA_EXPRESSION_SCOPE_STORE, {0u, 1u}};
    JINJA_EXPRESSION_SCOPE_REFERENCE reference;
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("x y"), NULL, declarations, 2u,
        &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("x"), &parent, &writes, 1u,
        &middle, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr(""), &middle, NULL, 0u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_resolve_scope(&scope, vstr_from_cstr("x"), &reference),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.depth, (size_t)1u);
    check_equal(reference.symbol, (size_t)0u);
    check_equal(middle.symbols[reference.symbol].load, JINJA_EXPRESSION_SCOPE_ALIAS);
    check_equal(jinja_expression_resolve_scope(&scope, vstr_from_cstr("y"), &reference),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.depth, (size_t)2u);
    check_equal(reference.symbol, (size_t)1u);
    check_equal(jinja_expression_resolve_scope(&middle, vstr_from_cstr("x"), &reference),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.depth, (size_t)0u);
    check_equal(reference.symbol, (size_t)0u);
    check_equal(jinja_expression_resolve_scope(&scope, vstr_from_cstr("absent"), &reference),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.depth, (size_t)0u);
    check_equal(reference.symbol, SIZE_MAX);
  }

  it("resolves exact UTF8 names without retaining the lookup buffer") {
    const char source[] = "prefix \xE5\x90\x8D";
    const char name[] = {'\xE5', '\x90', '\x8D'};
    const JINJA_EXPRESSION_SCOPE_EVENT declaration = {JINJA_EXPRESSION_SCOPE_PARAMETER, {7u, 3u}};
    JINJA_EXPRESSION_SCOPE_REFERENCE reference;
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr(source), NULL, &declaration, 1u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_resolve_scope(&scope, vstr_from_buf(name, sizeof(name)), &reference),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.depth, (size_t)0u);
    check_equal(reference.symbol, (size_t)0u);
  }

  it("rejects malformed lexical lookup inputs without publishing an address") {
    const JINJA_EXPRESSION_SCOPE_EVENT declaration = {JINJA_EXPRESSION_SCOPE_PARAMETER, {0u, 1u}};
    const char *invalid[] = {"", " x", "x.y", "x y", "\xFF"};
    const JINJA_EXPRESSION_SCOPE_REFERENCE previous = {1u, 2u};
    JINJA_EXPRESSION_SCOPE_REFERENCE reference = previous;
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("x"), NULL, &declaration, 1u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
      check_equal(jinja_expression_resolve_scope(&scope, vstr_from_cstr(invalid[i]), &reference),
          JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(memcmp(&reference, &previous, sizeof(reference)), 0);
    }
    scope.symbols[0].name.offset = SIZE_MAX;
    check_equal(jinja_expression_resolve_scope(&scope, vstr_from_cstr("x"), &reference),
        JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&reference, &previous, sizeof(reference)), 0);
    check_equal(jinja_expression_resolve_scope(NULL, vstr_from_cstr("x"), &reference),
        JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_expression_resolve_scope(&scope, vstr_from_cstr("x"), NULL),
        JINJA_EXPRESSION_PARSE_INVALID);
  }

  it("bounds the complete lookup chain even when the nearest frame contains the name") {
    static JINJA_EXPRESSION_SCOPE chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH + 1u];
    const JINJA_EXPRESSION_SCOPE_EVENT declaration = {JINJA_EXPRESSION_SCOPE_PARAMETER, {0u, 1u}};
    JINJA_EXPRESSION_SCOPE_REFERENCE reference;
    for (size_t i = 0u; i < JINJA_EXPRESSION_MAX_SCOPE_DEPTH; ++i)
      check_equal(jinja_expression_analyze_scope(vstr_from_cstr("x"), i == 0u ? NULL : &chain[i - 1u],
          &declaration, 1u, &chain[i], NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_resolve_scope(&chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH - 1u],
        vstr_from_cstr("x"), &reference), JINJA_EXPRESSION_PARSE_OK);
    check_equal(reference.depth, (size_t)0u);
    check_equal(reference.symbol, (size_t)0u);
    chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH] = chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH - 1u];
    chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH].parent = &chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH - 1u];
    check_equal(jinja_expression_resolve_scope(&chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH],
        vstr_from_cstr("x"), &reference), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(reference.depth, (size_t)0u);
    check_equal(reference.symbol, (size_t)0u);
  }

  it("preserves different initialization for read before write and write before read") {
    const char *source = "x y";
    const JINJA_EXPRESSION_SCOPE_EVENT events[] = {
        {JINJA_EXPRESSION_SCOPE_READ, {0u, 1u}},
        {JINJA_EXPRESSION_SCOPE_STORE, {0u, 1u}},
        {JINJA_EXPRESSION_SCOPE_STORE, {2u, 1u}},
        {JINJA_EXPRESSION_SCOPE_READ, {2u, 1u}}};
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr(source), NULL, events,
        sizeof(events) / sizeof(events[0]), &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)2u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_UNDEFINED);
    check_equal(scope.symbols[0].stored, 1);
    check_equal(scope.symbols[1].stored, 1);
  }

  it("shadows a parent binding only when the child writes the same name") {
    const char *source = "x y";
    const JINJA_EXPRESSION_SCOPE_EVENT parameters[] = {
        {JINJA_EXPRESSION_SCOPE_PARAMETER, {0u, 1u}},
        {JINJA_EXPRESSION_SCOPE_PARAMETER, {2u, 1u}}};
    const JINJA_EXPRESSION_SCOPE_EVENT body[] = {
        {JINJA_EXPRESSION_SCOPE_READ, {0u, 1u}},
        {JINJA_EXPRESSION_SCOPE_READ, {2u, 1u}},
        {JINJA_EXPRESSION_SCOPE_STORE, {0u, 1u}}};
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr(source), NULL, parameters, 2u,
        &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr(source), &parent, body, 3u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)1u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ALIAS);
    check_equal(scope.symbols[0].parent_depth, (size_t)1u);
    check_equal(scope.symbols[0].parent_symbol, (size_t)0u);
    check_equal(parent.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
  }

  it("initializes conditional stores from context without replacing existing locals or arguments") {
    const JINJA_EXPRESSION_SCOPE_EVENT events[] = {
        {JINJA_EXPRESSION_SCOPE_PARAMETER, {0u, 1u}},
        {JINJA_EXPRESSION_SCOPE_STORE, {2u, 1u}},
        {JINJA_EXPRESSION_SCOPE_BRANCH_STORE, {4u, 1u}},
        {JINJA_EXPRESSION_SCOPE_BRANCH_STORE, {2u, 1u}},
        {JINJA_EXPRESSION_SCOPE_BRANCH_STORE, {0u, 1u}}};
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("p x y"), NULL, events, 5u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_UNDEFINED);
    check_equal(scope.symbols[2].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
  }

  it("retains the nearest lexical ancestor identity across different source views") {
    static JINJA_EXPRESSION_SCOPE middle;
    const JINJA_EXPRESSION_SCOPE_EVENT declaration = {JINJA_EXPRESSION_SCOPE_PARAMETER, {0u, 5u}};
    const JINJA_EXPRESSION_SCOPE_EVENT conditional = {JINJA_EXPRESSION_SCOPE_BRANCH_STORE, {5u, 5u}};
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("value"), NULL, &declaration, 1u,
        &parent, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr(""), &parent, NULL, 0u,
        &middle, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("junk value"), &middle, &conditional, 1u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ALIAS);
    check_equal(scope.symbols[0].parent_depth, (size_t)2u);
    check_equal(scope.symbols[0].parent_symbol, (size_t)0u);
    check_equal(scope.symbols[0].name.offset, (size_t)5u);
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("value"), &parent, &declaration, 1u,
        &middle, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("junk value"), &middle, &conditional, 1u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.symbols[0].parent_depth, (size_t)1u);
    check_equal(middle.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
  }

  it("binds parsed default reads to all parameters before resolving external names") {
    const char *source = "f(a=b,b=external)";
    JINJA_EXPRESSION_MACRO_SIGNATURE signature;
    JINJA_EXPRESSION_SCOPE_EVENT events[JINJA_EXPRESSION_MAX_NODES + JINJA_EXPRESSION_MAX_REFERENCES];
    check_equal(jinja_expression_parse_macro_signature(vstr_from_cstr(source), &signature, NULL),
                JINJA_EXPRESSION_PARSE_OK);
    size_t count = 0u;
    for (size_t i = 0u; i < signature.parameter_count; ++i)
      events[count++] = (JINJA_EXPRESSION_SCOPE_EVENT){JINJA_EXPRESSION_SCOPE_PARAMETER, signature.parameters[i].name};
    for (size_t i = 0u; i < signature.default_reference_count; ++i)
      events[count++] = (JINJA_EXPRESSION_SCOPE_EVENT){JINJA_EXPRESSION_SCOPE_READ, signature.default_references[i]};
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr(source), NULL, events, count,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)3u);
    check_equal(scope.symbols[0].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[1].load, JINJA_EXPRESSION_SCOPE_ARGUMENT);
    check_equal(scope.symbols[2].load, JINJA_EXPRESSION_SCOPE_RESOLVE);
    check_equal(memcmp(source + scope.symbols[2].name.offset, "external", scope.symbols[2].name.length), 0);
  }

  it("rejects parameter ordering duplicates malformed names and overwriting ancestors atomically") {
    static JINJA_EXPRESSION_SCOPE previous;
    const JINJA_EXPRESSION_SCOPE_EVENT declaration = {JINJA_EXPRESSION_SCOPE_PARAMETER, {0u, 1u}};
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("x"), NULL, &declaration, 1u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    previous = scope;
    const JINJA_EXPRESSION_SCOPE_EVENT late[] = {
        {JINJA_EXPRESSION_SCOPE_READ, {0u, 1u}}, {JINJA_EXPRESSION_SCOPE_PARAMETER, {2u, 1u}}};
    size_t offset = SIZE_MAX;
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("x y"), NULL, late, 2u, &scope, &offset),
                JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)2u);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
    const JINJA_EXPRESSION_SCOPE_EVENT duplicate[] = {declaration, declaration};
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("x"), NULL, duplicate, 2u, &scope, NULL),
                JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
    const char *bad[] = {"x.y", " x", "x ", "true", "x+y"};
    for (size_t i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
      const JINJA_EXPRESSION_SCOPE_EVENT event = {JINJA_EXPRESSION_SCOPE_READ, {0u, strlen(bad[i])}};
      check_equal(jinja_expression_analyze_scope(vstr_from_cstr(bad[i]), NULL, &event, 1u, &scope, NULL),
                  JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
    }
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr("x"), &scope, NULL, 0u, &scope, NULL),
                JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
  }

  it("bounds local symbols and events without publishing a truncated scope") {
    enum { NAME_BYTES = 8, NAME_COUNT = JINJA_EXPRESSION_MAX_REFERENCES + 1u };
    char source[NAME_COUNT * NAME_BYTES];
    JINJA_EXPRESSION_SCOPE_EVENT events[NAME_COUNT];
    size_t used = 0u;
    for (size_t i = 0u; i < NAME_COUNT; ++i) {
      int length = snprintf(source + used, sizeof(source) - used, "n%zu ", i);
      check_greater(length, 0);
      events[i] = (JINJA_EXPRESSION_SCOPE_EVENT){JINJA_EXPRESSION_SCOPE_STORE, {used, (size_t)length - 1u}};
      used += (size_t)length;
    }
    check_equal(jinja_expression_analyze_scope(vstr_from_buf(source, used), NULL, events, NAME_COUNT - 1u,
        &scope, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(scope.count, (size_t)JINJA_EXPRESSION_MAX_REFERENCES);
    static JINJA_EXPRESSION_SCOPE previous;
    previous = scope;
    size_t offset = 0u;
    check_equal(jinja_expression_analyze_scope(vstr_from_buf(source, used), NULL, events, NAME_COUNT,
        &scope, &offset), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(offset, events[NAME_COUNT - 1u].name.offset);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
    check_equal(jinja_expression_analyze_scope(vstr_from_buf(source, used), NULL, events,
        JINJA_EXPRESSION_MAX_SCOPE_EVENTS + 1u, &scope, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
  }

  it("bounds lexical ancestry without recursion") {
    static JINJA_EXPRESSION_SCOPE chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH];
    for (size_t i = 0u; i < JINJA_EXPRESSION_MAX_SCOPE_DEPTH; ++i)
      check_equal(jinja_expression_analyze_scope(vstr_from_cstr(""), i == 0u ? NULL : &chain[i - 1u],
          NULL, 0u, &chain[i], NULL), JINJA_EXPRESSION_PARSE_OK);
    static JINJA_EXPRESSION_SCOPE previous;
    memset(&scope, 0, sizeof(scope));
    previous = scope;
    check_equal(jinja_expression_analyze_scope(vstr_from_cstr(""), &chain[JINJA_EXPRESSION_MAX_SCOPE_DEPTH - 1u],
        NULL, 0u, &scope, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&scope, &previous, sizeof(scope)), 0);
  }
}

spec("Jinja named Unicode escapes") {
  static JINJA_EXPRESSION_TREE tree;
  it("rejects unknown malformed and sequence names atomically") {
    static const char *sources[] = {"'\\N{UNKNOWN CHARACTER}'", "'\\N{}'", "'\\N'", "'\\N{LF'",
        "'\\N{KEYCAP DIGIT ONE}'", "'\\N{hangul syllable ga}'", "'\\N{LF}' '\\N{bad}'"};
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("42"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    static JINJA_EXPRESSION_TREE previous;
    previous = tree;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("named escape: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
    }
  }
  it("validates exact length names without reading beyond a truncated token") {
    const char source[] = {'\'', '\\', 'N', '{', 'L', 'F', '}', '\''};
    const vstr full = vstr_from_buf(source, sizeof(source));
    check_equal(jinja_expression_parse_tree(full, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_lexical_token_length(full, 0u), sizeof(source));
    for (size_t length = 1u; length < sizeof(source); ++length) {
      const vstr part = vstr_from_buf(source, length);
      check_equal(jinja_expression_parse_tree(part, &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(jinja_expression_lexical_token_length(part, 0u), SIZE_MAX);
    }
    check_equal(jinja_expression_lexical_token_length(vstr_from_cstr("'\\N{UNKNOWN}'"), 0u), SIZE_MAX);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("'\\\\N{UNKNOWN}' '\\N{LF}'"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }
}

spec("Jinja Unicode integer syntax") {
  static JINJA_EXPRESSION_TREE tree;
  it("handles exact length Unicode digits and rejects each truncated UTF8 tail") {
    const unsigned char bmp[] = {'1', 0xd9, 0xa2};
    const unsigned char supplementary[] = {'1', 0xf0, 0x9d, 0x9f, 0x90};
    const vstr cases[] = {vstr_from_buf((const char *)bmp, sizeof(bmp)),
        vstr_from_buf((const char *)supplementary, sizeof(supplementary))};
    static JINJA_EXPRESSION_TREE previous;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      check_equal(jinja_expression_parse_tree(cases[i], &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(tree.nodes[tree.root].integer, (int64_t)12);
      check_equal(jinja_expression_lexical_token_length(cases[i], 0u), cases[i].len);
      previous = tree;
      for (size_t length = 2u; length < cases[i].len; ++length) {
        const vstr truncated = vstr_from_buf(cases[i].data, length);
        check_not_equal(jinja_expression_parse_tree(truncated, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
        check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
        check_equal(jinja_expression_lexical_token_length(truncated, 0u), (size_t)1u);
      }
    }
    const unsigned char dotted[] = {'x', '.', '1', 0xd9, 0xa2, '.', '0'};
    check_equal(jinja_expression_parse_tree(vstr_from_buf((const char *)dotted, sizeof(dotted)), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_lexical_token_length(vstr_from_buf((const char *)dotted, sizeof(dotted)), 2u), (size_t)3u);
  }
  it("converts Unicode decimal digits in decimal tails and hexadecimal integers") {
    static const struct { const char *text; int64_t value; } cases[] = {
        {"1\u0662", 12}, {"1_\uff12", 12}, {"0x\u0661f", 31}, {"-0X_\uff12A", -42},
        {"1\U0001d7d0", 12}, {"1\U00011de2", 12}};
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      info("integer: %s", cases[i].text);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(cases[i].text), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(tree.nodes[tree.root].integer, cases[i].value);
    }
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x.1\u0662.0"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }
  it("rejects Unicode digits where the fixed upstream does not accept them") {
    static const char *sources[] = {"\u0661", "0\u0662", "0b\u0661", "0o\u0661", "x.\u0661",
        "\u0661.\u0662", "1\u0662.3", "1.\u0662", "1e\u0662", "x.\u0661.0"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("invalid numeric syntax: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    }
  }
  it("retains checked integer limits and atomic failure with Unicode digits") {
    static JINJA_EXPRESSION_TREE previous;
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("922337203685477580\u0667"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[tree.root].integer, INT64_MAX);
    previous = tree;
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("922337203685477580\u0668"), &tree, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("-922337203685477580\u0668"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[tree.root].integer, INT64_MIN);
  }
}

spec("Jinja calls after filters and tests") {
  static JINJA_EXPRESSION_TREE tree;
  it("parses calls on filter and test results") {
    static const char *sources[] = {"x|f()(1)", "x is f()(1)", "x|f()(1)(2)|g",
        "x is f()(1)|g", "-x|f()(1)"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("result call: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    }
  }
  it("requires parentheses before looking up attributes or items on filtered results") {
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x|f().a"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x|f()[0]"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("(x|f())[0]"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }
  it("keeps filter arguments separate from calls on the result") {
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x|f(1)(2)"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_CONDITION *call = &tree.nodes[tree.root];
    check_equal(call->kind, JINJA_EXPRESSION_CONDITION_CALL);
    check_equal(tree.nodes[tree.collection_items[call->first_collection_item].value_condition].integer, (int64_t)2);
    const JINJA_EXPRESSION_CONDITION *filter = &tree.nodes[call->left_condition];
    check_equal(filter->kind, JINJA_EXPRESSION_CONDITION_FILTER);
    check_equal(tree.nodes[tree.collection_items[filter->first_collection_item].value_condition].integer, (int64_t)1);
  }
  it("keeps shorthand argument calls inside the test but calls explicit test results") {
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x is f y(1)(2)"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_CONDITION *test = &tree.nodes[tree.root];
    check_equal(test->kind, JINJA_EXPRESSION_CONDITION_TEST);
    const JINJA_EXPRESSION_CONDITION *argument = &tree.nodes[tree.collection_items[test->first_collection_item].value_condition];
    check_equal(argument->kind, JINJA_EXPRESSION_CONDITION_CALL);
    check_equal(tree.nodes[argument->left_condition].kind, JINJA_EXPRESSION_CONDITION_CALL);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x is f(1)(2)"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_CONDITION *call = &tree.nodes[tree.root];
    check_equal(call->kind, JINJA_EXPRESSION_CONDITION_CALL);
    test = &tree.nodes[call->left_condition];
    check_equal(test->kind, JINJA_EXPRESSION_CONDITION_TEST);
    check_equal(tree.nodes[tree.collection_items[test->first_collection_item].value_condition].integer, (int64_t)1);
    check_equal(tree.nodes[tree.collection_items[call->first_collection_item].value_condition].integer, (int64_t)2);
  }
  it("bounds result call chains and preserves the tree on malformed calls") {
    static JINJA_EXPRESSION_TREE previous;
    enum { SOURCE_CAPACITY = JINJA_EXPRESSION_MAX_NODES * 2u + sizeof("x|f()") };
    char source[SOURCE_CAPACITY];
    size_t length = sizeof("x|f()") - 1u;
    memcpy(source, "x|f()", length);
    for (size_t i = 0u; i < JINJA_EXPRESSION_MAX_NODES; ++i) {
      source[length++] = '(';
      source[length++] = ')';
    }
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("1"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    previous = tree;
    check_equal(jinja_expression_parse_tree(vstr_from_buf(source, length), &tree, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x|f()(1,,2)"), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
  }
}

spec("Jinja string escape syntax") {
  static JINJA_EXPRESSION_TREE tree;
  it("rejects eight digit Unicode escapes beyond the maximum code point") {
    static const char *sources[] = {"'\\U00110000'", "'\\U01000000'", "'\\UFFFFFFFF'",
        "f('\\U00110000')", "'ok' '\\U00110000'"};
    static JINJA_EXPRESSION_TREE previous;
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("1"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    previous = tree;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("out of range escape: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
    }
  }
  it("accepts Unicode escape boundaries without imposing UTF8 output restrictions on syntax") {
    static const char *sources[] = {"'\\U00000000'", "'\\U000fffff'", "'\\U00100000'", "'\\U0010FFFF'",
        "'\\U0000d800'", "'\\udfff'", "'\\U0010FFFF0'", "'\\\\UFFFFFFFF'"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }
  it("rejects malformed fixed width escapes before publishing an expression tree") {
    static const char *sources[] = {"'\\xZ1'", "'\\x1'", "'\\u123'", "'\\u12XZ'", "'\\U0001234'",
        "f('\\x')", "['ok', '\\u']", "'ok' '\\x0'"};
    static JINJA_EXPRESSION_TREE previous;
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("1"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    previous = tree;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("invalid escape: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
    }
  }
  it("retains legal fixed width unknown and escaped backslash spellings") {
    static const char *sources[] = {"'\\x41'", "'\\u00e9'", "'\\U0001f600'", "'\\q'", "'\\\\xZ1'",
        "'\\777'", "'\\x41Z'", "'\\\n'", "'a' '\\u4f60'"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }
  it("reports the malformed string token location inside a larger expression") {
    size_t offset = SIZE_MAX;
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("f('\\xZ1')"), &tree, &offset), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)2u);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("'ok' '\\u12'"), &tree, &offset), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)5u);
  }
}

spec("Jinja subscript syntax") {
  static JINJA_EXPRESSION_TREE tree;
  it("parses numeric dot items including chained indexes and shorthand test arguments") {
    static const char *sources[] = {"x.0", "x.0.1", "x.0xA", "v is custom x.0"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("numeric item: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    }
  }
  it("parses empty and compound subscript keys with independent slices") {
    static const char *sources[] = {"x[]", "x[1,2]", "x[:,1]", "x[::2,1:3]", "v is custom x[:,1]"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("compound item: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    }
  }
  it("rejects signed dot indexes floats and trailing subscript commas") {
    static const char *sources[] = {"x.-1", "x.+1", "x. 0.1", "x[1,]", "x[:,]", "x[,1]"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
  }
  it("retains compound slice keys in source order without conflating tuples and slices") {
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x[:,1:3]"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_CONDITION *lookup = &tree.nodes[tree.root];
    check_equal(lookup->kind, JINJA_EXPRESSION_CONDITION_ITEM_LOOKUP);
    const JINJA_EXPRESSION_CONDITION *key = &tree.nodes[lookup->right_condition];
    check_equal(key->kind, JINJA_EXPRESSION_CONDITION_TUPLE);
    check_equal(key->collection_item_count, (size_t)2u);
    size_t item = key->first_collection_item;
    const JINJA_EXPRESSION_CONDITION *slice = &tree.nodes[tree.collection_items[item].value_condition];
    check_equal(slice->kind, JINJA_EXPRESSION_CONDITION_SLICE);
    check_equal(slice->collection_item_count, (size_t)3u);
    size_t bound = slice->first_collection_item;
    check_equal(tree.nodes[tree.collection_items[bound].value_condition].kind, JINJA_EXPRESSION_CONDITION_NONE);
    item = tree.collection_items[item].next;
    slice = &tree.nodes[tree.collection_items[item].value_condition];
    check_equal(slice->kind, JINJA_EXPRESSION_CONDITION_SLICE);
    bound = slice->first_collection_item;
    check_equal(tree.nodes[tree.collection_items[bound].value_condition].integer, (int64_t)1);
    bound = tree.collection_items[bound].next;
    check_equal(tree.nodes[tree.collection_items[bound].value_condition].integer, (int64_t)3);
    check_equal(tree.collection_items[item].next, SIZE_MAX);
  }
  it("bounds compound subscripts and preserves the prior tree on failure") {
    static JINJA_EXPRESSION_TREE previous;
    enum { SOURCE_CAPACITY = JINJA_EXPRESSION_MAX_NODES * 2u + 4u };
    char source[SOURCE_CAPACITY];
    size_t length = 0u;
    source[length++] = 'x'; source[length++] = '[';
    for (size_t i = 0u; i < JINJA_EXPRESSION_MAX_NODES; ++i) {
      if (i != 0u) source[length++] = ',';
      source[length++] = '1';
    }
    source[length++] = ']';
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x[1:3]"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[tree.root].kind, JINJA_EXPRESSION_CONDITION_SLICE_LOOKUP);
    previous = tree;
    check_equal(jinja_expression_parse_tree(vstr_from_buf(source, length), &tree, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(memcmp(&tree, &previous, sizeof(tree)), 0);
  }
  it("accepts a single underscore after a numeric radix prefix") {
    static const char *sources[] = {"x.0x_A", "x.0b_1", "x.0o_7", "0x_A", "-0b_1",
        "x.00", "x.0_0", "01.2", "x.0_0.2"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
  }
  it("rejects nonzero decimal leading zeros including after numeric dots") {
    static const char *sources[] = {"x.01", "x.0_1", "x.0_1.2", "01", "0_1", "x.0x__A"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
  }
}

spec("Jinja registered name syntax") {
  static JINJA_EXPRESSION_TREE tree;
  it("parses dotted keyword and Unicode filter and test names without registration") {
    static const char *sources[] = {"value|group.foo", "value|true", "value|not",
        "value|group . 名称(1)", "value is group.foo(1)", "value is custom",
        "value is not group.true", "value|group.not|default(0)", "value is custom and true"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("expression: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    }
  }
  it("rejects incomplete registered names") {
    static const char *sources[] = {"value|group.", "value|group..foo", "value is group.", "value|1"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
  }
  it("retains complete registered name spans and builtin availability separately") {
    const char *source = "x is defined . 名称";
    check_equal(jinja_expression_parse_tree(vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_CONDITION *node = &tree.nodes[tree.root];
    check_equal(node->test_supported, 0);
    check_equal(node->test_name.offset, (size_t)5u);
    check_equal(node->test_name.length, strlen("defined . 名称"));
    check_equal(memcmp(source + node->test_name.offset, "defined . 名称", node->test_name.length), 0);
    check_equal(jinja_expression_parse_tree(vstr_from_cstr("x is defined"), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[tree.root].test_supported, 1);
    source = "x | group . 名称";
    check_equal(jinja_expression_parse_tree(vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[tree.root].path.length, strlen("group . 名称"));
  }
}

spec("Jinja Unicode expression lexer") {
  it("does not read beyond exact length or truncated UTF8 identifiers") {
    const char input[] = {'\xe5', '\x90', '\x8d'};
    JINJA_EXPRESSION_LEXER lexer;
    JINJA_EXPRESSION_TOKEN token;
    int kind;
    jinja_expression_lexer_init(&lexer, vstr_from_buf(input, sizeof(input)));
    check_equal(jinja_expression_lexer_next(&lexer, &kind, &token), JINJA_EXPRESSION_LEX_TOKEN);
    check_equal(kind, JINJA_EXPRESSION_TOKEN_IDENTIFIER);
    check_equal(token.length, sizeof(input));
    check_equal(jinja_expression_lexer_next(&lexer, &kind, &token), JINJA_EXPRESSION_LEX_EOF);
    for (size_t length = 1u; length < sizeof(input); ++length) {
      jinja_expression_lexer_init(&lexer, vstr_from_buf(input, length));
      check_less(jinja_expression_lexer_next(&lexer, &kind, &token), 0);
    }
  }

  it("keeps keyword prefixes and combining marks in the original identifier span") {
    static const char *names[] = {"not名", "true名", "e\xcc\x81", "变量١", "ª", "_名"};
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
      JINJA_EXPRESSION_LEXER lexer;
      JINJA_EXPRESSION_TOKEN token;
      int kind;
      jinja_expression_lexer_init(&lexer, vstr_from_cstr(names[i]));
      check_equal(jinja_expression_lexer_next(&lexer, &kind, &token), JINJA_EXPRESSION_LEX_TOKEN);
      check_equal(kind, JINJA_EXPRESSION_TOKEN_IDENTIFIER);
      check_equal(token.length, strlen(names[i]));
      check_equal(memcmp(token.text, names[i], token.length), 0);
    }
  }
}

spec("Jinja for header parser") {
  static JINJA_TEMPLATE_FOR_HEADER header;

  it("parses for tuple targets filtering and recursive modifiers") {
    static const char *sources[] = {"x in xs", "a,b in pairs if a recursive",
        "(a,(b,c)) in rows", "x in (left if flag else right)", "x in 1,2,3",
        "x in xs if x if flag else false recursive", "not in xs", "x in xs is in recursive"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&header, 0, sizeof(header));
      info("for header: %s", sources[i]);
      check_equal(jinja_expression_parse_for_header(vstr_from_cstr(sources[i]), &header, NULL),
          JINJA_EXPRESSION_PARSE_OK);
      check_greater(header.targets.count, (size_t)0u);
      check_greater(header.iterable_tree.count, (size_t)0u);
      check_equal(header.has_test, i == 1u || i == 5u);
      check_equal(header.recursive, i == 1u || i == 5u);
    }
  }

  it("rejects malformed loop targets missing clauses and reordered modifiers") {
    static const char *sources[] = {"x.y in xs", "true in xs", "x xs", "x in",
        "x in xs recursive if x", "x in xs if", "x in xs recursive recursive", "x in xs if x,y",
        "x, in xs", "x in xs, if x"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&header, 0, sizeof(header));
      info("invalid for header: %s", sources[i]);
      check_equal(jinja_expression_parse_for_header(vstr_from_cstr(sources[i]), &header, NULL),
          JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(header.targets.count, (size_t)0u);
      check_equal(header.iterable_tree.count, (size_t)0u);
    }
  }

  it("keeps recursive identifiers inside iterable and filter expressions") {
    static const char *sources[] = {"x in obj.recursive", "x in xs if obj.recursive",
        "x in xs if recursive", "in in xs", "x in xs, recursive", "a,in in xs", "x in xs, if"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&header, 0, sizeof(header));
      check_equal(jinja_expression_parse_for_header(vstr_from_cstr(sources[i]), &header, NULL),
          JINJA_EXPRESSION_PARSE_OK);
      check_equal(header.recursive, 0);
    }
  }

  it("retains exact length spans and does not publish malformed headers") {
    const char source[] = "x in xs if x recursive";
    size_t offset = SIZE_MAX;
    memset(&header, 0, sizeof(header));
    check_equal(jinja_expression_parse_for_header(vstr_from_buf(source, sizeof(source) - 1u), &header, &offset),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(header.iterable.offset, (size_t)4u);
    check_equal(header.test.offset, (size_t)10u);
    check_equal(memcmp(source + header.iterable.offset, " xs ", header.iterable.length), 0);
    check_equal(jinja_expression_parse_for_header(vstr_from_cstr("x in a + * b"), &header, &offset),
        JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)9u);
    check_equal(header.recursive, 1);
    check_equal(header.has_test, 1);
  }

}

spec("Jinja assignment parser") {
  static JINJA_EXPRESSION_TREE targets;
  static JINJA_EXPRESSION_TREE tree;
  static int capture;
  before_each() {
    memset(&targets, 0, sizeof(targets));
    capture = 0;
  }

  it("parses named block modifiers in Jinja order") {
    static const char *sources[] = {"body", "body scoped", "body required", "body scoped required"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_TEMPLATE_BLOCK_HEADER header = {0};
      check_equal(jinja_expression_parse_block_header(vstr_from_cstr(sources[i]), &header, NULL),
          JINJA_EXPRESSION_PARSE_OK);
      check_equal(header.name.length, sizeof("body") - 1u);
      check_equal(memcmp(sources[i] + header.name.offset, "body", header.name.length), 0);
      check_equal(header.scoped, i == 1u || i == 3u);
      check_equal(header.required, i >= 2u);
    }
  }

  it("allows literal and keyword spellings as block names") {
    static const char *names[] = {"true","False","none","not","and","scoped","required"};
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
      JINJA_TEMPLATE_BLOCK_HEADER header = {0};
      check_equal(jinja_expression_parse_block_header(vstr_from_cstr(names[i]), &header, NULL),
          JINJA_EXPRESSION_PARSE_OK);
      check_equal(header.name.length, strlen(names[i]));
      check_equal(header.scoped, 0);
      check_equal(header.required, 0);
      check_equal(jinja_expression_parse_endblock(vstr_from_cstr(names[i]), vstr_from_cstr(names[i]), NULL),
          JINJA_EXPRESSION_PARSE_OK);
    }
  }

  it("rejects invalid block names duplicate modifiers and reversed modifier order") {
    static const char *sources[] = {"", "body-name", "body.name", "'body'", "1",
        "body required scoped", "body scoped scoped", "body required required", "body other"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_TEMPLATE_BLOCK_HEADER header = {.scoped = 1};
      check_equal(jinja_expression_parse_block_header(vstr_from_cstr(sources[i]), &header, NULL),
          JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(header.name.length, (size_t)0u);
      check_equal(header.scoped, 1);
    }
  }

  it("matches optional endblock names with exact length input") {
    const char exact[] = {'b','o','d','y'};
    vstr name = vstr_from_buf(exact, sizeof(exact));
    check_equal(jinja_expression_parse_endblock(vstr_from_cstr(" \t"), name, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_parse_endblock(name, name, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(jinja_expression_parse_endblock(vstr_from_cstr("other"), name, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_expression_parse_endblock(vstr_from_cstr("body scoped"), name, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(jinja_expression_parse_endblock(vstr_from_cstr("body-name"), name, NULL), JINJA_EXPRESSION_PARSE_INVALID);
  }

  it("reports block header errors relative to the supplied header") {
    JINJA_TEMPLATE_BLOCK_HEADER header = {0};
    size_t offset = SIZE_MAX;
    check_equal(jinja_expression_parse_block_header(vstr_from_cstr("  body required scoped"), &header, &offset),
        JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)16u);
    check_equal(header.required, 0);
    check_equal(jinja_expression_parse_endblock(vstr_from_cstr(" \tother"), vstr_from_cstr("body"), &offset),
        JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)2u);
  }

  it("parses template references with context modifiers and import aliases") {
    static const char *sources[] = {"base if enabled else 'base.html'",
        "['a','b'] ignore missing without context", "'macros' as m with context",
        "'macros' import render as r, helper without context"};
    static const JINJA_TEMPLATE_REFERENCE_KIND kinds[] = {JINJA_TEMPLATE_REFERENCE_EXTENDS,
        JINJA_TEMPLATE_REFERENCE_INCLUDE, JINJA_TEMPLATE_REFERENCE_IMPORT, JINJA_TEMPLATE_REFERENCE_FROM};
    static const size_t counts[] = {0u,0u,1u,2u};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_TEMPLATE_REFERENCE result = {0};
      check_equal(jinja_expression_parse_template_reference(vstr_from_cstr(sources[i]), kinds[i],
          &result, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(result.name_count, counts[i]);
      check_greater(result.tree.count, (size_t)0u);
      check_equal(result.with_context, i == 2u);
      check_equal(result.ignore_missing, i == 1u);
    }
  }

  it("keeps reference modifier words inside expressions and defaults include context to true") {
    static const char *sources[] = {"templates.with", "'ignore missing with context'",
        "base if with else fallback", "f('as',key='import')"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_TEMPLATE_REFERENCE result = {0};
      check_equal(jinja_expression_parse_template_reference(vstr_from_cstr(sources[i]),
          JINJA_TEMPLATE_REFERENCE_INCLUDE, &result, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(result.expression.length, strlen(sources[i]));
      check_equal(result.with_context, 1);
      check_equal(result.ignore_missing, 0);
    }
  }

  it("rejects malformed template reference suffixes without publishing partial imports") {
    static const char *sources[] = {"'x' without context ignore missing", "'x' ignore", "'x' with",
        "'x' as", "'x' as true", "'x' import _private", "'x' import f,", "'x' import f as",
        "'x' import f, g garbage", "'x','y'"};
    static const JINJA_TEMPLATE_REFERENCE_KIND kinds[] = {JINJA_TEMPLATE_REFERENCE_INCLUDE,
        JINJA_TEMPLATE_REFERENCE_INCLUDE,JINJA_TEMPLATE_REFERENCE_INCLUDE,
        JINJA_TEMPLATE_REFERENCE_IMPORT,JINJA_TEMPLATE_REFERENCE_IMPORT,
        JINJA_TEMPLATE_REFERENCE_FROM,JINJA_TEMPLATE_REFERENCE_FROM,JINJA_TEMPLATE_REFERENCE_FROM,
        JINJA_TEMPLATE_REFERENCE_FROM,JINJA_TEMPLATE_REFERENCE_EXTENDS};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_TEMPLATE_REFERENCE result = {0};
      check_equal(jinja_expression_parse_template_reference(vstr_from_cstr(sources[i]), kinds[i],
          &result, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(result.name_count, (size_t)0u);
      check_equal(result.tree.count, (size_t)0u);
    }
  }

  it("does not split template modifiers out of shorthand test arguments") {
    static const char *sources[] = {"value is equalto with", "value is equalto with without context",
        "value is equalto import import f", "value is equalto as as m"};
    static const JINJA_TEMPLATE_REFERENCE_KIND kinds[] = {JINJA_TEMPLATE_REFERENCE_INCLUDE,
        JINJA_TEMPLATE_REFERENCE_INCLUDE,JINJA_TEMPLATE_REFERENCE_FROM,JINJA_TEMPLATE_REFERENCE_IMPORT};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_TEMPLATE_REFERENCE result = {0};
      check_equal(jinja_expression_parse_template_reference(vstr_from_cstr(sources[i]), kinds[i],
          &result, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(result.tree.nodes[result.tree.root].kind, JINJA_EXPRESSION_CONDITION_TEST);
      check_equal(result.tree.nodes[result.tree.root].collection_item_count, (size_t)1u);
    }
    JINJA_TEMPLATE_REFERENCE result = {0};
    check_equal(jinja_expression_parse_template_reference(vstr_from_cstr("value is defined with context"),
        JINJA_TEMPLATE_REFERENCE_INCLUDE, &result, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(result.tree.count, (size_t)0u);
  }

  it("preserves the expression error offset inside a template reference") {
    JINJA_TEMPLATE_REFERENCE result = {0};
    size_t offset = SIZE_MAX;
    check_equal(jinja_expression_parse_template_reference(vstr_from_cstr("a + * b"),
        JINJA_TEMPLATE_REFERENCE_EXTENDS, &result, &offset), JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(offset, (size_t)4u);
    check_equal(result.tree.count, (size_t)0u);
  }

  it("retains import names aliases and exact length template expression spans") {
    const char source[] = "'macros' import render as r, helper";
    JINJA_TEMPLATE_REFERENCE result = {0};
    check_equal(jinja_expression_parse_template_reference(vstr_from_buf(source, sizeof(source) - 1u),
        JINJA_TEMPLATE_REFERENCE_FROM, &result, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(result.name_count, (size_t)2u);
    check_equal(result.names[0].name.length, sizeof("render") - 1u);
    check_equal(memcmp(source + result.names[0].name.offset, "render", result.names[0].name.length), 0);
    check_equal(result.names[0].alias.length, (size_t)1u);
    check_equal(source[result.names[0].alias.offset], 'r');
    check_equal(result.names[1].alias.offset, result.names[1].name.offset);
    const char exact[] = {'\'', 'x', '\''};
    check_equal(jinja_expression_parse_template_reference(vstr_from_buf(exact, sizeof(exact)),
        JINJA_TEMPLATE_REFERENCE_INCLUDE, &result, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(result.expression.length, sizeof(exact));
    check_equal(result.name_count, (size_t)0u);
  }

  it("bounds imported names and does not publish a partially filled table") {
    enum { IMPORT_NAME_BYTES = 8 };
    char source[sizeof("'x' import ") + (JINJA_EXPRESSION_MAX_NODES + 1u) * IMPORT_NAME_BYTES];
    size_t used = sizeof("'x' import ") - 1u;
    memcpy(source, "'x' import ", used);
    for (size_t i = 0u; i <= JINJA_EXPRESSION_MAX_NODES; ++i) {
      int length = snprintf(source + used, sizeof(source) - used, "%sf%zu", i == 0u ? "" : ",", i);
      check_greater(length, 0);
      check_less((size_t)length, sizeof(source) - used);
      used += (size_t)length;
      if (i + 1u < JINJA_EXPRESSION_MAX_NODES) continue;
      JINJA_TEMPLATE_REFERENCE result = {0};
      JINJA_EXPRESSION_PARSE_STATUS expected = i < JINJA_EXPRESSION_MAX_NODES
          ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_CAPACITY;
      check_equal(jinja_expression_parse_template_reference(vstr_from_buf(source, used),
          JINJA_TEMPLATE_REFERENCE_FROM, &result, NULL), expected);
      check_equal(result.name_count, i < JINJA_EXPRESSION_MAX_NODES ? (size_t)JINJA_EXPRESSION_MAX_NODES : (size_t)0u);
    }
  }

  it("parses call block headers with optional caller defaults and expanded call arguments") {
    static const char *sources[] = {"render()", "() render()", "(x,y=x+1) render(*xs,**kw)",
        "(x={'a':(1,2)}) registry['render'](1)", "() (render())"};
    static const size_t counts[] = {0u, 0u, 2u, 1u, 0u};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
      JINJA_EXPRESSION_SPAN call = {0};
      memset(&tree, 0, sizeof(tree));
      info("call header: %s", sources[i]);
      check_equal(jinja_expression_parse_call_header(vstr_from_cstr(sources[i]), &signature,
          &call, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_equal(signature.parameter_count, counts[i]);
      check_equal(signature.name.length, (size_t)0u);
      check_equal(tree.nodes[tree.root].kind, JINJA_EXPRESSION_CONDITION_CALL);
      check_equal(call.offset + call.length, strlen(sources[i]));
    }
  }

  it("rejects noncall roots and malformed caller signatures without publishing outputs") {
    static const char *sources[] = {"", "render", "render()+1", "render()|safe",
        "(x,) render()", "(x=1,y) render()", "(x,x) render()", "(*args) render()",
        "(x) render", "() render() if true else other()", "() render(),other()",
        "not render()", "not not render()"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
      JINJA_EXPRESSION_SPAN call = {0};
      memset(&tree, 0, sizeof(tree));
      check_equal(jinja_expression_parse_call_header(vstr_from_cstr(sources[i]), &signature,
          &call, &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(signature.parameter_count, (size_t)0u);
      check_equal(call.length, (size_t)0u);
      check_equal(tree.count, (size_t)0u);
    }
  }

  it("retains caller default spans and parameter references without borrowing temporary storage") {
    const char source[] = "(x=y,y=2,z=outer) render(x)";
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
    JINJA_EXPRESSION_SPAN call = {0};
    memset(&tree, 0, sizeof(tree));
    check_equal(jinja_expression_parse_call_header(vstr_from_buf(source, sizeof(source) - 1u),
        &signature, &call, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.default_reference_count, (size_t)2u);
    check_equal(signature.default_reference_parameters[0], (size_t)1u);
    check_equal(signature.default_reference_parameters[1], SIZE_MAX);
    const JINJA_EXPRESSION_SPAN name = signature.parameters[0].name;
    check_equal(memcmp(source + name.offset, "x", name.length), 0);
    const JINJA_EXPRESSION_SPAN target = tree.nodes[tree.nodes[tree.root].left_condition].path;
    check_equal(target.length, sizeof("render") - 1u);
    check_equal(memcmp(source + call.offset + target.offset, "render", target.length), 0);
    const char exact[] = {'f','(',')'};
    check_equal(jinja_expression_parse_call_header(vstr_from_buf(exact, sizeof(exact)),
        &signature, &call, &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(call.length, sizeof(exact));
    check_equal(signature.parameter_count, (size_t)0u);
  }

  it("parses argument expansion in calls filters and tests") {
    static const char *sources[] = {"f(1,*xs,k=2,**kw)", "x|default(*xs,**kw)",
        "x is divisibleby(*xs,**kw)", "f(k=1,*xs)", "f(**kw)", "f(*xs,)"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      info("expanded call: %s", sources[i]);
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
      check_greater(tree.nodes[tree.root].collection_item_count, (size_t)0u);
    }
    memset(&tree, 0, sizeof(tree));
    const char source[] = "f(1,*xs,k=2,**kw)";
    check_equal(jinja_expression_parse_tree(vstr_from_cstr(source), &tree, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_CONDITION *call = &tree.nodes[tree.root];
    check_equal(call->collection_item_count, (size_t)4u);
    size_t item = call->first_collection_item;
    check_equal(tree.collection_items[item].expansion, JINJA_EXPRESSION_EXPANSION_NONE);
    item = tree.collection_items[item].next;
    check_equal(tree.collection_items[item].expansion, JINJA_EXPRESSION_EXPANSION_POSITIONAL);
    JINJA_EXPRESSION_SPAN path = tree.nodes[tree.collection_items[item].value_condition].path;
    check_equal(path.length, (size_t)2u);
    check_equal(memcmp(source + path.offset, "xs", path.length), 0);
    item = tree.collection_items[item].next;
    check_equal(tree.collection_items[item].keyword.length, (size_t)1u);
    item = tree.collection_items[item].next;
    check_equal(tree.collection_items[item].expansion, JINJA_EXPRESSION_EXPANSION_KEYWORD);
    path = tree.nodes[tree.collection_items[item].value_condition].path;
    check_equal(path.length, (size_t)2u);
    check_equal(memcmp(source + path.offset, "kw", path.length), 0);
    check_equal(tree.collection_items[item].next, SIZE_MAX);
  }

  it("rejects invalid argument expansion ordering without publishing a tree") {
    static const char *sources[] = {"f(*a,*b)", "f(**a,**b)", "f(**a,k=1)",
        "f(**a,*b)", "f(*a,1)", "f(k=1,2)", "f(*)", "f(**)"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      check_equal(jinja_expression_parse_tree(vstr_from_cstr(sources[i]), &tree, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)0u);
    }
  }

  it("parses macro parameters and defaults without evaluating them") {
    const char source[] = "render(a,b=a+1,c={'x':(1,2)},d=1/0)";
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
    check_equal(jinja_expression_parse_macro_signature(vstr_from_buf(source, sizeof(source) - 1u),
        &signature, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.name.offset, (size_t)0u);
    check_equal(signature.name.length, sizeof("render") - 1u);
    check_equal(signature.parameter_count, (size_t)4u);
    check_equal(signature.parameters[0].default_expression.length, (size_t)0u);
    const JINJA_EXPRESSION_SPAN value = signature.parameters[1].default_expression;
    check_equal(value.length, sizeof("a+1") - 1u);
    check_equal(memcmp(source + value.offset, "a+1", value.length), 0);
    const JINJA_EXPRESSION_SPAN last = signature.parameters[3].default_expression;
    check_equal(last.length, sizeof("1/0") - 1u);
    check_equal(memcmp(source + last.offset, "1/0", last.length), 0);
  }

  it("records default expression reads without confusing attribute or argument names") {
    const char source[] = "f(a=user . name,b=fn(user[key],label=other)|default(fallback),c=a<limit)";
    static const char *expected[] = {"user", "fn", "key", "other", "fallback", "a", "limit"};
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
    check_equal(jinja_expression_parse_macro_signature(vstr_from_buf(source, sizeof(source) - 1u),
        &signature, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.default_reference_count, sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0u; i < signature.default_reference_count; ++i) {
      JINJA_EXPRESSION_SPAN span = signature.default_references[i];
      check_equal(span.length, strlen(expected[i]));
      check_equal(memcmp(source + span.offset, expected[i], span.length), 0);
    }
    check_equal(signature.default_references[0].offset, (size_t)4u);
  }

  it("retains default reads from short circuit and conditional branches without evaluation") {
    const char source[] = "f(a=false and hidden,b=left if test else right,c=(x<y<z),"
        "d={'literal': value},e=1/0,g=caller(),h=varargs[0],i=kwargs.name)";
    static const char *expected[] = {"hidden", "left", "test", "right", "x", "y", "z", "value",
        "caller", "varargs", "kwargs"};
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
    check_equal(jinja_expression_parse_macro_signature(vstr_from_cstr(source), &signature, NULL),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.default_reference_count, sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0u; i < signature.default_reference_count; ++i) {
      JINJA_EXPRESSION_SPAN span = signature.default_references[i];
      check_equal(span.length, strlen(expected[i]));
      check_equal(memcmp(source + span.offset, expected[i], span.length), 0);
    }
  }

  it("accepts macro keyword names and exact length empty signatures") {
    static const char *sources[] = {"not(and,or,in,if,else,is,not)",
        "f(a='x,y=z',b=range(1,3),c=(1 if true else 2))", "f(a=not false,b=1 not in [2])"};
    static const size_t counts[] = {7u, 3u, 2u};
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      check_equal(jinja_expression_parse_macro_signature(vstr_from_cstr(sources[i]), &signature, NULL),
          JINJA_EXPRESSION_PARSE_OK);
      check_equal(signature.parameter_count, counts[i]);
    }
    const char exact[] = {'f','(',')'};
    check_equal(jinja_expression_parse_macro_signature(vstr_from_buf(exact, sizeof(exact)), &signature, NULL),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.parameter_count, (size_t)0u);
  }

  it("binds default reads to all declared parameters including self and later parameters") {
    const char source[] = "f(a=b,b=2,c=c,d=external,e=a)";
    static const size_t expected[] = {1u, 2u, SIZE_MAX, 0u};
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
    check_equal(jinja_expression_parse_macro_signature(vstr_from_buf(source, sizeof(source) - 1u),
        &signature, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.default_reference_count, sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0u; i < signature.default_reference_count; ++i)
      check_equal(signature.default_reference_parameters[i], expected[i]);
  }

  it("resolves special spellings as ordinary parameters when explicitly declared") {
    const char source[] = "f(a=caller(),b=varargs[0],c=kwargs.name,caller=none,varargs=(),kwargs=none)";
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
    check_equal(jinja_expression_parse_macro_signature(vstr_from_cstr(source), &signature, NULL),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.default_reference_count, (size_t)3u);
    check_equal(signature.default_reference_parameters[0], (size_t)3u);
    check_equal(signature.default_reference_parameters[1], (size_t)4u);
    check_equal(signature.default_reference_parameters[2], (size_t)5u);
  }

  it("deduplicates default reads at their earliest source occurrence") {
    const char source[] = "f(a=fn(fn),b=(fn).attr,c=fn is defined,d=fn[fn],e='fn',g=true)";
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
    check_equal(jinja_expression_parse_macro_signature(vstr_from_buf(source, sizeof(source) - 1u),
        &signature, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.default_reference_count, (size_t)1u);
    check_equal(signature.default_references[0].offset, (size_t)4u);
    check_equal(signature.default_references[0].length, (size_t)2u);
    const char constants[] = "f(a='user',b=42,c=none,d=true,e={'key':false})";
    check_equal(jinja_expression_parse_macro_signature(vstr_from_cstr(constants), &signature, NULL),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(signature.default_reference_count, (size_t)0u);
  }

  it("bounds aggregate default references without publishing a partial signature") {
    enum { SOURCE_CAPACITY = 4096, PARAMETER_COUNT = 64 };
    char source[SOURCE_CAPACITY];
    for (size_t overflow = 0u; overflow <= 1u; ++overflow) {
      size_t used = 0u;
      source[used++] = 'f';
      source[used++] = '(';
      for (size_t i = 0u; i < PARAMETER_COUNT; ++i) {
        int written = snprintf(source + used, sizeof(source) - used, "%sp%zu=(a%zu<b%zu)%s",
            i == 0u ? "" : ",", i, i, i, overflow != 0u && i == 0u ? "+extra" : "");
        check_true(written > 0 && (size_t)written < sizeof(source) - used);
        used += (size_t)written;
      }
      source[used++] = ')';
      JINJA_EXPRESSION_MACRO_SIGNATURE signature = {.default_reference_count = SIZE_MAX};
      size_t offset = SIZE_MAX;
      check_equal(jinja_expression_parse_macro_signature(vstr_from_buf(source, used), &signature, &offset),
          overflow == 0u ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_CAPACITY);
      check_equal(signature.default_reference_count, overflow == 0u ? (size_t)128u : SIZE_MAX);
      if (overflow != 0u) check_true(offset < used);
    }
  }

  it("does not publish collected default reads when a later parameter is malformed") {
    JINJA_EXPRESSION_MACRO_SIGNATURE signature = {.default_reference_count = SIZE_MAX};
    check_equal(jinja_expression_parse_macro_signature(vstr_from_cstr("f(a=user,b=)"), &signature, NULL),
        JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(signature.default_reference_count, SIZE_MAX);
  }

  it("rejects malformed macro signatures without publishing partial parameters") {
    static const char *sources[] = {"f(a,a)", "f(a=1,b)", "f(a,)", "f(true)", "true()",
        "f(a.b)", "f((a))", "f(*args)", "f(**kwargs)", "f(a=)", "f(a=(1,])", "f(a=1+)",
        "f(a='bad)", "f(a) trailing", "f", "f(", "f(,a)", "f(a=1,,b=2)"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_EXPRESSION_MACRO_SIGNATURE signature = {.parameter_count = SIZE_MAX};
      size_t offset = SIZE_MAX;
      info("macro signature rejection %zu", i);
      check_not_equal(jinja_expression_parse_macro_signature(vstr_from_cstr(sources[i]), &signature, &offset),
          JINJA_EXPRESSION_PARSE_OK);
      check_equal(signature.parameter_count, SIZE_MAX);
      check_true(offset <= strlen(sources[i]));
    }
  }

  it("reports macro signature errors relative to the original header") {
    static const char *sources[] = {"  f(a,b=1+)", " f(a,b='bad)", " f(a,a)"};
    static const size_t expected[] = {10u, 7u, 5u};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
      size_t offset = SIZE_MAX;
      check_equal(jinja_expression_parse_macro_signature(vstr_from_cstr(sources[i]), &signature, &offset),
          JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(offset, expected[i]);
    }
  }

  it("bounds macro parameter count before writing signature storage") {
    enum { SIGNATURE_BYTES = 1024 };
    char source[SIGNATURE_BYTES];
    for (size_t count = JINJA_EXPRESSION_MAX_NODES; count <= JINJA_EXPRESSION_MAX_NODES + 1u; ++count) {
      size_t length = 2u;
      source[0] = 'f';
      source[1] = '(';
      for (size_t i = 0u; i < count; ++i) {
        int written = snprintf(source + length, sizeof(source) - length, "%sa%zu", i == 0u ? "" : ",", i);
        check_true(written > 0 && (size_t)written < sizeof(source) - length);
        length += (size_t)written;
      }
      source[length++] = ')';
      JINJA_EXPRESSION_MACRO_SIGNATURE signature = {0};
      check_equal(jinja_expression_parse_macro_signature(vstr_from_buf(source, length), &signature, NULL),
          count == JINJA_EXPRESSION_MAX_NODES ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_CAPACITY);
      check_equal(signature.parameter_count, count == JINJA_EXPRESSION_MAX_NODES ? count : (size_t)0u);
    }
  }

  it("parses inline filter block chains with original source spans") {
    const char source[] = {'t','r','i','m','|','e','s','c','a','p','e'};
    memset(&tree, 0, sizeof(tree));
    check_equal(jinja_expression_parse_filter_block(vstr_from_buf(source, sizeof(source)), &tree, NULL),
        JINJA_EXPRESSION_PARSE_OK);
    check_equal(tree.nodes[tree.root].kind, JINJA_EXPRESSION_CONDITION_FILTER);
    check_equal(tree.nodes[tree.root].path.offset, (size_t)5u);
    const JINJA_EXPRESSION_CONDITION *inner = &tree.nodes[tree.nodes[tree.root].left_condition];
    check_equal(inner->path.offset, (size_t)0u);
    check_equal(tree.nodes[inner->left_condition].kind, JINJA_EXPRESSION_CONDITION_CAPTURE);
    tree.count = 0u;
    check_equal(jinja_expression_parse_filter_block(vstr_from_cstr("trim + 1"), &tree, NULL),
        JINJA_EXPRESSION_PARSE_INVALID);
    check_equal(tree.count, (size_t)0u);
  }

  it("separates with initializers at lexical top level and borrows exact input") {
    static const char source[] = "(a,b)=(1,2), c='x,y=z'";
    memset(&tree, 0, sizeof(tree));
    vstr rhs = {0};
    size_t consumed = 0u;
    check_equal(jinja_expression_parse_with_binding(vstr_from_buf(source, sizeof(source) - 1u),
        &consumed, &rhs, &tree, &targets), JINJA_EXPRESSION_PARSE_OK);
    check_equal(consumed, sizeof("(a,b)=(1,2)") - 1u);
    check_true(rhs.data == source + sizeof("(a,b)=") - 1u);
    check_equal(rhs.len, sizeof("(1,2)") - 1u);
    check_equal(targets.nodes[targets.root].kind, JINJA_EXPRESSION_CONDITION_TUPLE);
    const char exact[] = {'c','=','1'};
    check_equal(jinja_expression_parse_with_binding(vstr_from_buf(exact, sizeof(exact)),
        &consumed, &rhs, &tree, &targets), JINJA_EXPRESSION_PARSE_OK);
    check_equal(consumed, sizeof(exact));
    check_true(rhs.data == exact + 2u);
    check_equal(tree.nodes[tree.root].integer, (int64_t)1);
  }

  it("does not publish with parser results on malformed targets or expressions") {
    static const char *sources[] = {"a", "ns.x=1", "a=", "true=1", "a=(1,]", "a='unterminated"};
    memset(&tree, 0, sizeof(tree));
    vstr rhs = vstr_from_cstr("unchanged");
    size_t consumed = SIZE_MAX;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      check_not_equal(jinja_expression_parse_with_binding(vstr_from_cstr(sources[i]),
          &consumed, &rhs, &tree, &targets), JINJA_EXPRESSION_PARSE_OK);
      check_equal(consumed, SIZE_MAX);
      check_equal(rhs.len, sizeof("unchanged") - 1u);
      check_equal(tree.count, (size_t)0u);
      check_equal(targets.count, (size_t)0u);
    }
  }

  it("parses capture headers with filter spans without fabricating source bytes") {
    static const char source[] = "x | default('a=b', true) | trim";
    memset(&tree, 0, sizeof(tree));
    vstr name = {0}, rhs = {0};
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr(source), &name, &rhs,
                                                  &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(capture, 1);
    check_true(rhs.data == source);
    check_equal(name.len, (size_t)1u);
    check_equal(name.data[0], 'x');
    check_equal(tree.nodes[tree.root].kind, JINJA_EXPRESSION_CONDITION_FILTER);
    const JINJA_EXPRESSION_SPAN filter = tree.nodes[tree.root].path;
    check_equal(filter.length, sizeof("trim") - 1u);
    check_equal(memcmp(rhs.data + filter.offset, "trim", filter.length), 0);
    const JINJA_EXPRESSION_CONDITION *default_filter = &tree.nodes[tree.nodes[tree.root].left_condition];
    check_equal(default_filter->kind, JINJA_EXPRESSION_CONDITION_FILTER);
    check_equal(tree.nodes[default_filter->left_condition].kind, JINJA_EXPRESSION_CONDITION_CAPTURE);
  }

  it("accepts exact length capture headers and leaves outputs unchanged on filter errors") {
    static const char source[] = {'x'};
    memset(&tree, 0, sizeof(tree));
    vstr name = {0}, rhs = {0};
    check_equal(jinja_expression_parse_assignment(vstr_from_buf(source, sizeof(source)), &name, &rhs,
                                                  &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(capture, 1);
    check_equal(tree.nodes[tree.root].kind, JINJA_EXPRESSION_CONDITION_CAPTURE);
    check_true(name.data == source);
    tree.count = 0u;
    targets.count = 0u;
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr("y|trim + 1"), &name, &rhs,
                                                  &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_INVALID);
    check_true(name.data == source);
    check_equal(tree.count, (size_t)0u);
    check_equal(targets.count, (size_t)0u);
    check_equal(capture, 1);
  }

  it("preserves nested tuple target names separately from RHS expression spans") {
    static const char source[] = "a,(b,c) = (1,(2,3))";
    memset(&tree, 0, sizeof(tree));
    vstr name = {0}, rhs = {0};
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr(source), &name, &rhs,
                                                  &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_CONDITION *outer = &targets.nodes[targets.root];
    check_equal(outer->kind, JINJA_EXPRESSION_CONDITION_TUPLE);
    check_equal(outer->collection_item_count, (size_t)2u);
    size_t item = outer->first_collection_item;
    const JINJA_EXPRESSION_CONDITION *first = &targets.nodes[targets.collection_items[item].value_condition];
    check_equal(first->kind, JINJA_EXPRESSION_CONDITION_PATH);
    check_equal(first->path.length, (size_t)1u);
    check_equal(source[first->path.offset], 'a');
    item = targets.collection_items[item].next;
    const JINJA_EXPRESSION_CONDITION *nested = &targets.nodes[targets.collection_items[item].value_condition];
    check_equal(nested->kind, JINJA_EXPRESSION_CONDITION_TUPLE);
    check_equal(nested->collection_item_count, (size_t)2u);
    check_true(rhs.data == source + sizeof("a,(b,c) =") - 1u);
    check_equal(tree.nodes[tree.root].kind, JINJA_EXPRESSION_CONDITION_TUPLE);
  }

  it("rejects invalid tuple targets without publishing either tree") {
    static const char *sources[] = {"a,=1", "a,(not)=1,2", "a,[b]=1,[2]", "a,true=1,2", "a,b=("};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      vstr name = {0}, rhs = {0};
      check_equal(jinja_expression_parse_assignment(vstr_from_cstr(sources[i]), &name, &rhs,
                                                    &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)0u);
      check_equal(targets.count, (size_t)0u);
      check_null(name.data);
      check_null(rhs.data);
    }
  }

  it("bounds tuple target count independently of the right hand side") {
    enum { TARGET_COUNT = JINJA_EXPRESSION_MAX_NODES };
    char source[TARGET_COUNT * 2u + sizeof("=0")];
    memset(&tree, 0, sizeof(tree));
    vstr name = {0}, rhs = {0};
    for (size_t i = 0u; i < TARGET_COUNT; ++i) {
      source[i * 2u] = 'x';
      source[i * 2u + 1u] = ',';
    }
    memcpy(source + TARGET_COUNT * 2u - 1u, "=0", sizeof("=0"));
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr(source), &name, &rhs,
                                                  &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_equal(targets.count, (size_t)0u);
    check_equal(tree.count, (size_t)0u);
  }
  it("unwraps grouped name targets while preserving the RHS source spans") {
    static const char *sources[] = {"(x) = 3", " ((x)) = 3", "(and) = 3"};
    static const char *names[] = {"x", "x", "and"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      vstr name = {0}, rhs = {0};
      JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_assignment(
          vstr_from_cstr(sources[i]), &name, &rhs, &tree, &targets, &capture, NULL);
      check_equal(status, JINJA_EXPRESSION_PARSE_OK);
      if (status != JINJA_EXPRESSION_PARSE_OK) continue;
      check_equal(name.len, strlen(names[i]));
      check_equal(memcmp(name.data, names[i], name.len), 0);
      check_true(name.data >= sources[i] && name.data < sources[i] + strlen(sources[i]));
      check_equal(rhs.len, (size_t)2u);
      check_equal(memcmp(rhs.data, " 3", rhs.len), 0);
      check_equal(tree.nodes[tree.root].kind, JINJA_EXPRESSION_CONDITION_INTEGER);
      check_equal(tree.nodes[tree.root].integer, (int64_t)3);
    }
  }

  it("rejects grouped expressions that cannot be assignment targets") {
    static const char *sources[] = {
        "(not) = 3", "(not x) = 3", "(true) = 3", "(x.y) = 3",
        "(x|safe) = 3", "(x+1) = 3", "(x if y else z) = 3",
        "(x = 3", "(x)) = 3", "(x).y = 3"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      vstr name = {0}, rhs = {0};
      check_equal(jinja_expression_parse_assignment(vstr_from_cstr(sources[i]), &name, &rhs,
                                                     &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_null(name.data);
      check_null(rhs.data);
      check_equal(tree.count, (size_t)0u);
    }
  }

  it("separates a name assignment from quoted and comparison equals") {
    static const char *sources[] = {"value = 'a=b'", "and = 1 == 1", "not = 2", "x= {'a=b': 3}"};
    static const char *names[] = {"value", "and", "not", "x"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      vstr name = {0}, rhs = {0};
      size_t offset = 0u;
      check_equal(jinja_expression_parse_assignment(vstr_from_cstr(sources[i]), &name, &rhs,
                                                     &tree, &targets, &capture, &offset), JINJA_EXPRESSION_PARSE_OK);
      check_equal(name.len, strlen(names[i]));
      check_equal(memcmp(name.data, names[i], name.len), 0);
      check_greater(tree.count, (size_t)0u);
      check_less(tree.root, tree.count);
      check_greater(rhs.len, (size_t)0u);
    }
  }

  it("rejects missing separators constants and malformed right hand sides") {
    static const char *sources[] = {"x == 1", "x =", "true = 1", "None = 1", "1 = 2", "x = (1 + )"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      vstr name = {0}, rhs = {0};
      check_equal(jinja_expression_parse_assignment(vstr_from_cstr(sources[i]), &name, &rhs,
                                                     &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_null(name.data);
      check_null(rhs.data);
    }
  }

  it("accepts exact length input without borrowing temporary parser storage") {
    static const char source[] = {'x', '=', '\'', 'a', '\''};
    memset(&tree, 0, sizeof(tree));
    vstr name = {0}, rhs = {0};
    check_equal(jinja_expression_parse_assignment(vstr_from_buf(source, sizeof(source)),
                                                   &name, &rhs, &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_true(name.data == source);
    check_true(rhs.data == source + 2u);
    check_equal(rhs.len, (size_t)3u);
  }

  it("parses namespace target owner and attribute as separate borrowed spans") {
    static const char *sources[] = {"ns.x = 1", " ns . x = 1", "ns.true = 1", "not . x = 1", "ns . not = 1"};
    static const char *owners[] = {"ns", "ns", "ns", "not", "ns"};
    static const char *attributes[] = {"x", "x", "true", "x", "not"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      vstr name = {0}, rhs = {0};
      check_equal(jinja_expression_parse_assignment(vstr_from_cstr(sources[i]), &name, &rhs,
          &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
      const JINJA_EXPRESSION_CONDITION *target = &targets.nodes[targets.root];
      check_equal(target->kind, JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET);
      check_equal(target->path.length, strlen(owners[i]));
      check_equal(memcmp(sources[i] + target->path.offset, owners[i], target->path.length), 0);
      check_equal(target->namespace_attribute.length, strlen(attributes[i]));
      check_equal(memcmp(sources[i] + target->namespace_attribute.offset, attributes[i],
                         target->namespace_attribute.length), 0);
    }
  }

  it("retains namespace targets in outer unpack and capture headers") {
    static const char source[] = "a,ns.x = 1,2";
    static const char captured[] = {'n', 's', '.', 'x'};
    memset(&tree, 0, sizeof(tree));
    vstr name = {0}, rhs = {0};
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr(source), &name, &rhs,
        &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    const JINJA_EXPRESSION_CONDITION *tuple = &targets.nodes[targets.root];
    check_equal(tuple->kind, JINJA_EXPRESSION_CONDITION_TUPLE);
    size_t second = targets.collection_items[tuple->first_collection_item].next;
    const JINJA_EXPRESSION_CONDITION *target = &targets.nodes[targets.collection_items[second].value_condition];
    check_equal(target->kind, JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET);
    check_equal(target->path.offset, (size_t)2u);
    check_equal(target->namespace_attribute.offset, (size_t)5u);
    check_equal(jinja_expression_parse_assignment(vstr_from_buf(captured, sizeof(captured)), &name, &rhs,
        &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_OK);
    check_equal(capture, 1);
    check_true(name.data == captured);
    check_equal(targets.nodes[targets.root].kind, JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET);
    check_equal(targets.nodes[targets.root].namespace_attribute.offset, (size_t)3u);
  }

  it("rejects namespace chains subscripts and nested expression targets without publishing outputs") {
    static const char *sources[] = {"ns.x.y = 1", "ns['x'] = 1", "a,(ns.x,b) = 1", "true.x = 1"};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      memset(&tree, 0, sizeof(tree));
      vstr name = {0}, rhs = {0};
      check_equal(jinja_expression_parse_assignment(vstr_from_cstr(sources[i]), &name, &rhs,
                                                     &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_INVALID);
      check_equal(tree.count, (size_t)0u);
      check_null(name.data);
      check_null(rhs.data);
    }
  }

  it("bounds grouped targets without publishing partial outputs") {
    enum { TARGET_DEPTH = 256 };
    char source[TARGET_DEPTH * 2u + sizeof("x=1")];
    memset(&tree, 0, sizeof(tree));
    vstr name = {0}, rhs = {0};
    memset(source, '(', TARGET_DEPTH);
    source[TARGET_DEPTH] = 'x';
    memset(source + TARGET_DEPTH + 1u, ')', TARGET_DEPTH);
    memcpy(source + TARGET_DEPTH * 2u + 1u, "=1", sizeof("=1"));
    check_equal(jinja_expression_parse_assignment(vstr_from_cstr(source), &name, &rhs,
                                                   &tree, &targets, &capture, NULL), JINJA_EXPRESSION_PARSE_CAPACITY);
    check_null(name.data);
    check_null(rhs.data);
    check_equal(tree.count, (size_t)0u);
  }
}
