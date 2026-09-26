#include "jinja_cmeta.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <cstl/typed.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typed(Vec, JinjaCMetaIntVec, int);
typed(Deque, JinjaCMetaIntDeque, int);
typed(List, JinjaCMetaIntList, int);
typed(Set, JinjaCMetaIntSet, int);
typed(HashSet, JinjaCMetaIntHashSet, int);
typed(Map, JinjaCMetaIntMap, int, int);
typed(HashMap, JinjaCMetaIntHashMap, int, int);
typed(MultiMap, JinjaCMetaIntMultiMap, int, int);
typed(BTree, JinjaCMetaIntBTree, int, int);
typed(BPlusTree, JinjaCMetaIntBPlusTree, int, int);

typedef struct JinjaCMetaCstlRoot {
  JinjaCMetaIntVec vec;
  JinjaCMetaIntDeque deque;
  JinjaCMetaIntList list;
  JinjaCMetaIntSet set;
  JinjaCMetaIntHashSet hash_set;
  JinjaCMetaIntMap map;
  JinjaCMetaIntHashMap hash_map;
  JinjaCMetaIntMultiMap multi;
  JinjaCMetaIntBTree btree;
  JinjaCMetaIntBPlusTree bplus;
} JinjaCMetaCstlRoot;

static const cmeta_type_identity jinja_cstl_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.jinja.CstlRoot");
static const cmeta_type_desc jinja_cstl_root_type = {
    .name = "JinjaCMetaCstlRoot",
    .size = sizeof(JinjaCMetaCstlRoot),
    .align = _Alignof(JinjaCMetaCstlRoot),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &jinja_cstl_root_identity
};

static const cmeta_field_desc jinja_cstl_root_layout_fields[] = {
    {
        .name = "vec",
        .type_name = "JinjaCMetaIntVec",
        .offset = offsetof(JinjaCMetaCstlRoot, vec),
        .size = sizeof(JinjaCMetaIntVec),
        .align = _Alignof(JinjaCMetaIntVec),
        .type = CMETA_TYPEOF(JinjaCMetaIntVec),
        .declared_type = NULL
    },
    {
        .name = "deque",
        .type_name = "JinjaCMetaIntDeque",
        .offset = offsetof(JinjaCMetaCstlRoot, deque),
        .size = sizeof(JinjaCMetaIntDeque),
        .align = _Alignof(JinjaCMetaIntDeque),
        .type = CMETA_TYPEOF(JinjaCMetaIntDeque),
        .declared_type = NULL
    },
    {
        .name = "list",
        .type_name = "JinjaCMetaIntList",
        .offset = offsetof(JinjaCMetaCstlRoot, list),
        .size = sizeof(JinjaCMetaIntList),
        .align = _Alignof(JinjaCMetaIntList),
        .type = CMETA_TYPEOF(JinjaCMetaIntList),
        .declared_type = NULL
    },
    {
        .name = "set",
        .type_name = "JinjaCMetaIntSet",
        .offset = offsetof(JinjaCMetaCstlRoot, set),
        .size = sizeof(JinjaCMetaIntSet),
        .align = _Alignof(JinjaCMetaIntSet),
        .type = CMETA_TYPEOF(JinjaCMetaIntSet),
        .declared_type = NULL
    },
    {
        .name = "hash_set",
        .type_name = "JinjaCMetaIntHashSet",
        .offset = offsetof(JinjaCMetaCstlRoot, hash_set),
        .size = sizeof(JinjaCMetaIntHashSet),
        .align = _Alignof(JinjaCMetaIntHashSet),
        .type = CMETA_TYPEOF(JinjaCMetaIntHashSet),
        .declared_type = NULL
    },
    {
        .name = "map",
        .type_name = "JinjaCMetaIntMap",
        .offset = offsetof(JinjaCMetaCstlRoot, map),
        .size = sizeof(JinjaCMetaIntMap),
        .align = _Alignof(JinjaCMetaIntMap),
        .type = CMETA_TYPEOF(JinjaCMetaIntMap),
        .declared_type = NULL
    },
    {
        .name = "hash_map",
        .type_name = "JinjaCMetaIntHashMap",
        .offset = offsetof(JinjaCMetaCstlRoot, hash_map),
        .size = sizeof(JinjaCMetaIntHashMap),
        .align = _Alignof(JinjaCMetaIntHashMap),
        .type = CMETA_TYPEOF(JinjaCMetaIntHashMap),
        .declared_type = NULL
    },
    {
        .name = "multi",
        .type_name = "JinjaCMetaIntMultiMap",
        .offset = offsetof(JinjaCMetaCstlRoot, multi),
        .size = sizeof(JinjaCMetaIntMultiMap),
        .align = _Alignof(JinjaCMetaIntMultiMap),
        .type = CMETA_TYPEOF(JinjaCMetaIntMultiMap),
        .declared_type = NULL
    },
    {
        .name = "btree",
        .type_name = "JinjaCMetaIntBTree",
        .offset = offsetof(JinjaCMetaCstlRoot, btree),
        .size = sizeof(JinjaCMetaIntBTree),
        .align = _Alignof(JinjaCMetaIntBTree),
        .type = CMETA_TYPEOF(JinjaCMetaIntBTree),
        .declared_type = NULL
    },
    {
        .name = "bplus",
        .type_name = "JinjaCMetaIntBPlusTree",
        .offset = offsetof(JinjaCMetaCstlRoot, bplus),
        .size = sizeof(JinjaCMetaIntBPlusTree),
        .align = _Alignof(JinjaCMetaIntBPlusTree),
        .type = CMETA_TYPEOF(JinjaCMetaIntBPlusTree),
        .declared_type = NULL
    }
};

static const cmeta_struct_desc jinja_cstl_root_layout = {
    .name = "JinjaCMetaCstlRoot",
    .size = sizeof(JinjaCMetaCstlRoot),
    .align = _Alignof(JinjaCMetaCstlRoot),
    .fields = jinja_cstl_root_layout_fields,
    .field_count = 10u
};

static const cmeta_data_field_desc jinja_cstl_root_fields[] = {
    {
        .stable_id = "test.jinja.CstlRoot.vec",
        .name = "vec",
        .offset = offsetof(JinjaCMetaCstlRoot, vec),
        .value = &JinjaCMetaIntVec_collection_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.deque",
        .name = "deque",
        .offset = offsetof(JinjaCMetaCstlRoot, deque),
        .value = &JinjaCMetaIntDeque_collection_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.list",
        .name = "list",
        .offset = offsetof(JinjaCMetaCstlRoot, list),
        .value = &JinjaCMetaIntList_collection_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.set",
        .name = "set",
        .offset = offsetof(JinjaCMetaCstlRoot, set),
        .value = &JinjaCMetaIntSet_collection_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.hash_set",
        .name = "hash_set",
        .offset = offsetof(JinjaCMetaCstlRoot, hash_set),
        .value = &JinjaCMetaIntHashSet_collection_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.map",
        .name = "map",
        .offset = offsetof(JinjaCMetaCstlRoot, map),
        .value = &JinjaCMetaIntMap_map_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.hash_map",
        .name = "hash_map",
        .offset = offsetof(JinjaCMetaCstlRoot, hash_map),
        .value = &JinjaCMetaIntHashMap_map_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.multi",
        .name = "multi",
        .offset = offsetof(JinjaCMetaCstlRoot, multi),
        .value = &JinjaCMetaIntMultiMap_map_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.btree",
        .name = "btree",
        .offset = offsetof(JinjaCMetaCstlRoot, btree),
        .value = &JinjaCMetaIntBTree_map_data
    },
    {
        .stable_id = "test.jinja.CstlRoot.bplus",
        .name = "bplus",
        .offset = offsetof(JinjaCMetaCstlRoot, bplus),
        .value = &JinjaCMetaIntBPlusTree_map_data
    }
};

static const cmeta_data_struct_shape jinja_cstl_root_shape = {
    .layout = &jinja_cstl_root_layout,
    .fields = jinja_cstl_root_fields,
    .field_count = 10u
};

static const cmeta_data_desc jinja_cstl_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.jinja.CstlRoot.data",
    .display_name = "CstlRoot",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &jinja_cstl_root_type,
    .shape = &jinja_cstl_root_shape
};

static JINJA_CMETA_STATUS render_root(
    const char *source, const JinjaCMetaCstlRoot *root, char **out) {
  JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
  JINJA_CMETA_TEMPLATE *templ =
      jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
  JINJA_CMETA_STATUS status;
  if (templ == NULL) return error.status;
  status = jinja_cmeta_render_string(
      templ, &jinja_cstl_root_data, root, NULL, out, &error);
  jinja_cmeta_release(templ);
  return status;
}

spec("Jinja consumes typed CSTL collections through canonical CMeta") {
  it("renders contiguous linked and ordered-set providers without storage knowledge") {
    JinjaCMetaCstlRoot root = {0};
    char *out = NULL;

    check_equal(JinjaCMetaIntVec_init(&root.vec, 8u), STL_OK);
    check_equal(JinjaCMetaIntDeque_init(&root.deque, 8u), STL_OK);
    check_equal(JinjaCMetaIntList_init(&root.list, 8u), STL_OK);
    check_equal(JinjaCMetaIntSet_init(&root.set, 8u), STL_OK);
    check_equal(JinjaCMetaIntHashSet_init(&root.hash_set, 8u), STL_OK);
    check_equal(JinjaCMetaIntMap_init(&root.map, 8u), STL_OK);
    check_equal(JinjaCMetaIntHashMap_init(&root.hash_map, 8u), STL_OK);
    check_equal(JinjaCMetaIntMultiMap_init(&root.multi, 8u), STL_OK);
    check_equal(JinjaCMetaIntBTree_init(&root.btree, 8u), STL_OK);
    check_equal(JinjaCMetaIntBPlusTree_init(&root.bplus, 8u), STL_OK);

    check_equal(JinjaCMetaIntVec_push(&root.vec, 3), STL_OK);
    check_equal(JinjaCMetaIntVec_push(&root.vec, 5), STL_OK);
    check_equal(JinjaCMetaIntDeque_push_back(&root.deque, 4), STL_OK);
    check_equal(JinjaCMetaIntDeque_push_back(&root.deque, 6), STL_OK);
    check_equal(JinjaCMetaIntList_push_back(&root.list, 7), STL_OK);
    check_equal(JinjaCMetaIntList_push_back(&root.list, 11), STL_OK);
    check_equal(JinjaCMetaIntSet_add(&root.set, 5), STL_OK);
    check_equal(JinjaCMetaIntSet_add(&root.set, 3), STL_OK);
    check_equal(JinjaCMetaIntHashSet_add(&root.hash_set, 5), STL_OK);
    check_equal(JinjaCMetaIntHashSet_add(&root.hash_set, 3), STL_OK);
    check_equal(JinjaCMetaIntMap_put(&root.map, 2, 20), STL_OK);
    check_equal(JinjaCMetaIntMap_put(&root.map, 1, 10), STL_OK);
    check_equal(JinjaCMetaIntHashMap_put(&root.hash_map, 7, 70), STL_OK);
    check_equal(JinjaCMetaIntHashMap_put(&root.hash_map, 9, 90), STL_OK);
    check_equal(JinjaCMetaIntMultiMap_put(&root.multi, 3, 30), STL_OK);
    check_equal(JinjaCMetaIntMultiMap_put(&root.multi, 3, 31), STL_OK);
    check_equal(JinjaCMetaIntMultiMap_put(&root.multi, 5, 50), STL_OK);
    check_equal(JinjaCMetaIntBTree_put(&root.btree, 4, 40), STL_OK);
    check_equal(JinjaCMetaIntBTree_put(&root.btree, 2, 20), STL_OK);
    check_equal(JinjaCMetaIntBPlusTree_put(&root.bplus, 8, 80), STL_OK);
    check_equal(JinjaCMetaIntBPlusTree_put(&root.bplus, 6, 60), STL_OK);

    check_equal(render_root(
                    "{{ vec|length }}:{% for x in vec %}{{x}}{% endfor %}"
                    "|{{ list[1] }}|{% for x in set %}{{x}}{% endfor %}",
                    &root, &out),
                JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2:35|11|35"), 0);
    free(out);
    out = NULL;

    check_equal(render_root("{{ map|length }}", &root, &out), JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2"), 0);
    free(out);
    out = NULL;

    check_equal(render_root("{{ map[2] }}", &root, &out), JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "20"), 0);
    free(out);
    out = NULL;

    check_equal(render_root("{% for k in map %}{{k}}{% endfor %}", &root, &out),
                JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "12"), 0);
    free(out);
    out = NULL;

    check_equal(render_root("{{ multi|length }}", &root, &out), JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2"), 0);
    free(out);
    out = NULL;

    check_equal(render_root("{{ multi[3] }}", &root, &out), JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "31"), 0);

    free(out);
    out = NULL;

    check_equal(render_root(
                    "{{ deque|length }}:{% for x in deque %}{{x}}{% endfor %}",
                    &root, &out),
                JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2:46"), 0);
    free(out);
    out = NULL;

    check_equal(render_root(
                    "{{ hash_set|length }}:{% if 3 in hash_set %}Y{% else %}N{% endif %}"
                    "{% if 9 in hash_set %}Y{% else %}N{% endif %}",
                    &root, &out),
                JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2:YN"), 0);
    free(out);
    out = NULL;

    check_equal(render_root("{{ hash_map|length }}:{{ hash_map[7] }}", &root, &out),
                JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2:70"), 0);
    free(out);
    out = NULL;

    check_equal(render_root("{{ btree|length }}:{{ btree[2] }}", &root, &out),
                JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2:20"), 0);
    free(out);
    out = NULL;

    check_equal(render_root("{{ bplus|length }}:{{ bplus[6] }}", &root, &out),
                JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2:60"), 0);

    free(out);
    JinjaCMetaIntBPlusTree_destroy(&root.bplus);
    JinjaCMetaIntBTree_destroy(&root.btree);
    JinjaCMetaIntMultiMap_destroy(&root.multi);
    JinjaCMetaIntHashMap_destroy(&root.hash_map);
    JinjaCMetaIntMap_destroy(&root.map);
    JinjaCMetaIntHashSet_destroy(&root.hash_set);
    JinjaCMetaIntSet_destroy(&root.set);
    JinjaCMetaIntList_destroy(&root.list);
    JinjaCMetaIntDeque_destroy(&root.deque);
    JinjaCMetaIntVec_destroy(&root.vec);
  }
}
