#ifndef JINJA_CMETA_TEST_SUPPORT_H
#define JINJA_CMETA_TEST_SUPPORT_H

/* Shared test-only models and helpers; each CMeta case owns its TinyTest main. */
#include "jinja_cmeta.h"
#include "tinytest.h"

#include <cmeta/struct.h>
#include <tstr.h>
#include <vstr.h>

#include <locale.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

Struct(JinjaTestUser, (vstr, name), (int, age));

Struct(JinjaTestRoot, (JinjaTestUser, user), (bool, active), (cmeta_data_collection_view, users));

Struct(JinjaTestIntegerRoot, (int, signed_value), (size_t, unsigned_value), (bool, boolean_value));

Struct(JinjaTestMembershipRoot, (cmeta_data_collection_view, ages),
       (cmeta_data_collection_view, names), (cmeta_data_collection_view, empty));

Struct(JinjaTestFloatRoot, (double, value));

static const cmeta_type_identity jinja_test_user_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.JinjaUser");
static const cmeta_type_identity jinja_test_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.JinjaRoot");
static const cmeta_type_identity jinja_test_integer_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.JinjaIntegerRoot");
static const cmeta_type_identity jinja_test_membership_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.JinjaMembershipRoot");
static const cmeta_type_identity jinja_test_float_root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.JinjaFloatRoot");

static const cmeta_type_desc jinja_test_user_type = {"JinjaTestUser",
                                                     sizeof(JinjaTestUser),
                                                     _Alignof(JinjaTestUser),
                                                     CMETA_T_OBJECT,
                                                     NULL,
                                                     NULL,
                                                     &jinja_test_user_identity};
static const cmeta_type_desc jinja_test_root_type = {"JinjaTestRoot",
                                                     sizeof(JinjaTestRoot),
                                                     _Alignof(JinjaTestRoot),
                                                     CMETA_T_OBJECT,
                                                     NULL,
                                                     NULL,
                                                     &jinja_test_root_identity};
static const cmeta_type_desc jinja_test_integer_root_type = {"JinjaTestIntegerRoot",
                                                             sizeof(JinjaTestIntegerRoot),
                                                             _Alignof(JinjaTestIntegerRoot),
                                                             CMETA_T_OBJECT,
                                                             NULL,
                                                             NULL,
                                                             &jinja_test_integer_root_identity};
static const cmeta_type_desc jinja_test_membership_root_type = {
    "JinjaTestMembershipRoot",
    sizeof(JinjaTestMembershipRoot),
    _Alignof(JinjaTestMembershipRoot),
    CMETA_T_OBJECT,
    NULL,
    NULL,
    &jinja_test_membership_root_identity};
static const cmeta_type_desc jinja_test_float_root_type = {"JinjaTestFloatRoot",
                                                           sizeof(JinjaTestFloatRoot),
                                                           _Alignof(JinjaTestFloatRoot),
                                                           CMETA_T_OBJECT,
                                                           NULL,
                                                           NULL,
                                                           &jinja_test_float_root_identity};

typedef struct JinjaTestModel {
  cmeta_data_field_desc user_fields[2];
  cmeta_data_struct_shape user_shape;
  cmeta_data_desc user_desc;
  cmeta_data_field_desc root_fields[3];
  cmeta_data_struct_shape root_shape;
  cmeta_data_desc root_desc;
} JinjaTestModel;

typedef struct JinjaTestIntegerModel {
  cmeta_data_field_desc fields[3];
  cmeta_data_struct_shape shape;
  cmeta_data_desc desc;
} JinjaTestIntegerModel;

typedef struct JinjaTestMembershipModel {
  cmeta_data_field_desc fields[3];
  cmeta_data_struct_shape shape;
  cmeta_data_desc desc;
} JinjaTestMembershipModel;

typedef struct JinjaTestFloatModel {
  cmeta_data_field_desc fields[1];
  cmeta_data_struct_shape shape;
  cmeta_data_desc desc;
} JinjaTestFloatModel;

#define JINJA_TEST_BYTE_SINK_CAPACITY 32u

typedef struct JinjaTestByteSink {
  unsigned char bytes[JINJA_TEST_BYTE_SINK_CAPACITY];
  size_t length;
} JinjaTestByteSink;

static int jinja_test_byte_sink_write(const char *output, size_t size, void *renderer_data) {
  JinjaTestByteSink *sink = (JinjaTestByteSink *)renderer_data;
  if (sink == NULL || sink->length > sizeof(sink->bytes) ||
      size > sizeof(sink->bytes) - sink->length)
    return -1;
  if (size != 0u) memcpy(sink->bytes + sink->length, output, size);
  sink->length += size;
  return 0;
}

static void jinja_test_model_init(JinjaTestModel *model) {
  memset(model, 0, sizeof(*model));

  model->user_fields[0] = (cmeta_data_field_desc){
      "test.JinjaUser.name", "name", offsetof(JinjaTestUser, name), jinja_cmeta_vstr_data()};
  model->user_fields[1] = (cmeta_data_field_desc){"test.JinjaUser.age", "age",
                                                  offsetof(JinjaTestUser, age), &cmeta_data_int};
  model->user_shape = (cmeta_data_struct_shape){StructMeta(JinjaTestUser), model->user_fields, 2u};
  model->user_desc = (cmeta_data_desc){sizeof(cmeta_data_desc),
                                       CMETA_DATA_DESC_ABI_VERSION,
                                       "test.JinjaUser.data",
                                       "JinjaTestUser",
                                       CMETA_DATA_STRUCT,
                                       &jinja_test_user_type,
                                       &model->user_shape,
                                       NULL,
                                       NULL,
                                       NULL};

  model->root_fields[0] = (cmeta_data_field_desc){"test.JinjaRoot.user", "user",
                                                  offsetof(JinjaTestRoot, user), &model->user_desc};
  model->root_fields[1] = (cmeta_data_field_desc){
      "test.JinjaRoot.active", "active", offsetof(JinjaTestRoot, active), &cmeta_data_bool};
  model->root_fields[2] = (cmeta_data_field_desc){
      "test.JinjaRoot.users", "users", offsetof(JinjaTestRoot, users), &cmeta_data_sequence_view};
  model->root_shape = (cmeta_data_struct_shape){StructMeta(JinjaTestRoot), model->root_fields, 3u};
  model->root_desc = (cmeta_data_desc){sizeof(cmeta_data_desc),
                                       CMETA_DATA_DESC_ABI_VERSION,
                                       "test.JinjaRoot.data",
                                       "JinjaTestRoot",
                                       CMETA_DATA_STRUCT,
                                       &jinja_test_root_type,
                                       &model->root_shape,
                                       NULL,
                                       NULL,
                                       NULL};
}

static void jinja_test_integer_model_init(JinjaTestIntegerModel *model) {
  memset(model, 0, sizeof(*model));
  model->fields[0] =
      (cmeta_data_field_desc){"test.JinjaIntegerRoot.signed", "signed_value",
                              offsetof(JinjaTestIntegerRoot, signed_value), &cmeta_data_int};
  model->fields[1] =
      (cmeta_data_field_desc){"test.JinjaIntegerRoot.unsigned", "unsigned_value",
                              offsetof(JinjaTestIntegerRoot, unsigned_value), &cmeta_data_size};
  model->fields[2] =
      (cmeta_data_field_desc){"test.JinjaIntegerRoot.boolean", "boolean_value",
                              offsetof(JinjaTestIntegerRoot, boolean_value), &cmeta_data_bool};
  model->shape = (cmeta_data_struct_shape){StructMeta(JinjaTestIntegerRoot), model->fields, 3u};
  model->desc = (cmeta_data_desc){sizeof(cmeta_data_desc),
                                  CMETA_DATA_DESC_ABI_VERSION,
                                  "test.JinjaIntegerRoot.data",
                                  "JinjaTestIntegerRoot",
                                  CMETA_DATA_STRUCT,
                                  &jinja_test_integer_root_type,
                                  &model->shape,
                                  NULL,
                                  NULL,
                                  NULL};
}

static void jinja_test_membership_model_init(JinjaTestMembershipModel *model) {
  memset(model, 0, sizeof(*model));
  model->fields[0] =
      (cmeta_data_field_desc){"test.JinjaMembershipRoot.ages", "ages",
                              offsetof(JinjaTestMembershipRoot, ages), &cmeta_data_sequence_view};
  model->fields[1] = (cmeta_data_field_desc){"test.JinjaMembershipRoot.names", "names",
                                             offsetof(JinjaTestMembershipRoot, names),
                                             &cmeta_data_sequence_view};
  model->fields[2] = (cmeta_data_field_desc){"test.JinjaMembershipRoot.empty", "empty",
                                             offsetof(JinjaTestMembershipRoot, empty),
                                             &cmeta_data_sequence_view};
  model->shape = (cmeta_data_struct_shape){StructMeta(JinjaTestMembershipRoot), model->fields, 3u};
  model->desc = (cmeta_data_desc){sizeof(cmeta_data_desc),
                                  CMETA_DATA_DESC_ABI_VERSION,
                                  "test.JinjaMembershipRoot.data",
                                  "JinjaTestMembershipRoot",
                                  CMETA_DATA_STRUCT,
                                  &jinja_test_membership_root_type,
                                  &model->shape,
                                  NULL,
                                  NULL,
                                  NULL};
}

static void jinja_test_float_model_init(JinjaTestFloatModel *model) {
  memset(model, 0, sizeof(*model));
  model->fields[0] =
      (cmeta_data_field_desc){"test.JinjaFloatRoot.value", "value",
                              offsetof(JinjaTestFloatRoot, value), &cmeta_data_double};
  model->shape = (cmeta_data_struct_shape){StructMeta(JinjaTestFloatRoot), model->fields, 1u};
  model->desc = (cmeta_data_desc){sizeof(cmeta_data_desc),
                                  CMETA_DATA_DESC_ABI_VERSION,
                                  "test.JinjaFloatRoot.data",
                                  "JinjaTestFloatRoot",
                                  CMETA_DATA_STRUCT,
                                  &jinja_test_float_root_type,
                                  &model->shape,
                                  NULL,
                                  NULL,
                                  NULL};
}

static void jinja_test_compile_failure(vstr source, const JINJA_CMETA_COMPILE_OPTIONS *options,
    JINJA_CMETA_STATUS expected_status, size_t expected_offset) {
  JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
  JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(source, options, &error);
  const int compiled = templ != NULL;
  jinja_cmeta_release(templ);
  check_equal(compiled, 0);
  check_equal(error.status, expected_status);
  check_equal(error.offset, expected_offset);
}

static tstr jinja_test_named_blocks(size_t count) {
  tstr source = tstr_new();
  if (source == NULL) return NULL;
  for (size_t i = 0u; i < count; ++i) {
    tstr next = tstr_cat_fmt(source, "{%% block b%zu %%}{%% endblock %%}", i);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  return source;
}

static JINJA_CMETA_STATUS jinja_test_render(const char *source, const JinjaTestModel *model,
                                            const JinjaTestRoot *root,
                                            const JINJA_CMETA_RENDER_OPTIONS *options, char **out,
                                            JINJA_CMETA_ERROR *error) {
  JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, error);
  JINJA_CMETA_STATUS status;

  if (templ == NULL) return error->status;
  status = jinja_cmeta_render_string(templ, &model->root_desc, root, options, out, error);
  jinja_cmeta_release(templ);
  return status;
}

static tstr jinja_test_condition_chain(size_t branch_count) {
  tstr source;
  tstr next;
  size_t i;

  if (branch_count == 0u) return NULL;
  source = tstr_new();
  if (source == NULL) return NULL;
  next = tstr_cat(source, "{% if active %}x");
  if (next == NULL) goto fail;
  source = next;
  for (i = 1u; i < branch_count; ++i) {
    next = tstr_cat(source, "{% elif active %}x");
    if (next == NULL) goto fail;
    source = next;
  }
  next = tstr_cat(source, "{% endif %}");
  if (next == NULL) goto fail;
  return next;

fail:
  tstr_free(source);
  return NULL;
}

static tstr jinja_test_boolean_conditions(size_t condition_count) {
  static const char condition[] = "{% if true %}x{% endif %}";
  tstr source = tstr_new();
  size_t i;

  if (source == NULL) return NULL;
  for (i = 0u; i < condition_count; ++i) {
    tstr next = tstr_cat_len(source, condition, sizeof(condition) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  return source;
}

static tstr jinja_test_logical_operands(size_t operand_count) {
  static const char opening[] = "{{ true";
  static const char operand[] = " or true";
  static const char closing[] = " }}";
  tstr source;
  size_t i;

  if (operand_count == 0u) return NULL;
  source = tstr_new();
  if (source == NULL) return NULL;
  {
    tstr next = tstr_cat_len(source, opening, sizeof(opening) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  for (i = 1u; i < operand_count; ++i) {
    tstr next = tstr_cat_len(source, operand, sizeof(operand) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, closing, sizeof(closing) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    return next;
  }
}

static tstr jinja_test_comparison_operands(size_t operand_count) {
  static const char opening[] = "{{ 0";
  static const char operand[] = " <= 0";
  static const char closing[] = " }}";
  tstr source;
  size_t i;

  if (operand_count == 0u) return NULL;
  source = tstr_new();
  if (source == NULL) return NULL;
  {
    tstr next = tstr_cat_len(source, opening, sizeof(opening) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  for (i = 1u; i < operand_count; ++i) {
    tstr next = tstr_cat_len(source, operand, sizeof(operand) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, closing, sizeof(closing) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    return next;
  }
}

static tstr jinja_test_arithmetic_terms(size_t term_count) {
  static const char opening[] = "{{ 1";
  static const char term[] = " + 1";
  static const char closing[] = " }}";
  tstr source;
  size_t i;

  if (term_count == 0u) return NULL;
  source = tstr_new();
  if (source == NULL) return NULL;
  {
    tstr next = tstr_cat_len(source, opening, sizeof(opening) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  for (i = 1u; i < term_count; ++i) {
    tstr next = tstr_cat_len(source, term, sizeof(term) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, closing, sizeof(closing) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    return next;
  }
}

static tstr jinja_test_conditional_chain(size_t conditional_count) {
  static const char opening[] = "{{ ";
  static const char conditional[] = "1 if true else ";
  static const char closing[] = "1 }}";
  tstr source = tstr_new();
  size_t i;

  if (source == NULL) return NULL;
  {
    tstr next = tstr_cat_len(source, opening, sizeof(opening) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  for (i = 0u; i < conditional_count; ++i) {
    tstr next = tstr_cat_len(source, conditional, sizeof(conditional) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, closing, sizeof(closing) - 1u);
    if (next == NULL) {
      tstr_free(source);
      return NULL;
    }
    return next;
  }
}

static tstr jinja_test_linear_is_tests(size_t test_count) {
  static const char prefix[] = "{{ ";
  static const char suffix[] = " }}";
  static const char term[] = "(1 is integer)";
  static const char separator[] = " + ";
  tstr source = tstr_new_len(prefix, sizeof(prefix) - 1u);
  size_t i;

  if (source == NULL || test_count == 0u) goto fail;
  for (i = 0u; i < test_count; ++i) {
    tstr next;
    if (i != 0u) {
      next = tstr_cat_len(source, separator, sizeof(separator) - 1u);
      if (next == NULL) goto fail;
      source = next;
    }
    next = tstr_cat_len(source, term, sizeof(term) - 1u);
    if (next == NULL) goto fail;
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, suffix, sizeof(suffix) - 1u);
    if (next == NULL) goto fail;
    return next;
  }

fail:
  tstr_free(source);
  return NULL;
}

static tstr jinja_test_item_lookup_chain(size_t lookup_count) {
  static const char prefix[] = "{{ user";
  static const char lookup[] = "[0]";
  static const char suffix[] = " }}";
  tstr source = tstr_new_len(prefix, sizeof(prefix) - 1u);
  size_t i;

  if (source == NULL || lookup_count == 0u) goto fail;
  for (i = 0u; i < lookup_count; ++i) {
    tstr next = tstr_cat_len(source, lookup, sizeof(lookup) - 1u);
    if (next == NULL) goto fail;
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, suffix, sizeof(suffix) - 1u);
    if (next == NULL) goto fail;
    return next;
  }

fail:
  tstr_free(source);
  return NULL;
}

static tstr jinja_test_attribute_lookup_chain(size_t attribute_count) {
  static const char prefix[] = "{{ user['name']";
  static const char attribute[] = ".name";
  static const char suffix[] = " }}";
  tstr source = tstr_new_len(prefix, sizeof(prefix) - 1u);
  size_t i;

  if (source == NULL || attribute_count == 0u) goto fail;
  for (i = 0u; i < attribute_count; ++i) {
    tstr next = tstr_cat_len(source, attribute, sizeof(attribute) - 1u);
    if (next == NULL) goto fail;
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, suffix, sizeof(suffix) - 1u);
    if (next == NULL) goto fail;
    return next;
  }

fail:
  tstr_free(source);
  return NULL;
}

static tstr jinja_test_list_literal(size_t item_count) {
  static const char opening[] = "{{ [";
  static const char item[] = "1";
  static const char separator[] = ",";
  static const char closing[] = "] }}";
  tstr source = tstr_new();
  size_t i;

  if (source == NULL) return NULL;
  {
    tstr next = tstr_cat_len(source, opening, sizeof(opening) - 1u);
    if (next == NULL) goto fail;
    source = next;
  }
  for (i = 0u; i < item_count; ++i) {
    tstr next;
    if (i != 0u) {
      next = tstr_cat_len(source, separator, sizeof(separator) - 1u);
      if (next == NULL) goto fail;
      source = next;
    }
    next = tstr_cat_len(source, item, sizeof(item) - 1u);
    if (next == NULL) goto fail;
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, closing, sizeof(closing) - 1u);
    if (next == NULL) goto fail;
    return next;
  }

fail:
  tstr_free(source);
  return NULL;
}

static tstr jinja_test_dict_literal(size_t item_count) {
  static const char opening[] = "{{ {";
  static const char item[] = "1:1";
  static const char separator[] = ",";
  static const char closing[] = "} | safe }}";
  tstr source = tstr_new();
  size_t i;

  if (source == NULL) return NULL;
  {
    tstr next = tstr_cat_len(source, opening, sizeof(opening) - 1u);
    if (next == NULL) goto fail;
    source = next;
  }
  for (i = 0u; i < item_count; ++i) {
    tstr next;
    if (i != 0u) {
      next = tstr_cat_len(source, separator, sizeof(separator) - 1u);
      if (next == NULL) goto fail;
      source = next;
    }
    next = tstr_cat_len(source, item, sizeof(item) - 1u);
    if (next == NULL) goto fail;
    source = next;
  }
  {
    tstr next = tstr_cat_len(source, closing, sizeof(closing) - 1u);
    if (next == NULL) goto fail;
    return next;
  }

fail:
  tstr_free(source);
  return NULL;
}



























#endif
