#include "jinja_cmeta.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <cstl/typed.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typed(Vec, JinjaCMetaIntVec, int);
typed(List, JinjaCMetaIntList, int);
typed(Set, JinjaCMetaIntSet, int);

typedef struct JinjaCMetaCstlRoot {
  JinjaCMetaIntVec vec;
  JinjaCMetaIntList list;
  JinjaCMetaIntSet set;
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
    }
};

static const cmeta_struct_desc jinja_cstl_root_layout = {
    .name = "JinjaCMetaCstlRoot",
    .size = sizeof(JinjaCMetaCstlRoot),
    .align = _Alignof(JinjaCMetaCstlRoot),
    .fields = jinja_cstl_root_layout_fields,
    .field_count = 3u
};

static const cmeta_data_field_desc jinja_cstl_root_fields[] = {
    {
        .stable_id = "test.jinja.CstlRoot.vec",
        .name = "vec",
        .offset = offsetof(JinjaCMetaCstlRoot, vec),
        .value = &JinjaCMetaIntVec_collection_data
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
    }
};

static const cmeta_data_struct_shape jinja_cstl_root_shape = {
    .layout = &jinja_cstl_root_layout,
    .fields = jinja_cstl_root_fields,
    .field_count = 3u
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
    check_equal(JinjaCMetaIntList_init(&root.list, 8u), STL_OK);
    check_equal(JinjaCMetaIntSet_init(&root.set, 8u), STL_OK);

    check_equal(JinjaCMetaIntVec_push(&root.vec, 3), STL_OK);
    check_equal(JinjaCMetaIntVec_push(&root.vec, 5), STL_OK);
    check_equal(JinjaCMetaIntList_push_back(&root.list, 7), STL_OK);
    check_equal(JinjaCMetaIntList_push_back(&root.list, 11), STL_OK);
    check_equal(JinjaCMetaIntSet_add(&root.set, 5), STL_OK);
    check_equal(JinjaCMetaIntSet_add(&root.set, 3), STL_OK);

    check_equal(render_root(
                    "{{ vec|length }}:{% for x in vec %}{{x}}{% endfor %}"
                    "|{{ list[1] }}|{% for x in set %}{{x}}{% endfor %}",
                    &root, &out),
                JINJA_CMETA_OK);
    check_not_null(out);
    check_equal(strcmp(out, "2:35|11|35"), 0);

    free(out);
    JinjaCMetaIntSet_destroy(&root.set);
    JinjaCMetaIntList_destroy(&root.list);
    JinjaCMetaIntVec_destroy(&root.vec);
  }
}
