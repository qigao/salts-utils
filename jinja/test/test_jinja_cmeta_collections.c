#include "jinja_cmeta_test_support.h"

#include <cstl/typed.h>

typed(Vec, JinjaCanonicalVec, int);
typed(Deque, JinjaCanonicalDeque, int);
typed(List, JinjaCanonicalList, int);
typed(Set, JinjaCanonicalSet, int);
typed(HashSet, JinjaCanonicalHashSet, int);
typed(Map, JinjaCanonicalMap, int, int);
typed(MultiMap, JinjaCanonicalMultiMap, int, int);

typedef struct JinjaCanonicalRoot {
  cmeta_data_collection_view view;
  JinjaCanonicalVec vec;
  JinjaCanonicalDeque deque;
  JinjaCanonicalList list;
  JinjaCanonicalSet set;
  JinjaCanonicalHashSet hash_set;
  JinjaCanonicalMap map;
  JinjaCanonicalMultiMap multi;
} JinjaCanonicalRoot;

static const cmeta_type_identity jinja_canonical_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.JinjaCanonicalRoot");
static const cmeta_type_desc jinja_canonical_root_type = {
    "JinjaCanonicalRoot", sizeof(JinjaCanonicalRoot), _Alignof(JinjaCanonicalRoot),
    CMETA_T_OBJECT, NULL, NULL, &jinja_canonical_root_identity};
static const cmeta_field_desc jinja_canonical_root_layout_fields[] = {
    {"view", "cmeta_data_collection_view", offsetof(JinjaCanonicalRoot, view),
     sizeof(cmeta_data_collection_view), _Alignof(cmeta_data_collection_view),
     &cmeta_type_collection_view, NULL},
    {"vec", "JinjaCanonicalVec", offsetof(JinjaCanonicalRoot, vec),
     sizeof(JinjaCanonicalVec), _Alignof(JinjaCanonicalVec), &JinjaCanonicalVec_cmeta_type, NULL},
    {"deque", "JinjaCanonicalDeque", offsetof(JinjaCanonicalRoot, deque),
     sizeof(JinjaCanonicalDeque), _Alignof(JinjaCanonicalDeque), &JinjaCanonicalDeque_cmeta_type, NULL},
    {"list", "JinjaCanonicalList", offsetof(JinjaCanonicalRoot, list),
     sizeof(JinjaCanonicalList), _Alignof(JinjaCanonicalList), &JinjaCanonicalList_cmeta_type, NULL},
    {"set", "JinjaCanonicalSet", offsetof(JinjaCanonicalRoot, set),
     sizeof(JinjaCanonicalSet), _Alignof(JinjaCanonicalSet), &JinjaCanonicalSet_cmeta_type, NULL},
    {"hash_set", "JinjaCanonicalHashSet", offsetof(JinjaCanonicalRoot, hash_set),
     sizeof(JinjaCanonicalHashSet), _Alignof(JinjaCanonicalHashSet),
     &JinjaCanonicalHashSet_cmeta_type, NULL},
    {"map", "JinjaCanonicalMap", offsetof(JinjaCanonicalRoot, map),
     sizeof(JinjaCanonicalMap), _Alignof(JinjaCanonicalMap), &JinjaCanonicalMap_cmeta_type, NULL},
    {"multi", "JinjaCanonicalMultiMap", offsetof(JinjaCanonicalRoot, multi),
     sizeof(JinjaCanonicalMultiMap), _Alignof(JinjaCanonicalMultiMap),
     &JinjaCanonicalMultiMap_cmeta_type, NULL}};
static const cmeta_struct_desc jinja_canonical_root_layout = {
    "JinjaCanonicalRoot", sizeof(JinjaCanonicalRoot), _Alignof(JinjaCanonicalRoot),
    jinja_canonical_root_layout_fields,
    sizeof(jinja_canonical_root_layout_fields) / sizeof(jinja_canonical_root_layout_fields[0])};
static const cmeta_data_field_desc jinja_canonical_root_fields[] = {
    {"test.JinjaCanonicalRoot.view", "view", offsetof(JinjaCanonicalRoot, view),
     &cmeta_data_sequence_view},
    {"test.JinjaCanonicalRoot.vec", "vec", offsetof(JinjaCanonicalRoot, vec),
     &JinjaCanonicalVec_collection_data},
    {"test.JinjaCanonicalRoot.deque", "deque", offsetof(JinjaCanonicalRoot, deque),
     &JinjaCanonicalDeque_collection_data},
    {"test.JinjaCanonicalRoot.list", "list", offsetof(JinjaCanonicalRoot, list),
     &JinjaCanonicalList_collection_data},
    {"test.JinjaCanonicalRoot.set", "set", offsetof(JinjaCanonicalRoot, set),
     &JinjaCanonicalSet_collection_data},
    {"test.JinjaCanonicalRoot.hash_set", "hash_set", offsetof(JinjaCanonicalRoot, hash_set),
     &JinjaCanonicalHashSet_collection_data},
    {"test.JinjaCanonicalRoot.map", "map", offsetof(JinjaCanonicalRoot, map),
     &JinjaCanonicalMap_map_data},
    {"test.JinjaCanonicalRoot.multi", "multi", offsetof(JinjaCanonicalRoot, multi),
     &JinjaCanonicalMultiMap_map_data}};
static const cmeta_data_struct_shape jinja_canonical_root_shape = {
    &jinja_canonical_root_layout, jinja_canonical_root_fields,
    sizeof(jinja_canonical_root_fields) / sizeof(jinja_canonical_root_fields[0])};
static const cmeta_data_desc jinja_canonical_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.JinjaCanonicalRoot.data",
    .display_name = "Jinja canonical collection root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &jinja_canonical_root_type,
    .shape = &jinja_canonical_root_shape};

spec("Jinja CMeta collections and runtime: collections 2") {
  /* TinyTest puts this spec's cases in one function; share diagnostics instead
   * of reserving hundreds of ASan-instrumented stack objects. */
  static JINJA_CMETA_ERROR error;
  before_each() { error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT; }
  it("consumes canonical contiguous CSTL collection set and map providers") {
    static const int view_values[] = {1, 2};
    static const char source[] =
        "{{view[1]}}|{{vec|length}}:{%for x in vec%}{{x}}{%endfor%}|"
        "{{deque|length}}:{%for x in deque%}{{x}}{%endfor%}|"
        "{{list|length}}:{%for x in list%}{{x}}{%endfor%}|"
        "{{set|length}}:{%for x in set%}{{x}}{%endfor%}:{{2 in set}}|"
        "{{hash_set|length}}:{{14 in hash_set}}|"
        "{{map|length}}:{{map[2]}}:{%for k in map%}{{k}}{%endfor%}:{{1 in map}}|"
        "{{multi|length}}:{{multi[1]}}:{%for k in multi%}{{k}}{%endfor%}:{{1 in multi}}|"
        "{{vec is sequence}}:{{set is sequence}}:{{set is iterable}}:"
        "{{map is mapping}}:{{map is iterable}}";
    JinjaCanonicalRoot root = {0};
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    root.view = (cmeta_data_collection_view){
        view_values, 2u, sizeof(view_values[0]), &cmeta_data_int};
    check_equal(JinjaCanonicalVec_init(&root.vec, 2u), STL_OK);
    check_equal(JinjaCanonicalVec_push(&root.vec, 3), STL_OK);
    check_equal(JinjaCanonicalVec_push(&root.vec, 4), STL_OK);
    check_equal(JinjaCanonicalDeque_init(&root.deque, 2u), STL_OK);
    check_equal(JinjaCanonicalDeque_push_back(&root.deque, 5), STL_OK);
    check_equal(JinjaCanonicalDeque_push_back(&root.deque, 6), STL_OK);
    check_equal(JinjaCanonicalList_init(&root.list, 2u), STL_OK);
    check_equal(JinjaCanonicalList_push_back(&root.list, 7), STL_OK);
    check_equal(JinjaCanonicalList_push_back(&root.list, 8), STL_OK);
    check_equal(JinjaCanonicalSet_init(&root.set, 2u), STL_OK);
    check_equal(JinjaCanonicalSet_add(&root.set, 2), STL_OK);
    check_equal(JinjaCanonicalSet_add(&root.set, 1), STL_OK);
    check_equal(JinjaCanonicalHashSet_init(&root.hash_set, 2u), STL_OK);
    check_equal(JinjaCanonicalHashSet_add(&root.hash_set, 13), STL_OK);
    check_equal(JinjaCanonicalHashSet_add(&root.hash_set, 14), STL_OK);
    check_equal(JinjaCanonicalMap_init(&root.map, 2u), STL_OK);
    check_equal(JinjaCanonicalMap_put(&root.map, 2, 20), STL_OK);
    check_equal(JinjaCanonicalMap_put(&root.map, 1, 10), STL_OK);
    check_equal(JinjaCanonicalMultiMap_init(&root.multi, 2u), STL_OK);
    check_equal(JinjaCanonicalMultiMap_put(&root.multi, 1, 10), STL_OK);
    check_equal(JinjaCanonicalMultiMap_put(&root.multi, 1, 11), STL_OK);

    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(
                    templ, &jinja_canonical_root_data, &root,
                    NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output,
                "2|2:34|2:56|2:78|2:12:True|2:True|2:20:12:True|"
                "2:11:11:True|True:False:True:True:True");

    free(output);
    jinja_cmeta_release(templ);
    JinjaCanonicalMultiMap_destroy(&root.multi);
    JinjaCanonicalMap_destroy(&root.map);
    JinjaCanonicalHashSet_destroy(&root.hash_set);
    JinjaCanonicalSet_destroy(&root.set);
    JinjaCanonicalList_destroy(&root.list);
    JinjaCanonicalDeque_destroy(&root.deque);
    JinjaCanonicalVec_destroy(&root.vec);
  }

  it("concatenates Jinja values with tilde and preserves multiplication precedence") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ user.name ~ ':' ~ user.age }}|{{ 'x' ~ 2 * 3 }}|"
                                  "{{ true ~ none ~ missing }}|{{ ('中' ~ '😀')[::-1] }}",
                                  &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "Ada:37|x6|TrueNone|😀中");
    free(output);
  }

  it("concatenates collection representations and evaluates operands once in order") {
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ ('v=' ~ ['中', none, missing]) | safe }}|"
                                  "{% for item in [1] %}{{ loop.changed(1) ~ loop.changed(1) ~ "
                                  "loop.changed(2) }}{% endfor %}",
                                  &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "v=['中', None, Undefined]|TrueFalseTrue");
    free(output);
  }

  it("shares the retained byte budget between concatenation and slicing") {
    static const char source[] = "{{ ('a' ~ 'bc')[::-1] }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_string_bytes = 5u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 6u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "cba");
    free(output);
    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ 'abc' ~ 'defg' }}", &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("slices sequences ranges and Unicode scalars with shared boundary rules") {
    static const struct {
      const char *source;
      const char *expected;
    } cases[] = {
        {"{{ [0,1,2,3,4][1:4:2] }}|{{ (0,1,2,3)[::-1] }}|{{ [0,1,2][-99:99] }}|{{ [0,1,2][2:1] }}",
         "[1, 3]|(3, 2, 1, 0)|[0, 1, 2]|[]"},
        {"{{ range(1,9,2)[::-1] }}|{{ range(1,9,2)[1:3] }}|{{ range(0)[::-1] }}|"
         "{{ range(9223372036854775807)[1:4] }}",
         "range(7, -1, -2)|range(3, 7, 2)|range(-1, -1, -1)|range(1, 4)"},
        {"{{ 'A中😀é'[::-1] }}|{{ 'A中😀é'[1:4:2] }}", "́e😀中A|中e"},
        {"{{ [0,1,2][::-1] }}|{{ [0,1,2][:-1:-1] }}|{{ [0,1,2][None:None:None] }}",
         "[2, 1, 0]|[]|[0, 1, 2]"}};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *output = NULL;
      JINJA_CMETA_STATUS status =
          jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error);
      check_equal(status, JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("slices borrowed CMeta sequences and retains nested Unicode slice values") {
    const JinjaTestUser users[] = {{vstr_from_cstr("Ada"), 37}, {vstr_from_cstr("Lin"), 42}};
    JinjaTestRoot root = {
        {vstr_from_cstr("root"), 0}, false, {users, 2u, sizeof(JinjaTestUser), NULL}};
    JinjaTestModel model;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(
        jinja_test_render("{% for item in users[::-1] %}{{ item.name }}{% endfor %}|"
                          "{{ ['A中😀'[::-1], 'xyz'[1:]][::-1] | safe }}|{{ 'A中😀'[::-1][1:] }}",
                          &model, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "LinAda|['yz', '😀中A']|中A");
    free(output);
  }

  it("rejects invalid slice operands and zero steps at render time") {
    static const char *const sources[] = {"{{ [0,1,2][::0] }}",      "{{ [0,1,2][user.name:] }}",
                                          "{{ [0,1,2][missing:] }}", "{{ range(3)[::0] }}",
                                          "{{ {'a':1}[:] }}",        "{{ missing[:] }}"};
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    size_t i;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    for (i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error),
                  JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("bounds the total retained string slice bytes for each render") {
    static const char source[] = "{{ 'abc'[::-1] }}{{ 'abc'[::-1] }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_string_bytes = 5u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 6u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "cbacba");
    free(output);
  }

  it("rejects unknown calls at execution and malformed calls at compile time") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    char *output = NULL;
    check_equal(jinja_test_render("{% for item in unknown_global(3) %}{{ item }}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_null(output);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr("{{ loop.cycle('a', 'b' }}"), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
  }

  it("charges neighbor nodes to the render workspace") {
    static const char source[] = "{% for item in [1, 2] %}{{ loop.nextitem }}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 6u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    options.max_nodes = 7u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "2");
    free(output);
  }

  it("charges direct loop metadata nodes to the render workspace") {
    static const char source[] = "{% for item in [1, 2] %}{{ loop.index }}{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    options.max_nodes = 5u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    options.max_nodes = 6u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "12");
    free(output);
  }

  it("renders nested list literals with Jinja sequence semantics") {
    static const char source[] =
        "{{ [] | safe }}|{{ [1, True, None, 'Ada'] | safe }}|{{ [1, [2, 3],] | safe }}|"
        "{% if [] %}bad{% else %}empty{% endif %}|{% if [0] %}nonempty{% endif %}|"
        "{{ [10, 20][-1] }}|<{{ [10][4] }}>|{{ 2 in [1, 2, 3] }}|"
        "{{ 4 not in [1, 2, 3] }}|{{ [1, 2] == [1, 2] }}|{{ [1] != [2] }}|"
        "{{ [] is sequence }}|{{ [1] is iterable }}|{{ [1] is mapping }}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[]|[1, True, None, 'Ada']|[1, [2, 3]]|empty|nonempty|20|<>|True|True|"
                        "True|True|True|True|False");
    free(output);
  }

  it("iterates list literal elements in the surrounding CMeta scope") {
    static const char source[] =
        "{% for item in [user.name, users[1].name, 3] %}[{{ item }}]"
        "{% else %}bad{% endfor %}|{% for item in [] %}bad{% else %}empty{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("A"), 1}, {vstr_from_cstr("Lin"), 2}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[Ada][Lin][3]|empty");
    free(output);
  }

  it("rejects malformed list literal delimiters") {
    static const char *const malformed[] = {"{{ [1 2] }}", "{{ [1, }}", "{{ [,1] }}"};
    size_t i;

    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("owns list literal source and charges bounded iteration nodes") {
    char source[] = "{% for item in ['Ada', 'Lin', 'Moe'] %}[{{ item }}]{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);

    options.max_nodes = 7u;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    options.max_nodes = 8u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "[Ada][Lin][Moe]");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("bounds list literal expression nodes") {
    tstr within_limit = jinja_test_list_literal(JINJA_CMETA_MAX_CONDITION_BRANCHES - 1u);
    tstr beyond_limit = jinja_test_list_literal(JINJA_CMETA_MAX_CONDITION_BRANCHES);
    JINJA_CMETA_TEMPLATE *templ;

    check_not_null(within_limit);
    check_not_null(beyond_limit);
    templ = jinja_cmeta_compile(vstr_from_buf(within_limit, tstr_len(within_limit)), NULL, &error);
    check_not_null(templ);
    jinja_cmeta_release(templ);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_buf(beyond_limit, tstr_len(beyond_limit)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(within_limit);
    tstr_free(beyond_limit);
  }

  it("renders indexes compares classifies and iterates tuple literals") {
    static const char source[] =
        "{{ () }}|{{ (1,) }}|{{ (1, 2) }}|"
        "{{ ((1), (2, 3)) }}|{{ (1) }}|{{ 1, 2 }}|{{ (10, 20)[-1] }}|"
        "{{ 2 in (1, 2, 3) }}|{{ (1, 2) == (1, 2) }}|{{ (1,) != [1] }}|"
        "{{ (1, 2) < (1, 3) }}|{{ () is sequence }}|{{ (1,) is iterable }}|"
        "{{ (1,) is mapping }}|{% for item in (user.name, users[1].name, 3) %}"
        "[{{ item }}]{% else %}bad{% endfor %}|"
        "{% for item in () %}bad{% else %}empty{% endfor %}|"
        "{% for item in 1, 2 %}{{ item }}{% endfor %}";
    const JinjaTestUser users[] = {{vstr_from_cstr("A"), 1}, {vstr_from_cstr("Lin"), 2}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "()|(1,)|(1, 2)|(1, (2, 3))|1|(1, 2)|20|True|True|True|True|True|True|"
                        "False|[Ada][Lin][3]|empty|12");
    free(output);
  }

  it("rejects malformed tuple literal separators") {
    static const char *const malformed[] = {"{{ (1,,2) }}", "{{ (,1) }}", "{{ (1,2 }}"};
    size_t i;

    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
    }
  }

  it("renders looks up classifies and iterates dict literals") {
    static const char values_source[] =
        "{{ {} | safe }}|{{ {'a': 1, 2: 'b', 'nested': [3]} | safe }}|"
        "{{ {'a': 1}['a'] }}|<{{ {'a': 1}['x'] }}>|"
        "{% if {} %}bad{% else %}empty{% endif %}|{{ {'a': 1} is mapping }}|"
        "{{ {'a': 1} is sequence }}|{{ {'a': 1} is iterable }}";
    static const char operations_source[] =
        "{{ 'a' in {'a': 1, 'b': 2} }}|{{ 1 not in {'a': 1} }}|"
        "{{ {'a': 1, 'b': 2} == {'b': 2, 'a': 1} }}|{{ {'a': 1} != {'a': 2} }}|"
        "{% for key in {'a': 1, 'b': 2} %}[{{ key }}]{% endfor %}|"
        "{% for key in {} %}bad{% else %}empty{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(values_source, &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "{}|{'a': 1, 2: 'b', 'nested': [3]}|1|<>|empty|True|True|True");
    free(output);

    output = NULL;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render(operations_source, &model, &root, NULL, &output, &error),
                JINJA_CMETA_OK);
    check_equal(output, "True|True|True|True|[a][b]|empty");
    free(output);
  }

  it("uses the final duplicate dict value and surrounding CMeta scope") {
    static const char source[] = "{{ {'a': 1, 'a': 2} | safe }}|{{ {'a': 1, 'a': 2}['a'] }}|"
                                 "{{ {'name': user.name, user.age: users[1].name}['name'] }}|"
                                 "{{ {'name': user.name, user.age: users[1].name}[37] }}";
    const JinjaTestUser users[] = {{vstr_from_cstr("A"), 1}, {vstr_from_cstr("Lin"), 2}};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 37}, true, {users, 2u, sizeof(users[0]), NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{'a': 2}|2|Ada|Lin");
    free(output);
  }

  it("supports tuple dict keys and preserves unique insertion order") {
    static const char source[] = "{{ {(1, 2): 'pair'}[(1, 2)] }}|{{ (1, 2) in {(1, 2): 'pair'} }}|"
                                 "{% for key in {'a': 1, 'a': 2, 'b': 3} %}[{{ key }}]{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "pair|True|[a][b]");
    free(output);
  }

  it("owns dict literal source and charges unique iteration nodes") {
    char source[] = "{% for key in {'a': 1, 'a': 2, 'b': 3} %}[{{ key }}]{% endfor %}";
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    JINJA_CMETA_TEMPLATE *templ;
    char *output = NULL;

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    templ = jinja_cmeta_compile(vstr_from_buf(source, sizeof(source) - 1u), NULL, &error);
    check_not_null(templ);
    memset(source, 'x', sizeof(source) - 1u);

    options.max_nodes = 5u;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);

    options.max_nodes = 6u;
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "[a][b]");
    free(output);
    jinja_cmeta_release(templ);
  }

  it("bounds dict literal expression nodes") {
    const size_t within_count = (JINJA_CMETA_MAX_CONDITION_BRANCHES - 1u) / 2u;
    tstr within_limit = jinja_test_dict_literal(within_count);
    tstr beyond_limit = jinja_test_dict_literal(within_count + 1u);
    JINJA_CMETA_TEMPLATE *templ;

    check_not_null(within_limit);
    check_not_null(beyond_limit);
    templ = jinja_cmeta_compile(vstr_from_buf(within_limit, tstr_len(within_limit)), NULL, &error);
    check_not_null(templ);
    jinja_cmeta_release(templ);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    templ = jinja_cmeta_compile(vstr_from_buf(beyond_limit, tstr_len(beyond_limit)), NULL, &error);
    check_null(templ);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    tstr_free(within_limit);
    tstr_free(beyond_limit);
  }

  it("rejects malformed dict syntax and unhashable collection keys") {
    static const char *const malformed[] = {"{{ {'a' 1} }}", "{{ {'a':} }}", "{{ {: 1} }}",
                                            "{{ {'a': 1,,} }}"};
    JINJA_CMETA_TEMPLATE *templ;
    JinjaTestRoot root = {{vstr_from_cstr("root"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    char *output = NULL;
    size_t i;

    for (i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
      templ = jinja_cmeta_compile(vstr_from_cstr(malformed[i]), NULL, &error);
      check_null(templ);
      check_equal(error.status, JINJA_CMETA_ERR_SYNTAX);
      error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    }

    jinja_test_model_init(&model);
    root.users.element = &model.user_desc;
    check_equal(jinja_test_render("{{ {[1]: 2} }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(jinja_test_render("{{ {([1],): 2} }}", &model, &root, NULL, &output, &error),
                JINJA_CMETA_ERR_RENDER);
    check_null(output);

    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    check_equal(
        jinja_test_render("{{ {'a': 1} < {'b': 2} }}", &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
  }
}
