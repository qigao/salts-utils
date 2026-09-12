/* Register private container elements without changing callable ABI. */
typedef struct JINJA_CMETA_INSTRUCTION JINJA_CMETA_INSTRUCTION;
typedef struct JINJA_CMETA_TRANSLATION JINJA_CMETA_TRANSLATION;
typedef struct JINJA_CMETA_TRANSLATION_BINDING JINJA_CMETA_TRANSLATION_BINDING;
typedef struct JINJA_BLOCK_NAME JINJA_BLOCK_NAME;
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST
#define CMETA_KNOWN_TYPE_LIST CMETA_BUILTIN_TYPE_LIST, \
  (JinjaInstruction, JINJA_CMETA_INSTRUCTION, jinja_instruction_type, CMETA_T_OBJECT, jinja_instruction_traits), \
  (JinjaTranslation, JINJA_CMETA_TRANSLATION, jinja_translation_type, CMETA_T_OBJECT, jinja_translation_traits), \
  (JinjaTranslationBinding, JINJA_CMETA_TRANSLATION_BINDING, jinja_translation_binding_type, CMETA_T_OBJECT, jinja_translation_binding_traits), \
  (JinjaBlockName, JINJA_BLOCK_NAME, jinja_block_name_type, CMETA_T_OBJECT, jinja_block_name_traits)

#include "jinja_cmeta_internal.h"
#include "jinja_cmeta_artifact.h"
#include "jinja_cmeta_environment.h"
#include <salts_unicode.h>
#include "jinja_expression_parser.h"
#include "jinja_expression_grammar_gen.h"
#include "jinja_template_lexer.h"
#include "jinja_template_parser.h"

#include <tstr.h>
#include <vstr.h>
#include <cstl/typed.h>
#include <cstl/sort.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum JINJA_BLOCK_KIND {
  JINJA_BLOCK_IF, JINJA_BLOCK_FOR, JINJA_BLOCK_AUTOESCAPE, JINJA_BLOCK_CAPTURE, JINJA_BLOCK_WITH,
  JINJA_BLOCK_FILTER, JINJA_BLOCK_FUNCTION, JINJA_BLOCK_NAMED
} JINJA_BLOCK_KIND;

typedef struct JINJA_BLOCK_FRAME {
  JINJA_BLOCK_KIND kind;
  vstr alias;
  size_t branch_start;
  size_t branch_count;
  int has_else;
  size_t source_offset;
  size_t opener;
  size_t pending_test;
  size_t exits;
  size_t loop_next;
  size_t function;
} JINJA_BLOCK_FRAME;

static bool jinja_instruction_copy(void *destination, const void *source) {
  if (destination == NULL || source == NULL) return false;
  memcpy(destination, source, sizeof(JINJA_CMETA_INSTRUCTION));
  return true;
}

static void jinja_instruction_move(void *destination, void *source) {
  memcpy(destination, source, sizeof(JINJA_CMETA_INSTRUCTION));
  memset(source, 0, sizeof(JINJA_CMETA_INSTRUCTION));
}

static void jinja_instruction_destroy(void *value) {
  memset(value, 0, sizeof(JINJA_CMETA_INSTRUCTION));
}

static const cmeta_type_traits jinja_instruction_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
    NULL, NULL, NULL, jinja_instruction_copy, jinja_instruction_move, jinja_instruction_destroy};
const cmeta_type_desc jinja_instruction_type = {
    "jinja.Instruction", sizeof(JINJA_CMETA_INSTRUCTION), _Alignof(JINJA_CMETA_INSTRUCTION),
    CMETA_T_OBJECT, NULL, &jinja_instruction_traits, NULL};

typed(Vec, JinjaInstructions, JINJA_CMETA_INSTRUCTION);

static bool jinja_translation_copy(void *destination, const void *source) {
  if (destination == NULL || source == NULL) return false;
  memcpy(destination, source, sizeof(JINJA_CMETA_TRANSLATION));
  return true;
}

static void jinja_translation_move(void *destination, void *source) {
  memcpy(destination, source, sizeof(JINJA_CMETA_TRANSLATION));
  memset(source, 0, sizeof(JINJA_CMETA_TRANSLATION));
}

static void jinja_translation_destroy(void *value) {
  memset(value, 0, sizeof(JINJA_CMETA_TRANSLATION));
}

static const cmeta_type_traits jinja_translation_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
    NULL, NULL, NULL, jinja_translation_copy, jinja_translation_move, jinja_translation_destroy};
const cmeta_type_desc jinja_translation_type = {
    "jinja.Translation", sizeof(JINJA_CMETA_TRANSLATION), _Alignof(JINJA_CMETA_TRANSLATION),
    CMETA_T_OBJECT, NULL, &jinja_translation_traits, NULL};

static bool jinja_translation_binding_copy(void *destination, const void *source) {
  if (destination == NULL || source == NULL) return false;
  memcpy(destination, source, sizeof(JINJA_CMETA_TRANSLATION_BINDING));
  return true;
}

static void jinja_translation_binding_move(void *destination, void *source) {
  memcpy(destination, source, sizeof(JINJA_CMETA_TRANSLATION_BINDING));
  memset(source, 0, sizeof(JINJA_CMETA_TRANSLATION_BINDING));
}

static void jinja_translation_binding_destroy(void *value) {
  memset(value, 0, sizeof(JINJA_CMETA_TRANSLATION_BINDING));
}

static const cmeta_type_traits jinja_translation_binding_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
    NULL, NULL, NULL, jinja_translation_binding_copy, jinja_translation_binding_move,
    jinja_translation_binding_destroy};
const cmeta_type_desc jinja_translation_binding_type = {
    "jinja.TranslationBinding", sizeof(JINJA_CMETA_TRANSLATION_BINDING),
    _Alignof(JINJA_CMETA_TRANSLATION_BINDING), CMETA_T_OBJECT, NULL,
    &jinja_translation_binding_traits, NULL};

typed(Vec, JinjaTranslations, JINJA_CMETA_TRANSLATION);
typed(Vec, JinjaTranslationBindings, JINJA_CMETA_TRANSLATION_BINDING);

struct JINJA_BLOCK_NAME {
  vstr name;
  size_t source_offset;
};

static int jinja_block_name_compare(const void *left, const void *right) {
  const vstr a = ((const JINJA_BLOCK_NAME *)left)->name;
  const vstr b = ((const JINJA_BLOCK_NAME *)right)->name;
  size_t common = a.len < b.len ? a.len : b.len;
  int order = memcmp(a.data, b.data, common);
  return order != 0 ? order : (a.len > b.len) - (a.len < b.len);
}

static bool jinja_block_name_copy(void *destination, const void *source) {
  if (destination == NULL || source == NULL) return false;
  memcpy(destination, source, sizeof(JINJA_BLOCK_NAME));
  return true;
}

static void jinja_block_name_move(void *destination, void *source) {
  memcpy(destination, source, sizeof(JINJA_BLOCK_NAME));
  memset(source, 0, sizeof(JINJA_BLOCK_NAME));
}

static void jinja_block_name_destroy(void *value) {
  memset(value, 0, sizeof(JINJA_BLOCK_NAME));
}

static const cmeta_type_traits jinja_block_name_traits = {
    CMETA_TRAIT_COMPARE | CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
    NULL, NULL, jinja_block_name_compare, jinja_block_name_copy, jinja_block_name_move,
    jinja_block_name_destroy};
const cmeta_type_desc jinja_block_name_type = {
    "jinja.BlockName", sizeof(JINJA_BLOCK_NAME), _Alignof(JINJA_BLOCK_NAME),
    CMETA_T_OBJECT, NULL, &jinja_block_name_traits, NULL};

typed(Vec, JinjaBlockNames, JINJA_BLOCK_NAME);

typedef struct JINJA_FUNCTION_BUILDER {
  vstr source;
  JINJA_TEMPLATE_TREE tree;
  JINJA_CMETA_FUNCTION functions[JINJA_CMETA_MAX_FUNCTIONS];
  JINJA_CMETA_PARAMETER parameters[JINJA_CMETA_MAX_PARAMETERS];
  size_t count;
  size_t parameter_count;
} JINJA_FUNCTION_BUILDER;

typedef struct JINJA_PROGRAM_BUILDER {
  JinjaInstructions instructions;
  JinjaTranslations translations;
  JinjaTranslationBindings translation_bindings;
  tstr strings;
  JINJA_FUNCTION_BUILDER *functions;
  JINJA_CMETA_STATUS status;
} JINJA_PROGRAM_BUILDER;

#define JINJA_CMETA_EXPRESSION_NAME_CAPACITY 32u
#define JINJA_ALIAS_PATH_SCOPED 2
#define JINJA_HEXADECIMAL_RADIX UINT32_C(16)
#define JINJA_HEX_ESCAPE_DIGITS 2u
#define JINJA_SHORT_UNICODE_ESCAPE_DIGITS 4u
#define JINJA_LONG_UNICODE_ESCAPE_DIGITS 8u
#define JINJA_UTF8_CONTINUATION_MASK UINT32_C(0x3f)
#define JINJA_UTF8_TWO_BYTE_PREFIX UINT32_C(0xc0)
#define JINJA_UTF8_THREE_BYTE_PREFIX UINT32_C(0xe0)
#define JINJA_UTF8_FOUR_BYTE_PREFIX UINT32_C(0xf0)
#define JINJA_UTF8_CONTINUATION_PREFIX UINT32_C(0x80)
#define JINJA_UTF8_SHIFT_ONE 6u
#define JINJA_UTF8_SHIFT_TWO 12u
#define JINJA_UTF8_SHIFT_THREE 18u

typedef struct JINJA_EXPRESSION_BUILDER {
  vstr newline_sequence;
  JINJA_CMETA_EXPRESSION_NODE nodes[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  size_t source_offsets[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  JINJA_CMETA_COMPARISON_STEP comparison_steps[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  JINJA_CMETA_COLLECTION_ITEM collection_items[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  JINJA_CMETA_DICT_ENTRY dict_entries[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  char names[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS][JINJA_CMETA_EXPRESSION_NAME_CAPACITY];
  size_t count;
  size_t comparison_step_count;
  size_t collection_item_count;
  size_t dict_entry_count;
} JINJA_EXPRESSION_BUILDER;

static int jinja_ascii_is_space(unsigned char value) {
  return value == ' ' || value == '\t' || value == '\n' || value == '\v' || value == '\f' ||
         value == '\r';
}

static int jinja_append(tstr *output, const char *data, size_t size) {
  tstr next;
  if (output == NULL || *output == NULL || (size != 0u && data == NULL)) return 0;
  if (size == 0u) return 1;
  next = tstr_cat_len(*output, data, size);
  if (next == NULL) return 0;
  *output = next;
  return 1;
}


static vstr jinja_trim(vstr value) {
  size_t first = 0u;
  size_t last = value.len;
  while (first < last && jinja_ascii_is_space((unsigned char)value.data[first]))
    ++first;
  while (last > first && jinja_ascii_is_space((unsigned char)value.data[last - 1u]))
    --last;
  return vstr_from_buf(value.data + first, last - first);
}

static int jinja_view_equal(vstr value, const char *literal) {
  size_t size = strlen(literal);
  return value.len == size && memcmp(value.data, literal, size) == 0;
}

/* Jinja 3.1.6 lowers calls containing Python hard keywords through a merged
 * dictionary. Soft keywords remain ordinary keyword arguments. */
static int jinja_call_keyword_requires_merge(vstr keyword) {
  static const char *const keywords[] = {
    "False", "None", "True", "and", "as", "assert", "async", "await", "break",
    "class", "continue", "def", "del", "elif", "else", "except", "finally",
    "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
    "not", "or", "pass", "raise", "return", "try", "while", "with", "yield"
  };
  for (size_t i = 0u; i < sizeof(keywords) / sizeof(keywords[0]); ++i)
    if (jinja_view_equal(keyword, keywords[i])) return 1;
  return 0;
}

/* Python's regex whitespace adds the four information separators to Unicode White_Space. */
static int jinja_raw_space(salts_unicode_scalar scalar) {
  enum { INFORMATION_SEPARATOR_FIRST = 0x1c, INFORMATION_SEPARATOR_LAST = 0x1f };
  return (scalar.properties & SALTS_UNICODE_PROPERTY_WHITE_SPACE) != 0u ||
      (scalar.value >= INFORMATION_SEPARATOR_FIRST && scalar.value <= INFORMATION_SEPARATOR_LAST);
}

static size_t jinja_raw_skip_space(vstr source, size_t cursor) {
  while (cursor < source.len) {
    size_t next = cursor;
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(source, &next, &scalar) != SALTS_UNICODE_OK ||
        !jinja_raw_space(scalar)) break;
    cursor = next;
  }
  return cursor;
}


static JINJA_CMETA_STATUS jinja_expression_builder_reserve(JINJA_EXPRESSION_BUILDER *builder,
                                                           JINJA_CMETA_EXPRESSION_KIND kind,
                                                           JINJA_CMETA_EXPRESSION_NODE **node,
                                                           vstr *name) {
  int written;
  size_t index;

  if (builder == NULL || node == NULL || name == NULL) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (builder->count == JINJA_CMETA_MAX_COMPILED_EXPRESSIONS) return JINJA_CMETA_ERR_CAPACITY;
  index = builder->count;
  written = snprintf(builder->names[index], sizeof(builder->names[index]), "%s%zu",
                     JINJA_CMETA_EXPRESSION_NAME_PREFIX, index);
  if (written < 0 || (size_t)written >= sizeof(builder->names[index]))
    return JINJA_CMETA_ERR_CAPACITY;
  builder->nodes[index] = (JINJA_CMETA_EXPRESSION_NODE){0};
  builder->nodes[index].kind = kind;
  builder->source_offsets[index] = 0u;
  builder->count = index + 1u;
  *node = &builder->nodes[index];
  *name = vstr_from_buf(builder->names[index], (size_t)written);
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_bool(JINJA_EXPRESSION_BUILDER *builder,
                                                               int boolean, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status =
      jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_BOOL, &node, name);
  if (status == JINJA_CMETA_OK) node->boolean = boolean != 0;
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_integer(JINJA_EXPRESSION_BUILDER *builder,
                                                                  int64_t integer, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status =
      jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_INTEGER, &node, name);
  if (status == JINJA_CMETA_OK) node->integer = integer;
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_float(JINJA_EXPRESSION_BUILDER *builder,
                                                                double floating, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status =
      jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_FLOAT, &node, name);
  if (status == JINJA_CMETA_OK) node->floating = floating;
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_string(JINJA_EXPRESSION_BUILDER *builder,
                                                                 vstr string, size_t unary_not_count,
                                                                 vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status =
      jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_STRING, &node, name);
  if (status == JINJA_CMETA_OK) {
    node->string = string;
    node->unary_not_count = unary_not_count;
    node->force_boolean = unary_not_count != 0u;
  }
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_none(JINJA_EXPRESSION_BUILDER *builder,
                                                               vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  return jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_NONE, &node, name);
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_path(JINJA_EXPRESSION_BUILDER *builder,
                                                               vstr path, size_t unary_not_count,
                                                               vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status =
      jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_PATH, &node, name);
  if (status == JINJA_CMETA_OK) {
    node->path = path;
    node->unary_not_count = unary_not_count;
  }
  return status;
}

static JINJA_CMETA_STATUS
jinja_expression_builder_append_logical(JINJA_EXPRESSION_BUILDER *builder,
                                        JINJA_CMETA_EXPRESSION_KIND kind, size_t left_node,
                                        size_t right_node, size_t unary_not_count, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status;

  if (kind != JINJA_CMETA_EXPRESSION_LOGICAL_AND && kind != JINJA_CMETA_EXPRESSION_LOGICAL_OR)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  status = jinja_expression_builder_reserve(builder, kind, &node, name);
  if (status == JINJA_CMETA_OK) {
    node->left_node = left_node;
    node->right_node = right_node;
    node->unary_not_count = unary_not_count;
  }
  return status;
}

static JINJA_CMETA_STATUS
jinja_expression_builder_append_item_lookup(JINJA_EXPRESSION_BUILDER *builder, size_t base_node,
                                            size_t key_node, size_t unary_not_count, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status =
      jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_ITEM_LOOKUP, &node, name);

  if (status == JINJA_CMETA_OK) {
    node->left_node = base_node;
    node->right_node = key_node;
    node->unary_not_count = unary_not_count;
  }
  return status;
}

static JINJA_CMETA_STATUS
jinja_expression_builder_append_attribute_lookup(JINJA_EXPRESSION_BUILDER *builder,
                                                 size_t base_node, vstr attribute,
                                                 size_t unary_not_count, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status = jinja_expression_builder_reserve(
      builder, JINJA_CMETA_EXPRESSION_ATTRIBUTE_LOOKUP, &node, name);

  if (status == JINJA_CMETA_OK) {
    node->left_node = base_node;
    node->path = attribute;
    node->unary_not_count = unary_not_count;
  }
  return status;
}

static JINJA_CMETA_STATUS
jinja_expression_builder_append_conditional(JINJA_EXPRESSION_BUILDER *builder, size_t true_node,
                                            size_t test_node, size_t false_node, int has_else,
                                            size_t unary_not_count, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status =
      jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_CONDITIONAL, &node, name);

  if (status == JINJA_CMETA_OK) {
    node->left_node = true_node;
    node->right_node = false_node;
    node->test_node = test_node;
    node->has_else = has_else != 0;
    node->unary_not_count = unary_not_count;
  }
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_list(
    JINJA_EXPRESSION_BUILDER *builder, const JINJA_EXPRESSION_TREE *tree,
    const JINJA_EXPRESSION_CONDITION *condition,
    const size_t compiled_nodes[JINJA_EXPRESSION_MAX_NODES], size_t node_position, vstr *name) {
  JINJA_CMETA_COLLECTION_ITEM items[JINJA_EXPRESSION_MAX_NODES] = {0};
  size_t item_index;
  size_t i;
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status;

  if (builder == NULL || tree == NULL || condition == NULL || compiled_nodes == NULL ||
      name == NULL ||
      (condition->kind != JINJA_EXPRESSION_CONDITION_LIST &&
       condition->kind != JINJA_EXPRESSION_CONDITION_TUPLE))
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (condition->collection_item_count > JINJA_EXPRESSION_MAX_NODES ||
      builder->collection_item_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
      condition->collection_item_count >
          JINJA_CMETA_MAX_COMPILED_EXPRESSIONS - builder->collection_item_count)
    return JINJA_CMETA_ERR_CAPACITY;
  if ((condition->collection_item_count == 0u) != (condition->first_collection_item == SIZE_MAX))
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;

  item_index = condition->first_collection_item;
  for (i = 0u; i < condition->collection_item_count; ++i) {
    const JINJA_EXPRESSION_COLLECTION_ITEM *item;
    if (item_index >= tree->collection_item_count) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    item = &tree->collection_items[item_index];
    if (item->value_condition >= node_position) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    items[i].value_node = compiled_nodes[item->value_condition];
    item_index = item->next;
  }
  if (item_index != SIZE_MAX) return JINJA_CMETA_ERR_INVALID_ARGUMENT;

  status = jinja_expression_builder_reserve(builder,
                                            condition->kind == JINJA_EXPRESSION_CONDITION_LIST
                                                ? JINJA_CMETA_EXPRESSION_LIST
                                                : JINJA_CMETA_EXPRESSION_TUPLE,
                                            &node, name);
  if (status != JINJA_CMETA_OK) return status;
  node->first_collection_item = builder->collection_item_count;
  node->collection_item_count = condition->collection_item_count;
  node->unary_not_count = condition->unary_not_count;
  if (condition->collection_item_count != 0u) {
    memcpy(builder->collection_items + builder->collection_item_count, items,
           condition->collection_item_count * sizeof(items[0]));
    builder->collection_item_count += condition->collection_item_count;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_builtin_call(
    JINJA_EXPRESSION_BUILDER *builder, const JINJA_EXPRESSION_TREE *tree,
    const JINJA_EXPRESSION_CONDITION *condition,
    const size_t compiled_nodes[JINJA_EXPRESSION_MAX_NODES], size_t node_position,
    JINJA_CMETA_EXPRESSION_KIND kind, vstr *name) {
  JINJA_CMETA_COLLECTION_ITEM arguments[JINJA_EXPRESSION_MAX_NODES] = {0};
  size_t item_index;
  size_t i;
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status;

  if (condition->collection_item_count > JINJA_EXPRESSION_MAX_NODES ||
      builder->collection_item_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
      condition->collection_item_count >
          JINJA_CMETA_MAX_COMPILED_EXPRESSIONS - builder->collection_item_count)
    return JINJA_CMETA_ERR_CAPACITY;
  if ((condition->collection_item_count == 0u) != (condition->first_collection_item == SIZE_MAX))
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;

  item_index = condition->first_collection_item;
  for (i = 0u; i < condition->collection_item_count; ++i) {
    const JINJA_EXPRESSION_COLLECTION_ITEM *item;
    if (item_index >= tree->collection_item_count) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    item = &tree->collection_items[item_index];
    if (item->value_condition >= node_position) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    arguments[i].value_node = compiled_nodes[item->value_condition];
    arguments[i].expansion = item->expansion == JINJA_EXPRESSION_EXPANSION_POSITIONAL ? JINJA_CMETA_EXPAND_POSITIONAL :
        item->expansion == JINJA_EXPRESSION_EXPANSION_KEYWORD ? JINJA_CMETA_EXPAND_KEYWORD : JINJA_CMETA_EXPAND_NONE;
    item_index = item->next;
  }
  if (item_index != SIZE_MAX) return JINJA_CMETA_ERR_INVALID_ARGUMENT;

  status = jinja_expression_builder_reserve(builder, kind, &node, name);
  if (status != JINJA_CMETA_OK) return status;
  node->first_collection_item = builder->collection_item_count;
  node->collection_item_count = condition->collection_item_count;
  node->left_node = compiled_nodes[condition->left_condition];
  node->unary_not_count = condition->unary_not_count;
  if (condition->collection_item_count != 0u) {
    memcpy(builder->collection_items + builder->collection_item_count, arguments,
           condition->collection_item_count * sizeof(arguments[0]));
    builder->collection_item_count += condition->collection_item_count;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_dict(
    JINJA_EXPRESSION_BUILDER *builder, const JINJA_EXPRESSION_TREE *tree,
    const JINJA_EXPRESSION_CONDITION *condition,
    const size_t compiled_nodes[JINJA_EXPRESSION_MAX_NODES], size_t node_position, vstr *name) {
  JINJA_CMETA_DICT_ENTRY entries[JINJA_EXPRESSION_MAX_NODES];
  size_t item_index;
  size_t i;
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status;

  if (builder == NULL || tree == NULL || condition == NULL || compiled_nodes == NULL ||
      name == NULL || condition->kind != JINJA_EXPRESSION_CONDITION_DICT)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (condition->collection_item_count > JINJA_EXPRESSION_MAX_NODES ||
      builder->dict_entry_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
      condition->collection_item_count >
          JINJA_CMETA_MAX_COMPILED_EXPRESSIONS - builder->dict_entry_count)
    return JINJA_CMETA_ERR_CAPACITY;
  if ((condition->collection_item_count == 0u) != (condition->first_collection_item == SIZE_MAX))
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;

  item_index = condition->first_collection_item;
  for (i = 0u; i < condition->collection_item_count; ++i) {
    const JINJA_EXPRESSION_DICT_ITEM *item;
    if (item_index >= tree->dict_item_count) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    item = &tree->dict_items[item_index];
    if (item->key_condition >= node_position || item->value_condition >= node_position)
      return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    entries[i].key_node = compiled_nodes[item->key_condition];
    entries[i].value_node = compiled_nodes[item->value_condition];
    item_index = item->next;
  }
  if (item_index != SIZE_MAX) return JINJA_CMETA_ERR_INVALID_ARGUMENT;

  status = jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_DICT, &node, name);
  if (status != JINJA_CMETA_OK) return status;
  node->first_collection_item = builder->dict_entry_count;
  node->collection_item_count = condition->collection_item_count;
  node->unary_not_count = condition->unary_not_count;
  if (condition->collection_item_count != 0u) {
    memcpy(builder->dict_entries + builder->dict_entry_count, entries,
           condition->collection_item_count * sizeof(entries[0]));
    builder->dict_entry_count += condition->collection_item_count;
  }
  return JINJA_CMETA_OK;
}

static int jinja_comparison_kind(JINJA_EXPRESSION_COMPARISON_KIND source,
                                 JINJA_CMETA_COMPARISON_KIND *target) {
  if (target == NULL) return 0;
  switch (source) {
  case JINJA_EXPRESSION_COMPARISON_EQUAL:
    *target = JINJA_CMETA_COMPARISON_EQUAL;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_NOT_EQUAL:
    *target = JINJA_CMETA_COMPARISON_NOT_EQUAL;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_LESS:
    *target = JINJA_CMETA_COMPARISON_LESS;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_LESS_EQUAL:
    *target = JINJA_CMETA_COMPARISON_LESS_EQUAL;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_GREATER:
    *target = JINJA_CMETA_COMPARISON_GREATER;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_GREATER_EQUAL:
    *target = JINJA_CMETA_COMPARISON_GREATER_EQUAL;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_IN:
    *target = JINJA_CMETA_COMPARISON_IN;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_NOT_IN:
    *target = JINJA_CMETA_COMPARISON_NOT_IN;
    return 1;
  }
  return 0;
}

static int jinja_arithmetic_kind(JINJA_EXPRESSION_ARITHMETIC_KIND source,
                                 JINJA_CMETA_ARITHMETIC_KIND *target) {
  if (target == NULL) return 0;
  switch (source) {
  case JINJA_EXPRESSION_ARITHMETIC_POSITIVE:
    *target = JINJA_CMETA_ARITHMETIC_POSITIVE;
    return 1;
  case JINJA_EXPRESSION_ARITHMETIC_NEGATE:
    *target = JINJA_CMETA_ARITHMETIC_NEGATE;
    return 1;
  case JINJA_EXPRESSION_ARITHMETIC_ADD:
    *target = JINJA_CMETA_ARITHMETIC_ADD;
    return 1;
  case JINJA_EXPRESSION_ARITHMETIC_SUBTRACT:
    *target = JINJA_CMETA_ARITHMETIC_SUBTRACT;
    return 1;
  case JINJA_EXPRESSION_ARITHMETIC_MULTIPLY:
    *target = JINJA_CMETA_ARITHMETIC_MULTIPLY;
    return 1;
  case JINJA_EXPRESSION_ARITHMETIC_TRUE_DIVIDE:
    *target = JINJA_CMETA_ARITHMETIC_TRUE_DIVIDE;
    return 1;
  case JINJA_EXPRESSION_ARITHMETIC_FLOOR_DIVIDE:
    *target = JINJA_CMETA_ARITHMETIC_FLOOR_DIVIDE;
    return 1;
  case JINJA_EXPRESSION_ARITHMETIC_MODULO:
    *target = JINJA_CMETA_ARITHMETIC_MODULO;
    return 1;
  case JINJA_EXPRESSION_ARITHMETIC_POWER:
    *target = JINJA_CMETA_ARITHMETIC_POWER;
    return 1;
  }
  return 0;
}

int jinja_builtin_filter_kind(vstr filter, JINJA_CMETA_EXPRESSION_KIND *kind) {
  if (kind == NULL || !vstr_is_valid(filter)) return 0;
  if (jinja_view_equal(filter, "length") || jinja_view_equal(filter, "count"))
    *kind = JINJA_CMETA_EXPRESSION_LENGTH;
  else if (jinja_view_equal(filter, "default") || jinja_view_equal(filter, "d"))
    *kind = JINJA_CMETA_EXPRESSION_DEFAULT;
  else if (jinja_view_equal(filter, "first")) *kind = JINJA_CMETA_EXPRESSION_FIRST;
  else if (jinja_view_equal(filter, "last")) *kind = JINJA_CMETA_EXPRESSION_LAST;
  else if (jinja_view_equal(filter, "abs")) *kind = JINJA_CMETA_EXPRESSION_ABS;
  else if (jinja_view_equal(filter, "int")) *kind = JINJA_CMETA_EXPRESSION_TO_INT;
  else if (jinja_view_equal(filter, "float")) *kind = JINJA_CMETA_EXPRESSION_TO_FLOAT;
  else if (jinja_view_equal(filter, "round")) *kind = JINJA_CMETA_EXPRESSION_ROUND;
  else if (jinja_view_equal(filter, "filesizeformat")) *kind = JINJA_CMETA_EXPRESSION_FILESIZEFORMAT;
  else if (jinja_view_equal(filter, "format")) *kind = JINJA_CMETA_EXPRESSION_FORMAT;
  else if (jinja_view_equal(filter, "wordcount")) *kind = JINJA_CMETA_EXPRESSION_WORDCOUNT;
  else if (jinja_view_equal(filter, "wordwrap")) *kind = JINJA_CMETA_EXPRESSION_WORDWRAP;
  else if (jinja_view_equal(filter, "upper")) *kind = JINJA_CMETA_EXPRESSION_UPPER;
  else if (jinja_view_equal(filter, "lower")) *kind = JINJA_CMETA_EXPRESSION_LOWER;
  else if (jinja_view_equal(filter, "capitalize")) *kind = JINJA_CMETA_EXPRESSION_CAPITALIZE;
  else if (jinja_view_equal(filter, "title")) *kind = JINJA_CMETA_EXPRESSION_TITLE;
  else if (jinja_view_equal(filter, "urlencode")) *kind = JINJA_CMETA_EXPRESSION_URLENCODE;
  else if (jinja_view_equal(filter, "xmlattr")) *kind = JINJA_CMETA_EXPRESSION_XMLATTR;
  else if (jinja_view_equal(filter, "string")) *kind = JINJA_CMETA_EXPRESSION_TO_STRING;
  else if (jinja_view_equal(filter, "safe")) *kind = JINJA_CMETA_EXPRESSION_SAFE;
  else if (jinja_view_equal(filter, "escape") || jinja_view_equal(filter, "e"))
    *kind = JINJA_CMETA_EXPRESSION_ESCAPE;
  else if (jinja_view_equal(filter, "forceescape")) *kind = JINJA_CMETA_EXPRESSION_FORCEESCAPE;
  else if (jinja_view_equal(filter, "list")) *kind = JINJA_CMETA_EXPRESSION_TO_LIST;
  else if (jinja_view_equal(filter, "items")) *kind = JINJA_CMETA_EXPRESSION_ITEMS;
  else if (jinja_view_equal(filter, "trim")) *kind = JINJA_CMETA_EXPRESSION_TRIM;
  else if (jinja_view_equal(filter, "center")) *kind = JINJA_CMETA_EXPRESSION_CENTER;
  else if (jinja_view_equal(filter, "indent")) *kind = JINJA_CMETA_EXPRESSION_INDENT;
  else if (jinja_view_equal(filter, "truncate")) *kind = JINJA_CMETA_EXPRESSION_TRUNCATE;
  else if (jinja_view_equal(filter, "reverse")) *kind = JINJA_CMETA_EXPRESSION_REVERSE;
  else if (jinja_view_equal(filter, "batch")) *kind = JINJA_CMETA_EXPRESSION_BATCH;
  else if (jinja_view_equal(filter, "slice")) *kind = JINJA_CMETA_EXPRESSION_SLICE_FILTER;
  else if (jinja_view_equal(filter, "sum")) *kind = JINJA_CMETA_EXPRESSION_SUM;
  else if (jinja_view_equal(filter, "min")) *kind = JINJA_CMETA_EXPRESSION_MIN;
  else if (jinja_view_equal(filter, "max")) *kind = JINJA_CMETA_EXPRESSION_MAX;
  else if (jinja_view_equal(filter, "sort")) *kind = JINJA_CMETA_EXPRESSION_SORT;
  else if (jinja_view_equal(filter, "dictsort")) *kind = JINJA_CMETA_EXPRESSION_DICTSORT;
  else if (jinja_view_equal(filter, "unique")) *kind = JINJA_CMETA_EXPRESSION_UNIQUE;
  else if (jinja_view_equal(filter, "groupby")) *kind = JINJA_CMETA_EXPRESSION_GROUPBY;
  else if (jinja_view_equal(filter, "pprint")) *kind = JINJA_CMETA_EXPRESSION_PPRINT;
  else if (jinja_view_equal(filter, "random")) *kind = JINJA_CMETA_EXPRESSION_RANDOM;
  else if (jinja_view_equal(filter, "striptags")) *kind = JINJA_CMETA_EXPRESSION_STRIPTAGS;
  else if (jinja_view_equal(filter, "urlize")) *kind = JINJA_CMETA_EXPRESSION_URLIZE;
  else if (jinja_view_equal(filter, "tojson")) *kind = JINJA_CMETA_EXPRESSION_TOJSON;
  else if (jinja_view_equal(filter, "join")) *kind = JINJA_CMETA_EXPRESSION_JOIN;
  else if (jinja_view_equal(filter, "attr")) *kind = JINJA_CMETA_EXPRESSION_ATTR;
  else if (jinja_view_equal(filter, "replace")) *kind = JINJA_CMETA_EXPRESSION_REPLACE;
  else if (jinja_view_equal(filter, "map")) *kind = JINJA_CMETA_EXPRESSION_MAP;
  else if (jinja_view_equal(filter, "select")) *kind = JINJA_CMETA_EXPRESSION_SELECT;
  else if (jinja_view_equal(filter, "reject")) *kind = JINJA_CMETA_EXPRESSION_REJECT;
  else if (jinja_view_equal(filter, "selectattr")) *kind = JINJA_CMETA_EXPRESSION_SELECTATTR;
  else if (jinja_view_equal(filter, "rejectattr")) *kind = JINJA_CMETA_EXPRESSION_REJECTATTR;
  else return 0;
  return 1;
}

static int jinja_test_kind(JINJA_EXPRESSION_TEST_KIND source, JINJA_CMETA_TEST_KIND *target) {
  if (target == NULL) return 0;
  switch (source) {
  case JINJA_EXPRESSION_TEST_DEFINED:
    *target = JINJA_CMETA_TEST_DEFINED;
    return 1;
  case JINJA_EXPRESSION_TEST_UNDEFINED:
    *target = JINJA_CMETA_TEST_UNDEFINED;
    return 1;
  case JINJA_EXPRESSION_TEST_NONE:
    *target = JINJA_CMETA_TEST_NONE;
    return 1;
  case JINJA_EXPRESSION_TEST_BOOLEAN:
    *target = JINJA_CMETA_TEST_BOOLEAN;
    return 1;
  case JINJA_EXPRESSION_TEST_TRUE:
    *target = JINJA_CMETA_TEST_TRUE;
    return 1;
  case JINJA_EXPRESSION_TEST_FALSE:
    *target = JINJA_CMETA_TEST_FALSE;
    return 1;
  case JINJA_EXPRESSION_TEST_INTEGER:
    *target = JINJA_CMETA_TEST_INTEGER;
    return 1;
  case JINJA_EXPRESSION_TEST_FLOAT:
    *target = JINJA_CMETA_TEST_FLOAT;
    return 1;
  case JINJA_EXPRESSION_TEST_NUMBER:
    *target = JINJA_CMETA_TEST_NUMBER;
    return 1;
  case JINJA_EXPRESSION_TEST_STRING:
    *target = JINJA_CMETA_TEST_STRING;
    return 1;
  case JINJA_EXPRESSION_TEST_MAPPING:
    *target = JINJA_CMETA_TEST_MAPPING;
    return 1;
  case JINJA_EXPRESSION_TEST_SEQUENCE:
    *target = JINJA_CMETA_TEST_SEQUENCE;
    return 1;
  case JINJA_EXPRESSION_TEST_ITERABLE:
    *target = JINJA_CMETA_TEST_ITERABLE;
    return 1;
  case JINJA_EXPRESSION_TEST_CALLABLE:
    *target = JINJA_CMETA_TEST_CALLABLE;
    return 1;
  case JINJA_EXPRESSION_TEST_ESCAPED:
    *target = JINJA_CMETA_TEST_ESCAPED;
    return 1;
  case JINJA_EXPRESSION_TEST_FILTER:
    *target = JINJA_CMETA_TEST_FILTER;
    return 1;
  case JINJA_EXPRESSION_TEST_TEST:
    *target = JINJA_CMETA_TEST_TEST;
    return 1;
  case JINJA_EXPRESSION_TEST_ODD:
    *target = JINJA_CMETA_TEST_ODD;
    return 1;
  case JINJA_EXPRESSION_TEST_EVEN:
    *target = JINJA_CMETA_TEST_EVEN;
    return 1;
  case JINJA_EXPRESSION_TEST_DIVISIBLEBY:
    *target = JINJA_CMETA_TEST_DIVISIBLEBY;
    return 1;
  case JINJA_EXPRESSION_TEST_SAMEAS:
    *target = JINJA_CMETA_TEST_SAMEAS;
    return 1;
  case JINJA_EXPRESSION_TEST_EQUAL:
    *target = JINJA_CMETA_TEST_EQUAL;
    return 1;
  case JINJA_EXPRESSION_TEST_NOT_EQUAL:
    *target = JINJA_CMETA_TEST_NOT_EQUAL;
    return 1;
  case JINJA_EXPRESSION_TEST_LESS:
    *target = JINJA_CMETA_TEST_LESS;
    return 1;
  case JINJA_EXPRESSION_TEST_LESS_EQUAL:
    *target = JINJA_CMETA_TEST_LESS_EQUAL;
    return 1;
  case JINJA_EXPRESSION_TEST_GREATER:
    *target = JINJA_CMETA_TEST_GREATER;
    return 1;
  case JINJA_EXPRESSION_TEST_GREATER_EQUAL:
    *target = JINJA_CMETA_TEST_GREATER_EQUAL;
    return 1;
  case JINJA_EXPRESSION_TEST_IN:
    *target = JINJA_CMETA_TEST_IN;
    return 1;
  }
  return 0;
}

int jinja_builtin_test_kind(vstr name, JINJA_CMETA_TEST_KIND *kind) {
  if (kind == NULL || !vstr_is_valid(name)) return 0;
  JINJA_EXPRESSION_TEST parsed;
  JINJA_EXPRESSION_TOKEN token = {.text = name.data, .length = name.len};
  return jinja_expression_test_from_token(token, &parsed) && parsed.supported &&
      jinja_test_kind(parsed.kind, kind);
}

static JINJA_CMETA_STATUS
jinja_expression_builder_append_arithmetic(JINJA_EXPRESSION_BUILDER *builder,
                                           JINJA_CMETA_EXPRESSION_KIND kind,
                                           JINJA_CMETA_ARITHMETIC_KIND arithmetic, size_t left_node,
                                           size_t right_node, size_t unary_not_count, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status;

  if (kind != JINJA_CMETA_EXPRESSION_UNARY_ARITHMETIC &&
      kind != JINJA_CMETA_EXPRESSION_BINARY_ARITHMETIC)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  status = jinja_expression_builder_reserve(builder, kind, &node, name);
  if (status == JINJA_CMETA_OK) {
    node->arithmetic = arithmetic;
    node->left_node = left_node;
    node->right_node = right_node;
    node->unary_not_count = unary_not_count;
  }
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_comparison(
    JINJA_EXPRESSION_BUILDER *builder, JINJA_CMETA_OPERAND left, JINJA_CMETA_OPERAND right,
    JINJA_CMETA_COMPARISON_KIND comparison, size_t unary_not_count, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status =
      jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_COMPARISON, &node, name);
  if (status == JINJA_CMETA_OK) {
    node->left = left;
    node->right = right;
    node->comparison = comparison;
    node->unary_not_count = unary_not_count;
  }
  return status;
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_nested_comparison(
    JINJA_EXPRESSION_BUILDER *builder, size_t left_node, size_t right_node,
    JINJA_CMETA_COMPARISON_KIND comparison, size_t unary_not_count, vstr *name) {
  JINJA_CMETA_EXPRESSION_NODE *node;
  JINJA_CMETA_STATUS status = jinja_expression_builder_reserve(
      builder, JINJA_CMETA_EXPRESSION_NESTED_COMPARISON, &node, name);
  if (status == JINJA_CMETA_OK) {
    node->left_node = left_node;
    node->right_node = right_node;
    node->comparison = comparison;
    node->unary_not_count = unary_not_count;
  }
  return status;
}

static int jinja_decode_simple_escape(char encoded, char *decoded) {
  if (decoded == NULL) return 0;
  switch (encoded) {
  case '\\':
    *decoded = '\\';
    return 1;
  case '\'':
    *decoded = '\'';
    return 1;
  case '"':
    *decoded = '"';
    return 1;
  case 'a':
    *decoded = '\a';
    return 1;
  case 'b':
    *decoded = '\b';
    return 1;
  case 'f':
    *decoded = '\f';
    return 1;
  case 'n':
    *decoded = '\n';
    return 1;
  case 'r':
    *decoded = '\r';
    return 1;
  case 't':
    *decoded = '\t';
    return 1;
  case 'v':
    *decoded = '\v';
    return 1;
  default:
    return 0;
  }
}

static int jinja_hex_digit(unsigned char encoded, uint32_t *digit) {
  if (digit == NULL) return 0;
  if (encoded >= '0' && encoded <= '9') {
    *digit = (uint32_t)(encoded - '0');
    return 1;
  }
  if (encoded >= 'a' && encoded <= 'f') {
    *digit = (uint32_t)(encoded - 'a') + UINT32_C(10);
    return 1;
  }
  if (encoded >= 'A' && encoded <= 'F') {
    *digit = (uint32_t)(encoded - 'A') + UINT32_C(10);
    return 1;
  }
  return 0;
}

static int jinja_parse_fixed_hex(const char *encoded, size_t digit_count, uint32_t *value) {
  uint32_t result = 0u;
  size_t index;

  if (encoded == NULL || value == NULL) return 0;
  for (index = 0u; index < digit_count; ++index) {
    uint32_t digit;
    if (!jinja_hex_digit((unsigned char)encoded[index], &digit)) return 0;
    result = result * JINJA_HEXADECIMAL_RADIX + digit;
  }
  *value = result;
  return 1;
}

static void jinja_encode_utf8_scalar(uint32_t codepoint, size_t width, char output[4]) {
  if (width == 1u) {
    output[0] = (char)codepoint;
  } else if (width == 2u) {
    output[0] = (char)(JINJA_UTF8_TWO_BYTE_PREFIX | (codepoint >> JINJA_UTF8_SHIFT_ONE));
    output[1] = (char)(JINJA_UTF8_CONTINUATION_PREFIX | (codepoint & JINJA_UTF8_CONTINUATION_MASK));
  } else if (width == 3u) {
    output[0] = (char)(JINJA_UTF8_THREE_BYTE_PREFIX | (codepoint >> JINJA_UTF8_SHIFT_TWO));
    output[1] = (char)(JINJA_UTF8_CONTINUATION_PREFIX |
                       ((codepoint >> JINJA_UTF8_SHIFT_ONE) & JINJA_UTF8_CONTINUATION_MASK));
    output[2] = (char)(JINJA_UTF8_CONTINUATION_PREFIX | (codepoint & JINJA_UTF8_CONTINUATION_MASK));
  } else {
    output[0] = (char)(JINJA_UTF8_FOUR_BYTE_PREFIX | (codepoint >> JINJA_UTF8_SHIFT_THREE));
    output[1] = (char)(JINJA_UTF8_CONTINUATION_PREFIX |
                       ((codepoint >> JINJA_UTF8_SHIFT_TWO) & JINJA_UTF8_CONTINUATION_MASK));
    output[2] = (char)(JINJA_UTF8_CONTINUATION_PREFIX |
                       ((codepoint >> JINJA_UTF8_SHIFT_ONE) & JINJA_UTF8_CONTINUATION_MASK));
    output[3] = (char)(JINJA_UTF8_CONTINUATION_PREFIX | (codepoint & JINJA_UTF8_CONTINUATION_MASK));
  }
}

static JINJA_CMETA_STATUS jinja_decode_string(vstr encoded, char *output, size_t capacity,
                                              size_t *written, vstr newline_sequence) {
  enum { OCTAL_RADIX = 8, OCTAL_MAX_DIGITS = 3, ESCAPE_PREFIX_BYTES = 2,
         ESCAPE_MAX_BYTES = ESCAPE_PREFIX_BYTES + JINJA_LONG_UNICODE_ESCAPE_DIGITS,
         HEX_RADIX = 16, LATIN1_MAX = 0xff, BMP_MAX = 0xffff, ASCII_MAX = 0x7f };
  static const char hex_digits[] = "0123456789abcdef";
  size_t input_offset;
  size_t output_offset = 0u;
  char quote = '\0';

  if (!vstr_is_valid(encoded) || written == NULL) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  for (input_offset = 0u; input_offset < encoded.len; ++input_offset) {
    char decoded[ESCAPE_MAX_BYTES] = {encoded.data[input_offset], 0};
    size_t decoded_size = 1u;
    if (quote == '\0') {
      if (decoded[0] == '\'' || decoded[0] == '"') quote = decoded[0];
      else {
        size_t next = jinja_raw_skip_space(encoded, input_offset);
        if (next == input_offset) return JINJA_CMETA_ERR_SYNTAX;
        input_offset = next - 1u;
      }
      continue;
    }
    if (decoded[0] == quote) {
      quote = '\0';
      continue;
    }
    if (decoded[0] == '\\') {
      char escape_kind;
      size_t digit_count = 0u;
      uint32_t codepoint;

      if (++input_offset == encoded.len) return JINJA_CMETA_ERR_SYNTAX;
      escape_kind = encoded.data[input_offset];
      if (jinja_decode_simple_escape(escape_kind, decoded)) {
        decoded_size = 1u;
      } else if (escape_kind == '\n' || escape_kind == '\r') {
        /* Normalization precedes unicode-escape decoding: only backslash LF
         * is a continuation; backslash CR/CRLF remains literal. */
        decoded_size = newline_sequence.data[0] == '\n' ? 0u : 1u + newline_sequence.len;
        if (decoded_size != 0u) memcpy(decoded + 1u, newline_sequence.data, newline_sequence.len);
        if (escape_kind == '\r' && input_offset + 1u < encoded.len &&
            encoded.data[input_offset + 1u] == '\n') ++input_offset;
      } else if (escape_kind >= '0' && escape_kind <= '7') {
        codepoint = (uint32_t)(escape_kind - '0');
        digit_count = 1u;
        while (digit_count < OCTAL_MAX_DIGITS && input_offset + 1u < encoded.len &&
               encoded.data[input_offset + 1u] >= '0' &&
               encoded.data[input_offset + 1u] <= '7') {
          codepoint = codepoint * OCTAL_RADIX + (uint32_t)(encoded.data[++input_offset] - '0');
          ++digit_count;
        }
        decoded_size = tstr_utf8_codepoint_size(codepoint);
        jinja_encode_utf8_scalar(codepoint, decoded_size, decoded);
      } else if (escape_kind == 'x' || escape_kind == 'u' || escape_kind == 'U') {
        digit_count = escape_kind == 'x'   ? JINJA_HEX_ESCAPE_DIGITS
                      : escape_kind == 'u' ? JINJA_SHORT_UNICODE_ESCAPE_DIGITS
                                           : JINJA_LONG_UNICODE_ESCAPE_DIGITS;
        if (digit_count > encoded.len - input_offset - 1u ||
            !jinja_parse_fixed_hex(encoded.data + input_offset + 1u, digit_count, &codepoint))
          return JINJA_CMETA_ERR_SYNTAX;
        decoded_size = tstr_utf8_codepoint_size(codepoint);
        if (decoded_size == 0u) return JINJA_CMETA_ERR_SYNTAX;
        jinja_encode_utf8_scalar(codepoint, decoded_size, decoded);
        input_offset += digit_count;
      } else if (escape_kind == 'N') {
        size_t name_start = input_offset + 1u;
        if (name_start == encoded.len || encoded.data[name_start] != '{')
          return JINJA_CMETA_ERR_SYNTAX;
        ++name_start;
        input_offset = name_start;
        while (input_offset < encoded.len && encoded.data[input_offset] != '}') ++input_offset;
        if (input_offset == encoded.len || salts_unicode_name_lookup(
            vstr_from_buf(encoded.data + name_start, input_offset - name_start), &codepoint) != SALTS_UNICODE_OK)
          return JINJA_CMETA_ERR_SYNTAX;
        decoded_size = tstr_utf8_codepoint_size(codepoint);
        if (decoded_size == 0u) return JINJA_CMETA_ERR_SYNTAX;
        jinja_encode_utf8_scalar(codepoint, decoded_size, decoded);
      } else if ((unsigned char)escape_kind > ASCII_MAX) {
        salts_unicode_scalar scalar;
        size_t next = input_offset;
        if (salts_unicode_utf8_next(encoded, &next, &scalar) != SALTS_UNICODE_OK)
          return JINJA_CMETA_ERR_SYNTAX;
        /* Jinja first ASCII-encodes with backslashreplace, then unescapes. */
        codepoint = scalar.value;
        digit_count = codepoint <= LATIN1_MAX ? JINJA_HEX_ESCAPE_DIGITS
                      : codepoint <= BMP_MAX ? JINJA_SHORT_UNICODE_ESCAPE_DIGITS
                                             : JINJA_LONG_UNICODE_ESCAPE_DIGITS;
        decoded[1] = codepoint <= LATIN1_MAX ? 'x' : codepoint <= BMP_MAX ? 'u' : 'U';
        decoded_size = ESCAPE_PREFIX_BYTES + digit_count;
        for (size_t digit = decoded_size; digit > ESCAPE_PREFIX_BYTES; --digit) {
          decoded[digit - 1u] = hex_digits[codepoint % HEX_RADIX];
          codepoint /= HEX_RADIX;
        }
        input_offset = next - 1u;
      } else {
        /* Preserve the slash; process the following UTF-8 byte on the next iteration. */
        --input_offset;
      }
    } else if (decoded[0] == '\r' || decoded[0] == '\n') {
      if (decoded[0] == '\r' && input_offset + 1u < encoded.len && encoded.data[input_offset + 1u] == '\n')
        ++input_offset;
      decoded_size = newline_sequence.len;
      memcpy(decoded, newline_sequence.data, decoded_size);
    }
    if (decoded_size > SIZE_MAX - output_offset) return JINJA_CMETA_ERR_CAPACITY;
    if (output != NULL) {
      if (output_offset > capacity || decoded_size > capacity - output_offset)
        return JINJA_CMETA_ERR_CAPACITY;
      memcpy(output + output_offset, decoded, decoded_size);
    }
    output_offset += decoded_size;
  }
  if (quote != '\0') return JINJA_CMETA_ERR_SYNTAX;
  *written = output_offset;
  return JINJA_CMETA_OK;
}

static int jinja_evaluate_string_comparison(int ordering,
                                            JINJA_EXPRESSION_COMPARISON_KIND comparison,
                                            int *result) {
  if (result == NULL) return 0;
  switch (comparison) {
  case JINJA_EXPRESSION_COMPARISON_EQUAL:
    *result = ordering == 0;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_NOT_EQUAL:
    *result = ordering != 0;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_LESS:
    *result = ordering < 0;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_LESS_EQUAL:
    *result = ordering <= 0;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_GREATER:
    *result = ordering > 0;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_GREATER_EQUAL:
    *result = ordering >= 0;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_IN:
  case JINJA_EXPRESSION_COMPARISON_NOT_IN:
    return 0;
  }
  return 0;
}

static JINJA_CMETA_STATUS jinja_fold_string_comparison(vstr expression,
                                                       const JINJA_EXPRESSION_CONDITION *condition,
                                                       int *result, vstr newline_sequence) {
  vstr left_encoded;
  vstr right_encoded;
  char *left_decoded = NULL;
  char *right_decoded = NULL;
  size_t left_length;
  size_t right_length;
  size_t common_length;
  int ordering;
  JINJA_CMETA_STATUS status;

  if (!vstr_is_valid(expression) || condition == NULL || result == NULL ||
      condition->kind != JINJA_EXPRESSION_CONDITION_STRING_COMPARISON)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (condition->string.offset > expression.len ||
      condition->string.length > expression.len - condition->string.offset ||
      condition->right_string.offset > expression.len ||
      condition->right_string.length > expression.len - condition->right_string.offset)
    return JINJA_CMETA_ERR_SYNTAX;

  left_encoded =
      vstr_from_buf(expression.data + condition->string.offset, condition->string.length);
  right_encoded = vstr_from_buf(expression.data + condition->right_string.offset,
                                condition->right_string.length);
  status = jinja_decode_string(left_encoded, NULL, 0u, &left_length, newline_sequence);
  if (status != JINJA_CMETA_OK) return status;
  status = jinja_decode_string(right_encoded, NULL, 0u, &right_length, newline_sequence);
  if (status != JINJA_CMETA_OK) return status;

  left_decoded = (char *)malloc(left_length == 0u ? 1u : left_length);
  right_decoded = (char *)malloc(right_length == 0u ? 1u : right_length);
  if (left_decoded == NULL || right_decoded == NULL) {
    status = JINJA_CMETA_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  status = jinja_decode_string(left_encoded, left_decoded, left_length, &left_length, newline_sequence);
  if (status != JINJA_CMETA_OK) goto cleanup;
  status = jinja_decode_string(right_encoded, right_decoded, right_length, &right_length, newline_sequence);
  if (status != JINJA_CMETA_OK) goto cleanup;

  common_length = left_length < right_length ? left_length : right_length;
  ordering = common_length == 0u ? 0 : memcmp(left_decoded, right_decoded, common_length);
  if (ordering == 0 && left_length != right_length) ordering = left_length < right_length ? -1 : 1;
  if (!jinja_evaluate_string_comparison(ordering, condition->comparison, result)) {
    status = JINJA_CMETA_ERR_INVALID_ARGUMENT;
    goto cleanup;
  }
  if ((condition->unary_not_count & 1u) != 0u) *result = !*result;
  status = JINJA_CMETA_OK;

cleanup:
  free(right_decoded);
  free(left_decoded);
  return status;
}

static JINJA_EXPRESSION_PARSE_STATUS
jinja_expression_parse_status_from_cmeta(JINJA_CMETA_STATUS status) {
  switch (status) {
  case JINJA_CMETA_OK:
    return JINJA_EXPRESSION_PARSE_OK;
  case JINJA_CMETA_ERR_UNSUPPORTED:
    return JINJA_EXPRESSION_PARSE_UNSUPPORTED;
  case JINJA_CMETA_ERR_OUT_OF_MEMORY:
    return JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY;
  case JINJA_CMETA_ERR_CAPACITY:
    return JINJA_EXPRESSION_PARSE_CAPACITY;
  default:
    return JINJA_EXPRESSION_PARSE_INVALID;
  }
}

static JINJA_CMETA_STATUS jinja_operand_storage_size(const JINJA_CMETA_OPERAND *operand,
                                                     size_t *size, vstr newline_sequence) {
  if (operand == NULL || size == NULL) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (operand->kind == JINJA_CMETA_OPERAND_PATH) {
    if (!vstr_is_valid(operand->text) || operand->text.len == 0u)
      return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    *size = operand->text.len;
    return JINJA_CMETA_OK;
  }
  if (operand->kind == JINJA_CMETA_OPERAND_STRING)
    return jinja_decode_string(operand->text, NULL, 0u, size, newline_sequence);
  if (operand->kind == JINJA_CMETA_OPERAND_BOOL || operand->kind == JINJA_CMETA_OPERAND_INTEGER ||
      operand->kind == JINJA_CMETA_OPERAND_FLOAT) {
    *size = 0u;
    return JINJA_CMETA_OK;
  }
  return JINJA_CMETA_ERR_INVALID_ARGUMENT;
}

static JINJA_CMETA_STATUS jinja_accumulate_operand_storage(const JINJA_CMETA_OPERAND *operand,
                                                           size_t *storage_bytes,
                                                           int *has_storage, vstr newline_sequence) {
  size_t operand_bytes;
  JINJA_CMETA_STATUS status;

  if (storage_bytes == NULL || has_storage == NULL) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  status = jinja_operand_storage_size(operand, &operand_bytes, newline_sequence);
  if (status != JINJA_CMETA_OK) return status;
  if (operand->kind != JINJA_CMETA_OPERAND_PATH && operand->kind != JINJA_CMETA_OPERAND_STRING)
    return JINJA_CMETA_OK;
  *has_storage = 1;
  if (operand_bytes > SIZE_MAX - *storage_bytes) return JINJA_CMETA_ERR_CAPACITY;
  *storage_bytes += operand_bytes;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_copy_operand_storage(JINJA_CMETA_OPERAND *operand, char *storage,
                                                     size_t capacity, size_t *offset, vstr newline_sequence) {
  size_t written;
  JINJA_CMETA_STATUS status;

  if (operand == NULL || storage == NULL || offset == NULL) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (operand->kind != JINJA_CMETA_OPERAND_PATH && operand->kind != JINJA_CMETA_OPERAND_STRING)
    return JINJA_CMETA_OK;
  if (*offset > capacity) return JINJA_CMETA_ERR_CAPACITY;
  if (operand->kind == JINJA_CMETA_OPERAND_PATH) {
    written = operand->text.len;
    if (written > capacity - *offset) return JINJA_CMETA_ERR_CAPACITY;
    if (written != 0u) memcpy(storage + *offset, operand->text.data, written);
  } else {
    status = jinja_decode_string(operand->text, storage + *offset, capacity - *offset, &written, newline_sequence);
    if (status != JINJA_CMETA_OK) return status;
  }
  operand->text = vstr_from_buf(storage + *offset, written);
  *offset += written;
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_template_copy_expressions(JINJA_CMETA_TEMPLATE *templ,
                                                          const JINJA_EXPRESSION_BUILDER *builder) {
  size_t storage_bytes = 0u;
  size_t storage_offset = 0u;
  size_t i;
  int has_storage = 0;

  if (templ == NULL || builder == NULL) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (builder->count > SIZE_MAX / sizeof(*templ->expressions)) return JINJA_CMETA_ERR_CAPACITY;
  if (builder->comparison_step_count > SIZE_MAX / sizeof(*templ->comparison_steps))
    return JINJA_CMETA_ERR_CAPACITY;
  if (builder->collection_item_count > SIZE_MAX / sizeof(*templ->collection_items))
    return JINJA_CMETA_ERR_CAPACITY;
  if (builder->dict_entry_count > SIZE_MAX / sizeof(*templ->dict_entries))
    return JINJA_CMETA_ERR_CAPACITY;
  for (i = 0u; i < builder->count; ++i) {
    const JINJA_CMETA_EXPRESSION_NODE *node = &builder->nodes[i];
    JINJA_CMETA_STATUS status;
    if (node->loop_alias.len != 0u) {
      if (!vstr_is_valid(node->loop_alias)) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      if (node->loop_alias.len > SIZE_MAX - storage_bytes) return JINJA_CMETA_ERR_CAPACITY;
      storage_bytes += node->loop_alias.len;
      has_storage = 1;
    }
    if (node->kind == JINJA_CMETA_EXPRESSION_STRING) {
      size_t decoded_length;
      has_storage = 1;
      status = jinja_decode_string(node->string, NULL, 0u, &decoded_length, builder->newline_sequence);
      if (status != JINJA_CMETA_OK) return status;
      if (decoded_length > SIZE_MAX - storage_bytes) return JINJA_CMETA_ERR_CAPACITY;
      storage_bytes += decoded_length;
    } else if (node->kind == JINJA_CMETA_EXPRESSION_PATH ||
               node->kind == JINJA_CMETA_EXPRESSION_ATTRIBUTE_LOOKUP ||
               node->kind == JINJA_CMETA_EXPRESSION_HOST_FILTER ||
               (node->kind == JINJA_CMETA_EXPRESSION_TEST &&
                node->test == JINJA_CMETA_TEST_HOST)) {
      if (!vstr_is_valid(node->path) || node->path.len == 0u)
        return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      has_storage = 1;
      if (node->path.len > SIZE_MAX - storage_bytes) return JINJA_CMETA_ERR_CAPACITY;
      storage_bytes += node->path.len;
    } else if (node->kind == JINJA_CMETA_EXPRESSION_COMPARISON) {
      status = jinja_accumulate_operand_storage(&node->left, &storage_bytes, &has_storage, builder->newline_sequence);
      if (status != JINJA_CMETA_OK) return status;
      status = jinja_accumulate_operand_storage(&node->right, &storage_bytes, &has_storage, builder->newline_sequence);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  for (i = 0u; i < builder->collection_item_count; ++i) {
    vstr keyword = builder->collection_items[i].keyword;
    if (keyword.len != 0u) {
      if (!vstr_is_valid(keyword)) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      if (keyword.len > SIZE_MAX - storage_bytes) return JINJA_CMETA_ERR_CAPACITY;
      storage_bytes += keyword.len;
      has_storage = 1;
    }
  }
  if (storage_bytes > JINJA_CMETA_MAX_TEMPLATE_BYTES) return JINJA_CMETA_ERR_CAPACITY;

  if (builder->count != 0u) {
    templ->expressions =
        (JINJA_CMETA_EXPRESSION_NODE *)jinja_cmeta_artifact_allocate(templ, builder->count, sizeof(*templ->expressions));
    if (templ->expressions == NULL) return templ->allocation_status;
    memcpy(templ->expressions, builder->nodes, builder->count * sizeof(*templ->expressions));
    templ->expression_count = builder->count;
  }
  if (builder->comparison_step_count != 0u) {
    templ->comparison_steps = (JINJA_CMETA_COMPARISON_STEP *)jinja_cmeta_artifact_allocate(templ, builder->comparison_step_count, sizeof(*templ->comparison_steps));
    if (templ->comparison_steps == NULL) return templ->allocation_status;
    memcpy(templ->comparison_steps, builder->comparison_steps,
           builder->comparison_step_count * sizeof(*templ->comparison_steps));
    templ->comparison_step_count = builder->comparison_step_count;
  }
  if (builder->collection_item_count != 0u) {
    templ->collection_items =
        (JINJA_CMETA_COLLECTION_ITEM *)jinja_cmeta_artifact_allocate(templ, builder->collection_item_count, sizeof(*templ->collection_items));
    if (templ->collection_items == NULL) return templ->allocation_status;
    memcpy(templ->collection_items, builder->collection_items,
           builder->collection_item_count * sizeof(*templ->collection_items));
    templ->collection_item_count = builder->collection_item_count;
  }
  if (builder->dict_entry_count != 0u) {
    templ->dict_entries =
        (JINJA_CMETA_DICT_ENTRY *)jinja_cmeta_artifact_allocate(templ, builder->dict_entry_count, sizeof(*templ->dict_entries));
    if (templ->dict_entries == NULL) return templ->allocation_status;
    memcpy(templ->dict_entries, builder->dict_entries,
           builder->dict_entry_count * sizeof(*templ->dict_entries));
    templ->dict_entry_count = builder->dict_entry_count;
  }
  if (!has_storage) return JINJA_CMETA_OK;
  if (storage_bytes == SIZE_MAX) return JINJA_CMETA_ERR_CAPACITY;

  templ->expression_strings = (char *)jinja_cmeta_artifact_allocate(templ, storage_bytes + 1u, sizeof(char));
  if (templ->expression_strings == NULL) return templ->allocation_status;
  for (i = 0u; i < templ->expression_count; ++i) {
    JINJA_CMETA_EXPRESSION_NODE *target = &templ->expressions[i];
    JINJA_CMETA_STATUS status;
    if (target->loop_alias.len != 0u) {
      if (target->loop_alias.len > storage_bytes - storage_offset) return JINJA_CMETA_ERR_CAPACITY;
      memcpy(templ->expression_strings + storage_offset, target->loop_alias.data,
             target->loop_alias.len);
      target->loop_alias = vstr_from_buf(templ->expression_strings + storage_offset,
                                         target->loop_alias.len);
      storage_offset += target->loop_alias.len;
    }
    if (target->kind == JINJA_CMETA_EXPRESSION_STRING) {
      vstr source = target->string;
      size_t decoded_length;
      status = jinja_decode_string(source, templ->expression_strings + storage_offset,
                                   storage_bytes - storage_offset, &decoded_length, builder->newline_sequence);
      if (status != JINJA_CMETA_OK) return status;
      target->string = vstr_from_buf(templ->expression_strings + storage_offset, decoded_length);
      storage_offset += decoded_length;
    } else if (target->kind == JINJA_CMETA_EXPRESSION_PATH ||
               target->kind == JINJA_CMETA_EXPRESSION_ATTRIBUTE_LOOKUP ||
               target->kind == JINJA_CMETA_EXPRESSION_HOST_FILTER ||
               (target->kind == JINJA_CMETA_EXPRESSION_TEST &&
                target->test == JINJA_CMETA_TEST_HOST)) {
      if (target->path.len > storage_bytes - storage_offset) return JINJA_CMETA_ERR_CAPACITY;
      memcpy(templ->expression_strings + storage_offset, target->path.data, target->path.len);
      target->path = vstr_from_buf(templ->expression_strings + storage_offset, target->path.len);
      storage_offset += target->path.len;
    } else if (target->kind == JINJA_CMETA_EXPRESSION_COMPARISON) {
      status = jinja_copy_operand_storage(&target->left, templ->expression_strings, storage_bytes,
                                          &storage_offset, builder->newline_sequence);
      if (status != JINJA_CMETA_OK) return status;
      status = jinja_copy_operand_storage(&target->right, templ->expression_strings, storage_bytes,
                                          &storage_offset, builder->newline_sequence);
      if (status != JINJA_CMETA_OK) return status;
    }
  }
  for (i = 0u; i < templ->collection_item_count; ++i) {
    vstr *keyword = &templ->collection_items[i].keyword;
    if (keyword->len == 0u) continue;
    memcpy(templ->expression_strings + storage_offset, keyword->data, keyword->len);
    *keyword = vstr_from_buf(templ->expression_strings + storage_offset, keyword->len);
    storage_offset += keyword->len;
  }
  templ->expression_strings[storage_bytes] = '\0';
  return JINJA_CMETA_OK;
}




static int jinja_lower_alias_path(vstr expression, const JINJA_BLOCK_FRAME *frames, size_t depth,
                                  vstr *lowered) {
  size_t i = depth;

  *lowered = expression;

  while (i != 0u) {
    const JINJA_BLOCK_FRAME *frame = &frames[--i];
    /* The else branch has no iteration binding; enclosing aliases remain visible. */
    if (frame->kind != JINJA_BLOCK_FOR || frame->has_else || frame->alias.len == 0u) continue;
    if (expression.len == frame->alias.len &&
        memcmp(expression.data, frame->alias.data, frame->alias.len) == 0) {
      /* Runtime bindings allow set to shadow the iteration variable. */
      return 1;
    }
    if (expression.len > frame->alias.len && expression.data[frame->alias.len] == '.' &&
        memcmp(expression.data, frame->alias.data, frame->alias.len) == 0) {
      return JINJA_ALIAS_PATH_SCOPED;
    }
  }
  return 1;
}

static JINJA_CMETA_STATUS jinja_expression_span_view(vstr expression, JINJA_EXPRESSION_SPAN span,
                                                     vstr *view) {
  if (view == NULL || !vstr_is_valid(expression)) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (span.offset > expression.len || span.length > expression.len - span.offset)
    return JINJA_CMETA_ERR_SYNTAX;
  *view = vstr_from_buf(expression.data + span.offset, span.length);
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_lower_comparison_operand(vstr expression,
                                                         const JINJA_EXPRESSION_OPERAND *source,
                                                         const JINJA_BLOCK_FRAME *frames,
                                                         size_t depth,
                                                         JINJA_CMETA_OPERAND *target) {
  JINJA_CMETA_STATUS status;

  if (source == NULL || target == NULL) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  *target = (JINJA_CMETA_OPERAND){0};
  switch (source->kind) {
  case JINJA_EXPRESSION_OPERAND_PATH:
    target->kind = JINJA_CMETA_OPERAND_PATH;
    status = jinja_expression_span_view(expression, source->span, &target->text);
    if (status != JINJA_CMETA_OK) return status;
    if (!jinja_lower_alias_path(target->text, frames, depth, &target->text))
      return JINJA_CMETA_ERR_UNSUPPORTED;
    return JINJA_CMETA_OK;
  case JINJA_EXPRESSION_OPERAND_BOOL:
    target->kind = JINJA_CMETA_OPERAND_BOOL;
    target->boolean = source->boolean != 0;
    return JINJA_CMETA_OK;
  case JINJA_EXPRESSION_OPERAND_INTEGER:
    target->kind = JINJA_CMETA_OPERAND_INTEGER;
    target->integer = source->integer;
    return JINJA_CMETA_OK;
  case JINJA_EXPRESSION_OPERAND_FLOAT:
    target->kind = JINJA_CMETA_OPERAND_FLOAT;
    target->floating = source->floating;
    return JINJA_CMETA_OK;
  case JINJA_EXPRESSION_OPERAND_STRING:
    target->kind = JINJA_CMETA_OPERAND_STRING;
    return jinja_expression_span_view(expression, source->span, &target->text);
  }
  return JINJA_CMETA_ERR_INVALID_ARGUMENT;
}

static JINJA_CMETA_STATUS
jinja_append_deferred_comparison(vstr expression, const JINJA_EXPRESSION_CONDITION *condition,
                                 const JINJA_BLOCK_FRAME *frames, size_t depth,
                                 JINJA_EXPRESSION_BUILDER *builder, vstr *name) {
  JINJA_CMETA_OPERAND left;
  JINJA_CMETA_OPERAND right;
  JINJA_CMETA_COMPARISON_KIND comparison;
  JINJA_CMETA_STATUS status;

  if (condition == NULL || condition->kind != JINJA_EXPRESSION_CONDITION_COMPARISON)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  status =
      jinja_lower_comparison_operand(expression, &condition->left_operand, frames, depth, &left);
  if (status != JINJA_CMETA_OK) return status;
  status =
      jinja_lower_comparison_operand(expression, &condition->right_operand, frames, depth, &right);
  if (status != JINJA_CMETA_OK) return status;
  if (!jinja_comparison_kind(condition->comparison, &comparison))
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  return jinja_expression_builder_append_comparison(builder, left, right, comparison,
                                                    condition->unary_not_count, name);
}

static JINJA_CMETA_STATUS jinja_expression_builder_append_comparison_chain(
    JINJA_EXPRESSION_BUILDER *builder, const JINJA_EXPRESSION_TREE *tree,
    const JINJA_EXPRESSION_CONDITION *condition, const size_t compiled_nodes[],
    size_t node_position, vstr *name) {
  JINJA_CMETA_COMPARISON_STEP steps[JINJA_EXPRESSION_MAX_NODES];
  JINJA_CMETA_EXPRESSION_NODE *node;
  size_t step_index;
  size_t i;
  JINJA_CMETA_STATUS status;

  if (builder == NULL || tree == NULL || condition == NULL || compiled_nodes == NULL ||
      name == NULL || condition->kind != JINJA_EXPRESSION_CONDITION_COMPARISON_CHAIN ||
      condition->comparison_step_count < 2u || condition->left_condition >= node_position ||
      builder->comparison_step_count > JINJA_CMETA_MAX_COMPILED_EXPRESSIONS)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;
  if (condition->comparison_step_count >
      JINJA_CMETA_MAX_COMPILED_EXPRESSIONS - builder->comparison_step_count)
    return JINJA_CMETA_ERR_CAPACITY;

  step_index = condition->first_comparison_step;
  for (i = 0u; i < condition->comparison_step_count; ++i) {
    const JINJA_EXPRESSION_COMPARISON_STEP *source;
    if (step_index >= tree->comparison_step_count) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    source = &tree->comparison_steps[step_index];
    if (source->operand_condition >= node_position ||
        !jinja_comparison_kind(source->comparison, &steps[i].comparison))
      return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    steps[i].operand_node = compiled_nodes[source->operand_condition];
    step_index = source->next;
  }
  if (step_index != SIZE_MAX) return JINJA_CMETA_ERR_INVALID_ARGUMENT;

  status = jinja_expression_builder_reserve(builder, JINJA_CMETA_EXPRESSION_COMPARISON_CHAIN, &node,
                                            name);
  if (status != JINJA_CMETA_OK) return status;
  node->left_node = compiled_nodes[condition->left_condition];
  node->first_comparison_step = builder->comparison_step_count;
  node->comparison_step_count = condition->comparison_step_count;
  node->unary_not_count = condition->unary_not_count;
  memcpy(builder->comparison_steps + builder->comparison_step_count, steps,
         condition->comparison_step_count * sizeof(steps[0]));
  builder->comparison_step_count += condition->comparison_step_count;
  return JINJA_CMETA_OK;
}

/* O(n^2) duplicate checks over the fixed, at-most-64-node argument table. */
static JINJA_CMETA_STATUS jinja_validate_argument_keywords(vstr expression,
    const JINJA_EXPRESSION_TREE *tree, const JINJA_EXPRESSION_CONDITION *condition) {
  size_t item_index = condition->first_collection_item;
  int saw_keyword = 0;
  for (size_t i = 0u; i < condition->collection_item_count; ++i) {
    const JINJA_EXPRESSION_COLLECTION_ITEM *item;
    vstr keyword;
    JINJA_CMETA_STATUS status;
    if (item_index >= tree->collection_item_count) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    item = &tree->collection_items[item_index];
    if (item->expansion != JINJA_EXPRESSION_EXPANSION_NONE) {
      item_index = item->next;
      continue;
    }
    if (item->keyword.length == 0u) {
      if (saw_keyword) return JINJA_CMETA_ERR_SYNTAX;
    } else {
      size_t earlier = condition->first_collection_item;
      status = jinja_expression_span_view(expression, item->keyword, &keyword);
      if (status != JINJA_CMETA_OK) return status;
      for (size_t j = 0u; j < i; ++j) {
        const JINJA_EXPRESSION_COLLECTION_ITEM *previous = &tree->collection_items[earlier];
        vstr previous_name;
        if (previous->keyword.length != 0u) {
          status = jinja_expression_span_view(expression, previous->keyword, &previous_name);
          if (status != JINJA_CMETA_OK) return status;
          if (keyword.len == previous_name.len && memcmp(keyword.data, previous_name.data, keyword.len) == 0)
            return JINJA_CMETA_ERR_SYNTAX;
        }
        earlier = previous->next;
      }
      saw_keyword = 1;
    }
    item_index = item->next;
  }
  return item_index == SIZE_MAX ? JINJA_CMETA_OK : JINJA_CMETA_ERR_INVALID_ARGUMENT;
}

static JINJA_CMETA_STATUS
jinja_compile_expression_tree(vstr expression, const JINJA_EXPRESSION_TREE *tree,
                              const JINJA_BLOCK_FRAME *frames, size_t depth, int force_boolean,
                              JINJA_EXPRESSION_BUILDER *builder, vstr *name) {
  size_t compiled_nodes[JINJA_EXPRESSION_MAX_NODES];
  size_t i;

  if (tree == NULL || builder == NULL || name == NULL || tree->count == 0u ||
      tree->count > JINJA_EXPRESSION_MAX_NODES ||
      tree->comparison_step_count > JINJA_EXPRESSION_MAX_NODES ||
      tree->collection_item_count > JINJA_EXPRESSION_MAX_NODES ||
      tree->dict_item_count > JINJA_EXPRESSION_MAX_NODES || tree->root != tree->count - 1u)
    return JINJA_CMETA_ERR_INVALID_ARGUMENT;

  for (i = 0u; i < tree->count; ++i) {
    const JINJA_EXPRESSION_CONDITION *condition = &tree->nodes[i];
    JINJA_CMETA_STATUS status;
    vstr node_name = {0};

    switch (condition->kind) {
    case JINJA_EXPRESSION_CONDITION_CAPTURE:
      status = jinja_expression_builder_append_none(builder, &node_name);
      if (status == JINJA_CMETA_OK)
        builder->nodes[builder->count - 1u].kind = JINJA_CMETA_EXPRESSION_CAPTURE;
      break;
    case JINJA_EXPRESSION_CONDITION_PATH: {
      vstr path;
      status = jinja_expression_span_view(expression, condition->path, &path);
      if (status == JINJA_CMETA_OK && !jinja_lower_alias_path(path, frames, depth, &path))
        status = JINJA_CMETA_ERR_UNSUPPORTED;
      if (status == JINJA_CMETA_OK)
        status = jinja_expression_builder_append_path(builder, path, condition->unary_not_count,
                                                      &node_name);
      break;
    }
    case JINJA_EXPRESSION_CONDITION_BOOL:
      status = jinja_expression_builder_append_bool(builder, condition->boolean, &node_name);
      break;
    case JINJA_EXPRESSION_CONDITION_INTEGER:
      status = jinja_expression_builder_append_integer(builder, condition->integer, &node_name);
      break;
    case JINJA_EXPRESSION_CONDITION_FLOAT:
      status = jinja_expression_builder_append_float(builder, condition->floating, &node_name);
      break;
    case JINJA_EXPRESSION_CONDITION_STRING: {
      vstr string;
      status = jinja_expression_span_view(expression, condition->string, &string);
      if (status == JINJA_CMETA_OK)
        status = jinja_expression_builder_append_string(builder, string, condition->unary_not_count, &node_name);
      break;
    }
    case JINJA_EXPRESSION_CONDITION_NONE:
      status = jinja_expression_builder_append_none(builder, &node_name);
      break;
    case JINJA_EXPRESSION_CONDITION_SLICE:
      return JINJA_CMETA_ERR_UNSUPPORTED;
    case JINJA_EXPRESSION_CONDITION_SLICE_LOOKUP:
    case JINJA_EXPRESSION_CONDITION_CONCAT:
    case JINJA_EXPRESSION_CONDITION_ITEM_LOOKUP:
      if (condition->left_condition >= i || condition->right_condition >= i)
        return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      status = jinja_expression_builder_append_item_lookup(
          builder, compiled_nodes[condition->left_condition],
          compiled_nodes[condition->right_condition], condition->unary_not_count, &node_name);
      if (status == JINJA_CMETA_OK && condition->kind == JINJA_EXPRESSION_CONDITION_SLICE_LOOKUP)
        builder->nodes[builder->count - 1u].kind = JINJA_CMETA_EXPRESSION_SLICE_LOOKUP;
      if (status == JINJA_CMETA_OK && condition->kind == JINJA_EXPRESSION_CONDITION_CONCAT)
        builder->nodes[builder->count - 1u].kind = JINJA_CMETA_EXPRESSION_CONCAT;
      break;
    case JINJA_EXPRESSION_CONDITION_FILTER: {
      vstr filter;
      JINJA_CMETA_EXPRESSION_KIND kind;
      if (condition->left_condition >= i) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      status = jinja_expression_span_view(expression, condition->path, &filter);
      if (status != JINJA_CMETA_OK) return status;
      if (!jinja_builtin_filter_kind(filter, &kind)) kind = JINJA_CMETA_EXPRESSION_HOST_FILTER;
      status = jinja_expression_builder_append_builtin_call(builder, tree, condition,
                                                            compiled_nodes, i, kind, &node_name);
      if (status == JINJA_CMETA_OK) {
        JINJA_CMETA_EXPRESSION_NODE *filter_node = &builder->nodes[builder->count - 1u];
        size_t item_index = condition->first_collection_item;
        size_t argument;
        if (kind == JINJA_CMETA_EXPRESSION_HOST_FILTER) filter_node->path = filter;
        status = jinja_validate_argument_keywords(expression, tree, condition);
        if (status != JINJA_CMETA_OK) return status;
        for (argument = 0u; argument < condition->collection_item_count; ++argument) {
          const JINJA_EXPRESSION_COLLECTION_ITEM *item = &tree->collection_items[item_index];
          if (item->keyword.length != 0u) {
            vstr keyword;
            status = jinja_expression_span_view(expression, item->keyword, &keyword);
            if (status != JINJA_CMETA_OK) return status;
            builder->collection_items[filter_node->first_collection_item + argument].keyword = keyword;
            filter_node->merge_expanded_keywords |= jinja_call_keyword_requires_merge(keyword);
          }
          item_index = item->next;
        }
      }
      break;
    }
    case JINJA_EXPRESSION_CONDITION_ATTRIBUTE_LOOKUP: {
      vstr attribute;
      if (condition->left_condition >= i) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      status = jinja_expression_span_view(expression, condition->path, &attribute);
      if (status == JINJA_CMETA_OK)
        status = jinja_expression_builder_append_attribute_lookup(
            builder, compiled_nodes[condition->left_condition], attribute,
            condition->unary_not_count, &node_name);
      break;
    }
    case JINJA_EXPRESSION_CONDITION_CALL: {
      JINJA_CMETA_EXPRESSION_KIND kind = JINJA_CMETA_EXPRESSION_CALL;
      status = jinja_validate_argument_keywords(expression, tree, condition);
      if (status != JINJA_CMETA_OK) break;
      if (condition->left_condition >= i) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      const JINJA_EXPRESSION_CONDITION *target = &tree->nodes[condition->left_condition];
      if (target->kind == JINJA_EXPRESSION_CONDITION_PATH && target->unary_not_count == 0u) {
        vstr path;
        status = jinja_expression_span_view(expression, target->path, &path);
        if (status != JINJA_CMETA_OK) break;
        if (jinja_view_equal(path, "loop.cycle")) kind = JINJA_CMETA_EXPRESSION_LOOP_CYCLE;
        else if (jinja_view_equal(path, "loop.changed")) kind = JINJA_CMETA_EXPRESSION_LOOP_CHANGED;
        else if (jinja_view_equal(path, "loop")) kind = JINJA_CMETA_EXPRESSION_LOOP_RECURSE;
      }
      status = jinja_expression_builder_append_builtin_call(builder, tree, condition,
                                                            compiled_nodes, i, kind, &node_name);
      if (status == JINJA_CMETA_OK) {
        size_t item = condition->first_collection_item;
        JINJA_CMETA_EXPRESSION_NODE *call = &builder->nodes[builder->count - 1u];
        for (size_t j = 0u; j < condition->collection_item_count; ++j) {
          if (tree->collection_items[item].keyword.length != 0u) {
            status = jinja_expression_span_view(expression, tree->collection_items[item].keyword,
                &builder->collection_items[call->first_collection_item + j].keyword);
            if (status != JINJA_CMETA_OK) break;
            call->merge_expanded_keywords |= jinja_call_keyword_requires_merge(
                builder->collection_items[call->first_collection_item + j].keyword);
          }
          item = tree->collection_items[item].next;
        }
      }
      break;
    }
    case JINJA_EXPRESSION_CONDITION_TEST: {
      JINJA_CMETA_TEST_KIND test;
      vstr test_name = vstr_from_buf(NULL, 0u);
      if (condition->left_condition >= i) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      if (condition->test_supported) {
        if (!jinja_test_kind(condition->test, &test)) return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      } else {
        status = jinja_expression_span_view(expression, condition->test_name, &test_name);
        if (status != JINJA_CMETA_OK) return status;
        if (test_name.len == 0u) return JINJA_CMETA_ERR_UNSUPPORTED;
        test = JINJA_CMETA_TEST_HOST;
      }
      status = jinja_expression_builder_append_builtin_call(
          builder, tree, condition, compiled_nodes, i, JINJA_CMETA_EXPRESSION_TEST, &node_name);
      if (status == JINJA_CMETA_OK) {
        JINJA_CMETA_EXPRESSION_NODE *test_node = &builder->nodes[builder->count - 1u];
        size_t item_index = condition->first_collection_item;
        test_node->test = test;
        if (test == JINJA_CMETA_TEST_HOST) test_node->path = test_name;
        status = jinja_validate_argument_keywords(expression, tree, condition);
        if (status != JINJA_CMETA_OK) return status;
        for (size_t argument = 0u; argument < condition->collection_item_count; ++argument) {
          const JINJA_EXPRESSION_COLLECTION_ITEM *item = &tree->collection_items[item_index];
          if (item->keyword.length != 0u) {
            vstr keyword;
            status = jinja_expression_span_view(expression, item->keyword, &keyword);
            if (status != JINJA_CMETA_OK) return status;
            builder->collection_items[test_node->first_collection_item + argument].keyword = keyword;
            test_node->merge_expanded_keywords |= jinja_call_keyword_requires_merge(keyword);
          }
          item_index = item->next;
        }
      }
      break;
    }
    case JINJA_EXPRESSION_CONDITION_STRING_COMPARISON: {
      int value;
      status = jinja_fold_string_comparison(expression, condition, &value, builder->newline_sequence);
      if (status == JINJA_CMETA_OK)
        status = jinja_expression_builder_append_bool(builder, value, &node_name);
      break;
    }
    case JINJA_EXPRESSION_CONDITION_COMPARISON:
      status = jinja_append_deferred_comparison(expression, condition, frames, depth, builder,
                                                &node_name);
      break;
    case JINJA_EXPRESSION_CONDITION_NESTED_COMPARISON: {
      JINJA_CMETA_COMPARISON_KIND comparison;
      if (condition->left_condition >= i || condition->right_condition >= i ||
          !jinja_comparison_kind(condition->comparison, &comparison))
        return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      status = jinja_expression_builder_append_nested_comparison(
          builder, compiled_nodes[condition->left_condition],
          compiled_nodes[condition->right_condition], comparison, condition->unary_not_count,
          &node_name);
      break;
    }
    case JINJA_EXPRESSION_CONDITION_COMPARISON_CHAIN:
      status = jinja_expression_builder_append_comparison_chain(builder, tree, condition,
                                                                compiled_nodes, i, &node_name);
      break;
    case JINJA_EXPRESSION_CONDITION_UNARY_ARITHMETIC:
    case JINJA_EXPRESSION_CONDITION_BINARY_ARITHMETIC: {
      const int binary = condition->kind == JINJA_EXPRESSION_CONDITION_BINARY_ARITHMETIC;
      JINJA_CMETA_ARITHMETIC_KIND arithmetic;
      if (condition->left_condition >= i || (binary && condition->right_condition >= i) ||
          (!binary && condition->arithmetic > JINJA_EXPRESSION_ARITHMETIC_NEGATE) ||
          (binary && condition->arithmetic < JINJA_EXPRESSION_ARITHMETIC_ADD) ||
          !jinja_arithmetic_kind(condition->arithmetic, &arithmetic))
        return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      status = jinja_expression_builder_append_arithmetic(
          builder,
          binary ? JINJA_CMETA_EXPRESSION_BINARY_ARITHMETIC
                 : JINJA_CMETA_EXPRESSION_UNARY_ARITHMETIC,
          arithmetic, compiled_nodes[condition->left_condition],
          binary ? compiled_nodes[condition->right_condition] : 0u, condition->unary_not_count,
          &node_name);
      break;
    }
    case JINJA_EXPRESSION_CONDITION_LOGICAL_AND:
    case JINJA_EXPRESSION_CONDITION_LOGICAL_OR: {
      JINJA_CMETA_EXPRESSION_KIND kind = condition->kind == JINJA_EXPRESSION_CONDITION_LOGICAL_AND
                                             ? JINJA_CMETA_EXPRESSION_LOGICAL_AND
                                             : JINJA_CMETA_EXPRESSION_LOGICAL_OR;
      if (condition->left_condition >= i || condition->right_condition >= i)
        return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      status = jinja_expression_builder_append_logical(
          builder, kind, compiled_nodes[condition->left_condition],
          compiled_nodes[condition->right_condition], condition->unary_not_count, &node_name);
      break;
    }
    case JINJA_EXPRESSION_CONDITION_CONDITIONAL:
      if (condition->left_condition >= i || condition->test_condition >= i ||
          (condition->has_else && condition->right_condition >= i))
        return JINJA_CMETA_ERR_INVALID_ARGUMENT;
      status = jinja_expression_builder_append_conditional(
          builder, compiled_nodes[condition->left_condition],
          compiled_nodes[condition->test_condition],
          condition->has_else ? compiled_nodes[condition->right_condition] : 0u,
          condition->has_else, condition->unary_not_count, &node_name);
      break;
    case JINJA_EXPRESSION_CONDITION_LIST:
    case JINJA_EXPRESSION_CONDITION_TUPLE:
      status = jinja_expression_builder_append_list(builder, tree, condition, compiled_nodes, i,
                                                    &node_name);
      break;
    case JINJA_EXPRESSION_CONDITION_DICT:
      status = jinja_expression_builder_append_dict(builder, tree, condition, compiled_nodes, i,
                                                    &node_name);
      break;
    default:
      return JINJA_CMETA_ERR_INVALID_ARGUMENT;
    }
    if (status != JINJA_CMETA_OK) return status;
    compiled_nodes[i] = builder->count - 1u;
    if (i == tree->root) *name = node_name;
  }

  builder->nodes[compiled_nodes[tree->root]].force_boolean = force_boolean != 0;
  return JINJA_CMETA_OK;
}

static int jinja_condition_requires_tree(JINJA_EXPRESSION_CONDITION_KIND kind) {
  return kind == JINJA_EXPRESSION_CONDITION_NONE ||
         kind == JINJA_EXPRESSION_CONDITION_ITEM_LOOKUP ||
         kind == JINJA_EXPRESSION_CONDITION_SLICE_LOOKUP ||
         kind == JINJA_EXPRESSION_CONDITION_CONCAT || kind == JINJA_EXPRESSION_CONDITION_FILTER ||
         kind == JINJA_EXPRESSION_CONDITION_ATTRIBUTE_LOOKUP ||
         kind == JINJA_EXPRESSION_CONDITION_CALL || kind == JINJA_EXPRESSION_CONDITION_TEST ||
         kind == JINJA_EXPRESSION_CONDITION_NESTED_COMPARISON ||
         kind == JINJA_EXPRESSION_CONDITION_COMPARISON_CHAIN ||
         kind == JINJA_EXPRESSION_CONDITION_UNARY_ARITHMETIC ||
         kind == JINJA_EXPRESSION_CONDITION_BINARY_ARITHMETIC ||
         kind == JINJA_EXPRESSION_CONDITION_LOGICAL_AND ||
         kind == JINJA_EXPRESSION_CONDITION_LOGICAL_OR ||
         kind == JINJA_EXPRESSION_CONDITION_CONDITIONAL ||
         kind == JINJA_EXPRESSION_CONDITION_LIST || kind == JINJA_EXPRESSION_CONDITION_TUPLE ||
         kind == JINJA_EXPRESSION_CONDITION_DICT;
}

static int jinja_program_string(JINJA_PROGRAM_BUILDER *program, vstr value, size_t *offset) {
  *offset = tstr_len(program->strings);
  if (*offset > JINJA_CMETA_MAX_PROGRAM_BYTES ||
      value.len > JINJA_CMETA_MAX_PROGRAM_BYTES - *offset) {
    program->status = JINJA_CMETA_ERR_CAPACITY;
    return 0;
  }
  if (!jinja_append(&program->strings, value.data, value.len)) {
    program->status = JINJA_CMETA_ERR_OUT_OF_MEMORY;
    return 0;
  }
  return 1;
}

static int jinja_program_emit(JINJA_PROGRAM_BUILDER *program,
                               JINJA_CMETA_INSTRUCTION instruction, vstr value,
                               const JINJA_EXPRESSION_BUILDER *expressions) {
  size_t i;
  stl_status status;
  instruction.expression = SIZE_MAX;
  if (expressions != NULL) {
    for (i = 0u; i < expressions->count; ++i) {
      size_t length = strlen(expressions->names[i]);
      if (value.len == length && memcmp(value.data, expressions->names[i], length) == 0) {
        instruction.expression = i;
        value = vstr_from_buf("", 0u);
        break;
      }
    }
  }
  instruction.length = value.len;
  if (!jinja_program_string(program, value, &instruction.offset)) return 0;
  status = JinjaInstructions_push(&program->instructions, instruction);
  if (status != STL_OK) {
    program->status = status == STL_CAPACITY_EXCEEDED ? JINJA_CMETA_ERR_CAPACITY
                      : status == STL_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
                                                    : JINJA_CMETA_ERR_INVALID_ARGUMENT;
    return 0;
  }
  return 1;
}

static int jinja_program_flush_text(JINJA_PROGRAM_BUILDER *program, tstr text,
                                     size_t depth, size_t offset) {
  if (tstr_len(text) != 0u &&
      !jinja_program_emit(program,
          (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_TEXT,
                                    .depth = depth, .source_offset = offset},
          vstr_from_buf(text, tstr_len(text)), NULL))
    return 0;
  return tstr_set_len_checked(text, 0u);
}

static JINJA_EXPRESSION_PARSE_STATUS
jinja_parse_interpolation_value(vstr expression, JINJA_EXPRESSION_BUILDER *expressions,
                                const JINJA_BLOCK_FRAME *frames, size_t depth, vstr *path) {
  JINJA_EXPRESSION_TREE tree;
  const JINJA_EXPRESSION_CONDITION *condition;
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_tree(expression, &tree, NULL);

  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  if (tree.count == 0u || tree.root >= tree.count) return JINJA_EXPRESSION_PARSE_INVALID;
  condition = &tree.nodes[tree.root];
  if (jinja_condition_requires_tree(condition->kind))
    return jinja_expression_parse_status_from_cmeta(
        jinja_compile_expression_tree(expression, &tree, frames, depth, 0, expressions, path));
  if (condition->kind == JINJA_EXPRESSION_CONDITION_PATH) {
    if (condition->unary_not_count != 0u)
      return jinja_expression_parse_status_from_cmeta(
          jinja_compile_expression_tree(expression, &tree, frames, depth, 0, expressions, path));
    *path = vstr_from_buf(expression.data + condition->path.offset, condition->path.length);
    vstr lowered;
    if (jinja_lower_alias_path(*path, frames, depth, &lowered) == JINJA_ALIAS_PATH_SCOPED)
      return jinja_expression_parse_status_from_cmeta(
          jinja_expression_builder_append_path(expressions, lowered, 0, path));
    return JINJA_EXPRESSION_PARSE_OK;
  }
  if (condition->kind == JINJA_EXPRESSION_CONDITION_BOOL) {
    return jinja_expression_builder_append_bool(expressions, condition->boolean, path) ==
                   JINJA_CMETA_OK
               ? JINJA_EXPRESSION_PARSE_OK
               : JINJA_EXPRESSION_PARSE_CAPACITY;
  }
  if (condition->kind == JINJA_EXPRESSION_CONDITION_INTEGER) {
    return jinja_expression_builder_append_integer(expressions, condition->integer, path) ==
                   JINJA_CMETA_OK
               ? JINJA_EXPRESSION_PARSE_OK
               : JINJA_EXPRESSION_PARSE_CAPACITY;
  }
  if (condition->kind == JINJA_EXPRESSION_CONDITION_FLOAT) {
    return jinja_expression_builder_append_float(expressions, condition->floating, path) ==
                   JINJA_CMETA_OK
               ? JINJA_EXPRESSION_PARSE_OK
               : JINJA_EXPRESSION_PARSE_CAPACITY;
  }
  if (condition->kind == JINJA_EXPRESSION_CONDITION_STRING) {
    vstr string =
        vstr_from_buf(expression.data + condition->string.offset, condition->string.length);
    return jinja_expression_builder_append_string(expressions, string, condition->unary_not_count, path) == JINJA_CMETA_OK
               ? JINJA_EXPRESSION_PARSE_OK
               : JINJA_EXPRESSION_PARSE_CAPACITY;
  }
  if (condition->kind == JINJA_EXPRESSION_CONDITION_STRING_COMPARISON) {
    int value;
    JINJA_CMETA_STATUS fold_status = jinja_fold_string_comparison(expression, condition, &value, expressions->newline_sequence);
    if (fold_status != JINJA_CMETA_OK) return jinja_expression_parse_status_from_cmeta(fold_status);
    return jinja_expression_builder_append_bool(expressions, value, path) == JINJA_CMETA_OK
               ? JINJA_EXPRESSION_PARSE_OK
               : JINJA_EXPRESSION_PARSE_CAPACITY;
  }
  if (condition->kind == JINJA_EXPRESSION_CONDITION_COMPARISON) {
    JINJA_CMETA_STATUS comparison_status =
        jinja_append_deferred_comparison(expression, condition, frames, depth, expressions, path);
    return jinja_expression_parse_status_from_cmeta(comparison_status);
  }
  return JINJA_EXPRESSION_PARSE_UNSUPPORTED;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_parse_for(vstr body, JINJA_TEMPLATE_FOR_HEADER *header,
                                                     vstr *alias, vstr *iterable) {
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_for_header(body, header, NULL);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  const JINJA_EXPRESSION_CONDITION *target = &header->targets.nodes[header->targets.root];
  *alias = target->kind == JINJA_EXPRESSION_CONDITION_PATH
      ? vstr_from_buf(body.data + header->target.offset + target->path.offset, target->path.length)
      : vstr_from_buf("", 0u);
  *iterable = vstr_from_buf(body.data + header->iterable.offset, header->iterable.length);
  return JINJA_EXPRESSION_PARSE_OK;
}

static int jinja_lower_iterable(vstr expression, const JINJA_BLOCK_FRAME *frames, size_t depth,
                                JINJA_EXPRESSION_BUILDER *expressions, vstr *path,
                                size_t source_offset, JINJA_CMETA_ERROR *error) {
  size_t previous_count = expressions->count;
  JINJA_EXPRESSION_PARSE_STATUS status =
      jinja_parse_interpolation_value(expression, expressions, frames, depth, path);

  if (status == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, source_offset,
                          "unable to allocate iterable expression storage");
    return 0;
  }
  if (status == JINJA_EXPRESSION_PARSE_CAPACITY) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, source_offset,
                          "iterable parser or expression limit exceeded");
    return 0;
  }
  if (status == JINJA_EXPRESSION_PARSE_INVALID) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                          "invalid for iterable expression syntax");
    return 0;
  }
  if (status != JINJA_EXPRESSION_PARSE_OK || !jinja_lower_alias_path(*path, frames, depth, path)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_UNSUPPORTED, source_offset,
                          "for iterable requires a supported expression");
    return 0;
  }
  if (expressions->count == previous_count) {
    JINJA_CMETA_STATUS append_status =
        jinja_expression_builder_append_path(expressions, *path, 0u, path);
    if (append_status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, append_status, source_offset,
                            "unable to store iterable path expression");
      return 0;
    }
  }
  return 1;
}

static int jinja_lower_condition(vstr expression,
                                 const JINJA_BLOCK_FRAME frames[JINJA_CMETA_MAX_BLOCK_DEPTH],
                                 size_t depth, JINJA_EXPRESSION_BUILDER *expressions, vstr *path,
                                 char *truth_marker, size_t source_offset,
                                 JINJA_CMETA_ERROR *error) {
  JINJA_EXPRESSION_TREE tree;
  const JINJA_EXPRESSION_CONDITION *condition;
  JINJA_EXPRESSION_PARSE_STATUS parse_status = jinja_expression_parse_tree(expression, &tree, NULL);

  if (parse_status == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY) return 0;
  if (parse_status == JINJA_EXPRESSION_PARSE_CAPACITY) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, source_offset,
                          "condition parser capacity exceeded");
    return 0;
  }
  if (parse_status == JINJA_EXPRESSION_PARSE_INVALID) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset, "invalid condition syntax");
    return 0;
  }
  if (parse_status != JINJA_EXPRESSION_PARSE_OK) {
    jinja_cmeta_error_set(
        error, JINJA_CMETA_ERR_UNSUPPORTED, source_offset,
        "condition requires supported paths, literals, comparisons, or logical operators");
    return 0;
  }

  if (tree.count == 0u || tree.root >= tree.count) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                          "condition parser returned an invalid expression tree");
    return 0;
  }
  condition = &tree.nodes[tree.root];

  if (condition->kind == JINJA_EXPRESSION_CONDITION_CONDITIONAL && !condition->grouped) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                          "conditional expression in an if statement must be grouped");
    return 0;
  }

  if (jinja_condition_requires_tree(condition->kind)) {
    JINJA_CMETA_STATUS status =
        jinja_compile_expression_tree(expression, &tree, frames, depth, 1, expressions, path);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset,
                            status == JINJA_CMETA_ERR_UNSUPPORTED
                                ? "expression cannot resolve an alias outside the nearest "
                                  "loop context"
                                : "expression tree could not be compiled");
      return 0;
    }
    *truth_marker = '#';
    return 1;
  }

  if (condition->kind == JINJA_EXPRESSION_CONDITION_PATH) {
    *path = vstr_from_buf(expression.data + condition->path.offset, condition->path.length);
    if (jinja_lower_alias_path(*path, frames, depth, path) == JINJA_ALIAS_PATH_SCOPED) {
      JINJA_CMETA_STATUS status =
          jinja_expression_builder_append_path(expressions, *path, 0, path);
      if (status != JINJA_CMETA_OK) {
        jinja_cmeta_error_set(error, status, source_offset, "compiled expression limit exceeded");
        return 0;
      }
      expressions->nodes[expressions->count - 1u].force_boolean = 1;
    }
    *truth_marker = condition->truth_marker;
    return 1;
  }

  if (condition->kind == JINJA_EXPRESSION_CONDITION_BOOL) {
    JINJA_CMETA_STATUS status =
        jinja_expression_builder_append_bool(expressions, condition->boolean, path);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "compiled expression limit exceeded");
      return 0;
    }
    *truth_marker = '#';
    return 1;
  }

  if (condition->kind == JINJA_EXPRESSION_CONDITION_INTEGER) {
    JINJA_CMETA_STATUS status =
        jinja_expression_builder_append_integer(expressions, condition->integer, path);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "compiled expression limit exceeded");
      return 0;
    }
    *truth_marker = '#';
    return 1;
  }

  if (condition->kind == JINJA_EXPRESSION_CONDITION_FLOAT) {
    JINJA_CMETA_STATUS status =
        jinja_expression_builder_append_float(expressions, condition->floating, path);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "compiled expression limit exceeded");
      return 0;
    }
    *truth_marker = '#';
    return 1;
  }

  if (condition->kind == JINJA_EXPRESSION_CONDITION_STRING) {
    vstr string =
        vstr_from_buf(expression.data + condition->string.offset, condition->string.length);
    JINJA_CMETA_STATUS status = jinja_expression_builder_append_string(
        expressions, string, condition->unary_not_count, path);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "compiled expression limit exceeded");
      return 0;
    }
    *truth_marker = '#';
    return 1;
  }

  if (condition->kind == JINJA_EXPRESSION_CONDITION_STRING_COMPARISON) {
    int value;
    JINJA_CMETA_STATUS status = jinja_fold_string_comparison(expression, condition, &value, expressions->newline_sequence);
    if (status == JINJA_CMETA_OK)
      status = jinja_expression_builder_append_bool(expressions, value, path);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset,
                            "string literal comparison could not be compiled");
      return 0;
    }
    *truth_marker = '#';
    return 1;
  }

  if (condition->kind == JINJA_EXPRESSION_CONDITION_COMPARISON) {
    JINJA_CMETA_STATUS status =
        jinja_append_deferred_comparison(expression, condition, frames, depth, expressions, path);
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset,
                            status == JINJA_CMETA_ERR_UNSUPPORTED
                                ? "comparison cannot resolve an alias outside the nearest loop "
                                  "context"
                                : "runtime comparison could not be compiled");
      return 0;
    }
    *truth_marker = '#';
    return 1;
  }

  jinja_cmeta_error_set(error, JINJA_CMETA_ERR_UNSUPPORTED, source_offset,
                        "unsupported condition expression kind");
  return 0;
}

static int jinja_program_close_branch(JINJA_PROGRAM_BUILDER *program,
                                       JINJA_BLOCK_FRAME *frame, size_t depth,
                                       size_t source_offset) {
  size_t jump = JinjaInstructions_size(&program->instructions);
  if (!jinja_program_emit(program,
      (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_JUMP,
                                .target = frame->exits, .depth = depth,
                                .source_offset = source_offset},
      vstr_from_buf("", 0u), NULL))
    return 0;
  JinjaInstructions_at(&program->instructions, frame->pending_test)->target = jump + 1u;
  frame->exits = jump;
  return 1;
}

static int jinja_program_end_iteration(JINJA_PROGRAM_BUILDER *program,
                                        JINJA_BLOCK_FRAME *frame, size_t depth,
                                        size_t source_offset) {
  frame->loop_next = JinjaInstructions_size(&program->instructions);
  if (!jinja_program_emit(program,
      (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_FOR_NEXT,
                                .target = frame->opener, .depth = depth,
                                .source_offset = source_offset},
      vstr_from_buf("", 0u), NULL))
    return 0;
  JinjaInstructions_at(&program->instructions, frame->opener)->target = frame->loop_next + 1u;
  return 1;
}

static int jinja_compile_assignment_target(JINJA_PROGRAM_BUILDER *program, vstr input,
                                            const JINJA_EXPRESSION_TREE *targets, size_t node_index,
                                            size_t depth, size_t source_offset) {
  const JINJA_EXPRESSION_CONDITION *node = &targets->nodes[node_index];
  size_t instruction = JinjaInstructions_size(&program->instructions);
  int tuple = node->kind == JINJA_EXPRESSION_CONDITION_TUPLE;
  int attribute = node->kind == JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET;
  vstr name = tuple ? vstr_from_buf("", 0u)
                    : vstr_from_buf(input.data + node->path.offset, attribute
                        ? node->namespace_attribute.offset + node->namespace_attribute.length - node->path.offset
                        : node->path.length);
  if (!jinja_program_emit(program,
      (JINJA_CMETA_INSTRUCTION){.opcode = tuple ? JINJA_CMETA_OP_UNPACK : attribute
                                    ? JINJA_CMETA_OP_ASSIGN_ATTRIBUTE : JINJA_CMETA_OP_ASSIGN,
                                .target = attribute ? node->path.length : node->collection_item_count,
                                .attribute_offset = attribute ? node->namespace_attribute.offset - node->path.offset : 0u,
                                .depth = depth, .source_offset = source_offset}, name, NULL))
    return 0;
  if (tuple) {
    size_t item = node->first_collection_item;
    for (size_t i = 0u; i < node->collection_item_count; ++i) {
      const JINJA_EXPRESSION_COLLECTION_ITEM *child = &targets->collection_items[item];
      if (!jinja_compile_assignment_target(program, input, targets, child->value_condition,
                                            depth, source_offset))
        return 0;
      item = child->next;
    }
  }
  JinjaInstructions_at(&program->instructions, instruction)->end =
      JinjaInstructions_size(&program->instructions);
  return 1;
}

static JINJA_CMETA_STATUS jinja_function_parse_status(JINJA_EXPRESSION_PARSE_STATUS status) {
  switch (status) {
  case JINJA_EXPRESSION_PARSE_OK: return JINJA_CMETA_OK;
  case JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY: return JINJA_CMETA_ERR_OUT_OF_MEMORY;
  case JINJA_EXPRESSION_PARSE_CAPACITY: return JINJA_CMETA_ERR_CAPACITY;
  case JINJA_EXPRESSION_PARSE_UNSUPPORTED: return JINJA_CMETA_ERR_UNSUPPORTED;
  default: return JINJA_CMETA_ERR_SYNTAX;
  }
}

static JINJA_CMETA_STATUS jinja_compile_function_expression(vstr source, JINJA_EXPRESSION_SPAN span,
    const JINJA_BLOCK_FRAME *frames, size_t depth, JINJA_EXPRESSION_BUILDER *expressions,
    size_t *result, JINJA_CMETA_ERROR *error) {
  JINJA_EXPRESSION_TREE *tree = (JINJA_EXPRESSION_TREE *)malloc(sizeof(*tree));
  if (tree == NULL) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_OUT_OF_MEMORY, span.offset,
                         "unable to allocate function expression parser");
    return JINJA_CMETA_ERR_OUT_OF_MEMORY;
  }
  const size_t first_expression = expressions->count;
  size_t offset = 0u;
  vstr name, input = vstr_from_buf(source.data + span.offset, span.length);
  JINJA_CMETA_STATUS status = jinja_function_parse_status(
      jinja_expression_parse_tree(input, tree, &offset));
  if (status == JINJA_CMETA_OK)
    status = jinja_compile_expression_tree(input, tree, frames, depth, 0, expressions, &name);
  if (status == JINJA_CMETA_OK) {
    for (size_t i = first_expression; i < expressions->count; ++i)
      expressions->source_offsets[i] = span.offset;
    *result = expressions->count - 1u;
  } else {
    jinja_cmeta_error_set(error, status, span.offset + offset, "unable to lower function expression");
  }
  free(tree);
  return status;
}

static int jinja_compile_function_begin(JINJA_PROGRAM_BUILDER *program, size_t source_offset,
    JINJA_BLOCK_FRAME *frames, size_t *depth, JINJA_EXPRESSION_BUILDER *expressions,
    JINJA_CMETA_ERROR *error) {
  JINJA_FUNCTION_BUILDER *builder = program->functions;
  JINJA_TEMPLATE_MACRO_DESCRIPTOR descriptor;
  size_t selected = SIZE_MAX, failure = source_offset;
  if (*depth == JINJA_CMETA_MAX_BLOCK_DEPTH || builder->count == JINJA_CMETA_MAX_FUNCTIONS) {
    program->status = JINJA_CMETA_ERR_CAPACITY;
    return 0;
  }
  for (size_t i = 0u; i < builder->tree.count; ++i)
    if (builder->tree.nodes[i].source.offset == source_offset &&
        (builder->tree.nodes[i].kind == JINJA_TEMPLATE_MACRO || builder->tree.nodes[i].kind == JINJA_TEMPLATE_CALL)) {
      selected = i;
      break;
    }
  JINJA_CMETA_STATUS status = jinja_function_parse_status(jinja_template_describe_macro(
      builder->source, &builder->tree, selected, &descriptor, &failure));
  if (status != JINJA_CMETA_OK) {
    jinja_cmeta_error_set(error, status, failure, "invalid function descriptor");
    return 0;
  }
  if (descriptor.signature.parameter_count > JINJA_CMETA_MAX_PARAMETERS - builder->parameter_count) {
    program->status = JINJA_CMETA_ERR_CAPACITY;
    return 0;
  }
  const size_t index = builder->count++;
  JINJA_CMETA_FUNCTION *function = &builder->functions[index];
  *function = (JINJA_CMETA_FUNCTION){.parent = SIZE_MAX, .call_expression = SIZE_MAX,
      .first_parameter = builder->parameter_count, .parameter_count = descriptor.signature.parameter_count,
      .uses_caller = (descriptor.bindings.accesses & (1u << JINJA_TEMPLATE_MACRO_CALLER)) != 0u,
      .accepts_kwargs = (descriptor.bindings.accesses & (1u << JINJA_TEMPLATE_MACRO_KWARGS)) != 0u,
      .accepts_varargs = (descriptor.bindings.accesses & (1u << JINJA_TEMPLATE_MACRO_VARARGS)) != 0u};
  for (size_t i = *depth; i != 0u; --i)
    if (frames[i - 1u].kind == JINJA_BLOCK_FUNCTION) { function->parent = frames[i - 1u].function; break; }
  if (descriptor.call.length != 0u) {
    status = jinja_compile_function_expression(builder->source, descriptor.call, frames, *depth,
        expressions, &function->call_expression, error);
    if (status != JINJA_CMETA_OK) return 0;
  }
  size_t opener = JinjaInstructions_size(&program->instructions);
  vstr name = vstr_from_buf(builder->source.data + descriptor.signature.name.offset,
                           descriptor.signature.name.length);
  if (!jinja_program_emit(program, (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_FUNCTION,
      .target = index, .depth = *depth, .source_offset = source_offset}, name, NULL)) return 0;
  function->name_offset = JinjaInstructions_at(&program->instructions, opener)->offset;
  function->name_length = name.len;
  function->body_begin = opener + 1u;
  frames[(*depth)++] = (JINJA_BLOCK_FRAME){.kind = JINJA_BLOCK_FUNCTION,
      .opener = opener, .function = index, .source_offset = source_offset};
  for (size_t i = 0u; i < descriptor.signature.parameter_count; ++i) {
    const JINJA_EXPRESSION_PARAMETER *parsed = &descriptor.signature.parameters[i];
    JINJA_CMETA_PARAMETER *parameter = &builder->parameters[builder->parameter_count++];
    *parameter = (JINJA_CMETA_PARAMETER){.name_length = parsed->name.length, .default_expression = SIZE_MAX};
    if (!jinja_program_string(program,
        vstr_from_buf(builder->source.data + parsed->name.offset, parsed->name.length), &parameter->name_offset))
      return 0;
    if (parsed->default_expression.length != 0u) {
      status = jinja_compile_function_expression(builder->source, parsed->default_expression,
          frames, *depth, expressions, &parameter->default_expression, error);
      if (status != JINJA_CMETA_OK) return 0;
    }
  }
  return 1;
}

static int jinja_translation_compile_expression(vstr expression,
    const JINJA_BLOCK_FRAME *frames, size_t depth, JINJA_EXPRESSION_BUILDER *expressions,
    size_t *index) {
  vstr path = {0};
  const size_t before = expressions->count;
  JINJA_EXPRESSION_PARSE_STATUS parsed = jinja_parse_interpolation_value(
      expression, expressions, frames, depth, &path);
  if (parsed != JINJA_EXPRESSION_PARSE_OK) return 0;
  if (expressions->count == before) {
    if (path.len == 0u) return 0;
    if (!jinja_lower_alias_path(path, frames, depth, &path) ||
        jinja_expression_builder_append_path(expressions, path, 0u, &path) != JINJA_CMETA_OK)
      return 0;
  }
  if (expressions->count == before) return 0;
  *index = expressions->count - 1u;
  return 1;
}

static int jinja_translation_append_binding(JINJA_PROGRAM_BUILDER *program,
    vstr name, size_t expression, JINJA_CMETA_TRANSLATION_BINDING *binding) {
  if (!jinja_program_string(program, name, &binding->name_offset)) return 0;
  binding->name_length = name.len;
  binding->expression = expression;
  return 1;
}

static int jinja_translation_binding_modifier(vstr *item, int *trimmed) {
  static const char trimmed_word[] = "trimmed";
  static const char notrimmed_word[] = "notrimmed";
  const char *word = NULL;
  int value;
  size_t word_length;
  size_t suffix;

  if (item == NULL || trimmed == NULL) return -1;
  if (item->len >= sizeof(trimmed_word) - 1u &&
      memcmp(item->data, trimmed_word, sizeof(trimmed_word) - 1u) == 0) {
    word = trimmed_word;
    value = 1;
  } else if (item->len >= sizeof(notrimmed_word) - 1u &&
             memcmp(item->data, notrimmed_word, sizeof(notrimmed_word) - 1u) == 0) {
    word = notrimmed_word;
    value = 0;
  } else {
    return 0;
  }
  word_length = strlen(word);
  suffix = jinja_raw_skip_space(*item, word_length);
  if (suffix == word_length && item->len != word_length) return 0;
  if (*trimmed != -1 && *trimmed != value) return -1;
  *trimmed = value;
  *item = jinja_trim(vstr_from_buf(item->data + suffix, item->len - suffix));
  return 1;
}

static int jinja_translation_binding_segment(vstr segment,
    const JINJA_BLOCK_FRAME *frames, size_t depth, JINJA_EXPRESSION_BUILDER *expressions,
    vstr *names, size_t *binding_expressions, size_t *binding_count, int *trimmed) {
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token;
  int kind;
  vstr item = jinja_trim(segment);
  size_t expression_index;
  const int modifier = jinja_translation_binding_modifier(&item, trimmed);
  if (modifier < 0) return 0;
  if (item.len == 0u) return modifier != 0 || segment.len == 0u;
  jinja_expression_lexer_init(&lexer, item);
  if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0 ||
      (kind != JINJA_EXPRESSION_TOKEN_IDENTIFIER && kind != JINJA_EXPRESSION_TOKEN_NOT)) return 0;
  vstr name = vstr_from_buf(item.data + token.offset,
      kind == JINJA_EXPRESSION_TOKEN_NOT ? sizeof("not") - 1u : token.length);
  for (size_t i = 0u; i < *binding_count; ++i)
    if (vstr_eq(names[i], name)) return 0;
  const int next_status = jinja_expression_lexer_next(&lexer, &kind, &token);
  vstr expression = name;
  if (next_status > 0 && kind == JINJA_EXPRESSION_TOKEN_ASSIGN) {
    if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0) return 0;
    expression = jinja_trim(vstr_from_buf(item.data + token.offset,
        item.len - token.offset));
    if (expression.len == 0u) return 0;
  } else if (next_status != JINJA_EXPRESSION_LEX_EOF) {
    return 0;
  }
  if (*binding_count == JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
      !jinja_translation_compile_expression(expression, frames, depth, expressions,
          &expression_index)) return 0;
  names[*binding_count] = name;
  binding_expressions[*binding_count] = expression_index;
  ++*binding_count;
  return 1;
}

static int jinja_translation_compile_bindings(vstr header,
    const JINJA_BLOCK_FRAME *frames, size_t depth, JINJA_EXPRESSION_BUILDER *expressions,
    vstr *context, vstr *names, size_t *binding_expressions, size_t *binding_count,
    int *trimmed) {
  size_t context_end = 0u;
  size_t segment_start = 0u;
  size_t nesting = 0u;
  if (trimmed == NULL) return 0;
  *trimmed = -1;
  if (header.len != 0u && (header.data[0] == '\'' || header.data[0] == '"')) {
    const char quote = header.data[0];
    size_t cursor = 1u;
    while (cursor < header.len) {
      if (header.data[cursor] == '\\') { cursor += cursor + 1u < header.len ? 2u : 1u; continue; }
      if (header.data[cursor++] == quote) break;
    }
    if (cursor == header.len && header.data[cursor - 1u] != quote) return 0;
    *context = vstr_from_buf(header.data + 1u, cursor - 2u);
    context_end = cursor;
  }
  vstr input = vstr_from_buf(header.data + context_end, header.len - context_end);
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token;
  int kind;
  jinja_expression_lexer_init(&lexer, input);
  for (;;) {
    const int status = jinja_expression_lexer_next(&lexer, &kind, &token);
    if (status == JINJA_EXPRESSION_LEX_EOF) {
      return jinja_translation_binding_segment(
          vstr_from_buf(input.data + segment_start, input.len - segment_start), frames, depth,
          expressions, names, binding_expressions, binding_count, trimmed);
    }
    if (status <= 0) return 0;
    if (kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN || kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACKET ||
        kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACE) {
      ++nesting;
    } else if (kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN ||
               kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET ||
               kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACE) {
      if (nesting == 0u) return 0;
      --nesting;
    } else if (nesting == 0u && kind == JINJA_EXPRESSION_TOKEN_COMMA) {
      if (!jinja_translation_binding_segment(
              vstr_from_buf(input.data + segment_start, token.offset - segment_start), frames, depth,
              expressions, names, binding_expressions, binding_count, trimmed)) return 0;
      segment_start = token.offset + token.length;
    } else if (nesting == 0u && kind == JINJA_EXPRESSION_TOKEN_COLON) {
      return jinja_translation_binding_segment(
          vstr_from_buf(input.data + segment_start, token.offset - segment_start), frames, depth,
          expressions, names, binding_expressions, binding_count, trimmed);
    }
  }
}

static int jinja_translation_message_append(tstr *message, vstr value) {
  return jinja_append(message, value.data, value.len);
}

static int jinja_translation_message_body(vstr source, const JINJA_TEMPLATE_TREE *tree,
    size_t begin, size_t end, tstr *message, vstr *names, size_t *binding_expressions,
    size_t *binding_count, const JINJA_BLOCK_FRAME *frames, size_t depth,
    JINJA_EXPRESSION_BUILDER *expressions) {
  for (size_t i = begin; i < end; ++i) {
    const JINJA_TEMPLATE_NODE *node = &tree->nodes[i];
    if (node->kind == JINJA_TEMPLATE_TEXT) {
      if (!jinja_translation_message_append(message,
              vstr_from_buf(source.data + node->content.offset, node->content.length))) return 0;
      continue;
    }
    if (node->kind != JINJA_TEMPLATE_OUTPUT) continue;
    vstr name = vstr_from_buf(source.data + node->name.offset, node->name.length);
    if (!jinja_append(message, "%(", 2u) ||
        !jinja_translation_message_append(message, name) ||
        !jinja_append(message, ")s", 2u)) return 0;
    size_t found = SIZE_MAX;
    for (size_t j = 0u; j < *binding_count; ++j)
      if (vstr_eq(names[j], name)) { found = j; break; }
    if (found == SIZE_MAX) {
      if (*binding_count == JINJA_CMETA_MAX_COMPILED_EXPRESSIONS ||
          !jinja_translation_compile_expression(name, frames, depth, expressions,
              &binding_expressions[*binding_count])) return 0;
      names[*binding_count] = name;
      ++*binding_count;
    }
  }
  return 1;
}

static int jinja_translation_trim_message(tstr *message) {
  tstr normalized;
  vstr input;
  size_t cursor = 0u;
  int pending_space = 0;

  if (message == NULL || *message == NULL) return 0;
  normalized = tstr_new();
  if (normalized == NULL) return 0;
  input = vstr_from_buf(*message, tstr_len(*message));
  while (cursor < input.len) {
    salts_unicode_scalar scalar;
    const size_t start = cursor;
    if (salts_unicode_utf8_next(input, &cursor, &scalar) != SALTS_UNICODE_OK) {
      tstr_free(normalized);
      return 0;
    }
    if (jinja_raw_space(scalar)) {
      pending_space = tstr_len(normalized) != 0u;
      continue;
    }
    if (pending_space && !jinja_append(&normalized, " ", 1u)) {
      tstr_free(normalized);
      return 0;
    }
    pending_space = 0;
    if (!jinja_append(&normalized, input.data + start, cursor - start)) {
      tstr_free(normalized);
      return 0;
    }
  }
  tstr_free(*message);
  *message = normalized;
  return 1;
}

static int jinja_compile_translation(JINJA_PROGRAM_BUILDER *program, vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener_index, size_t source_offset,
    const JINJA_BLOCK_FRAME *frames, size_t depth, JINJA_EXPRESSION_BUILDER *expressions,
    const JINJA_CMETA_ENV *env, JINJA_CMETA_ERROR *error) {
  const JINJA_TEMPLATE_NODE *opener;
  vstr names[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  size_t binding_expressions[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS];
  size_t binding_count = 0u;
  vstr context = vstr_from_buf("", 0u);
  tstr singular = NULL;
  tstr plural = NULL;
  JINJA_CMETA_TRANSLATION translation = {0};
  size_t end_index;
  int trimmed = -1;
  if (env == NULL || env->options.translation == NULL) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_UNSUPPORTED, source_offset,
        "i18n requires an environment translation callback");
    return 0;
  }
  if (opener_index >= tree->count || tree->nodes[opener_index].kind != JINJA_TEMPLATE_TRANS ||
      tree->nodes[opener_index].match >= tree->count) {
    program->status = JINJA_CMETA_ERR_METADATA;
    return 0;
  }
  opener = &tree->nodes[opener_index];
  end_index = opener->match;
  if (!jinja_translation_compile_bindings(
          vstr_from_buf(source.data + opener->header.offset, opener->header.length),
          frames, depth, expressions, &context, names, binding_expressions, &binding_count,
          &trimmed)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
        "invalid translation binding expression");
    return 0;
  }
  singular = tstr_new();
  plural = tstr_new();
  if (singular == NULL || plural == NULL) { program->status = JINJA_CMETA_ERR_OUT_OF_MEMORY; goto cleanup; }
  size_t branch = opener->branch < end_index ? opener->branch : end_index;
  if (!jinja_translation_message_body(source, tree, opener_index + 1u, branch, &singular,
          names, binding_expressions, &binding_count, frames, depth, expressions)) {
    program->status = JINJA_CMETA_ERR_CAPACITY;
    goto cleanup;
  }
  if (branch != end_index && !jinja_translation_message_body(source, tree, branch + 1u,
          end_index, &plural, names, binding_expressions, &binding_count, frames, depth, expressions)) {
    program->status = JINJA_CMETA_ERR_CAPACITY;
    goto cleanup;
  }
  if (trimmed == 1 && (!jinja_translation_trim_message(&singular) ||
      (branch != end_index && !jinja_translation_trim_message(&plural)))) {
    program->status = JINJA_CMETA_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  if (!jinja_program_string(program, context, &translation.context_offset) ||
      !jinja_program_string(program, vstr_from_buf(singular, tstr_len(singular)),
          &translation.singular_offset) ||
      !jinja_program_string(program, vstr_from_buf(plural, tstr_len(plural)),
          &translation.plural_offset)) goto cleanup;
  translation.context_length = context.len;
  translation.singular_length = tstr_len(singular);
  translation.plural_length = branch == end_index ? 0u : tstr_len(plural);
  translation.first_binding = JinjaTranslationBindings_size(&program->translation_bindings);
  translation.binding_count = binding_count;
  if (translation.first_binding > JINJA_CMETA_MAX_INSTRUCTIONS ||
      binding_count > JINJA_CMETA_MAX_INSTRUCTIONS - translation.first_binding) {
    program->status = JINJA_CMETA_ERR_CAPACITY;
    goto cleanup;
  }
  for (size_t i = 0u; i < binding_count; ++i) {
    JINJA_CMETA_TRANSLATION_BINDING binding = {0};
    if (!jinja_translation_append_binding(program, names[i], binding_expressions[i], &binding))
      goto cleanup;
    const stl_status binding_status = JinjaTranslationBindings_push(
        &program->translation_bindings, binding);
    if (binding_status != STL_OK) {
      program->status = binding_status == STL_CAPACITY_EXCEEDED
          ? JINJA_CMETA_ERR_CAPACITY : JINJA_CMETA_ERR_OUT_OF_MEMORY;
      goto cleanup;
    }
  }
  size_t target = JinjaTranslations_size(&program->translations);
  const stl_status translation_status = JinjaTranslations_push(&program->translations, translation);
  if (translation_status != STL_OK) {
    program->status = translation_status == STL_CAPACITY_EXCEEDED
        ? JINJA_CMETA_ERR_CAPACITY : JINJA_CMETA_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  const stl_status instruction_status = JinjaInstructions_push(&program->instructions,
      (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_TRANSLATE,
          .target = target, .depth = depth, .source_offset = source_offset});
  if (instruction_status != STL_OK) {
    program->status = instruction_status == STL_CAPACITY_EXCEEDED
        ? JINJA_CMETA_ERR_CAPACITY : JINJA_CMETA_ERR_OUT_OF_MEMORY;
    goto cleanup;
  }
  tstr_free(singular);
  tstr_free(plural);
  return 1;
cleanup:
  if (singular != NULL) tstr_free(singular);
  if (plural != NULL) tstr_free(plural);
  return 0;
}

static int
jinja_compile_statement(JINJA_PROGRAM_BUILDER *program, vstr statement, size_t source_offset,
                         JINJA_BLOCK_FRAME frames[JINJA_CMETA_MAX_BLOCK_DEPTH], size_t *depth,
                         size_t *branch_count, JINJA_EXPRESSION_BUILDER *expressions,
                         JINJA_CMETA_ERROR *error) {
  JINJA_BLOCK_FRAME *frame;
  vstr path = {0}, alias;
  char truth_marker;
  const size_t keyword_length = jinja_expression_lexical_token_length(statement, 0u);
  if (keyword_length == SIZE_MAX || keyword_length == 0u || keyword_length > statement.len) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset, "invalid statement name");
    return 0;
  }
  const vstr keyword = vstr_from_buf(statement.data, keyword_length);
  JINJA_EXPRESSION_SPAN arguments = jinja_template_content_left(statement,
      (JINJA_EXPRESSION_SPAN){keyword_length, statement.len - keyword_length}, -1, 0);
  arguments = jinja_template_content_right(statement, arguments, -1, 0);
  const vstr header_body = vstr_from_buf(statement.data + arguments.offset, arguments.length);
  const int is_if = jinja_view_equal(keyword, "if");
  const int is_elif = jinja_view_equal(keyword, "elif");
  const int is_print = jinja_view_equal(keyword, "print");

  if (program->functions != NULL) {
    if (jinja_view_equal(keyword, "block")) {
      JINJA_FUNCTION_BUILDER *builder = program->functions;
      if (*depth == JINJA_CMETA_MAX_BLOCK_DEPTH || builder->count == JINJA_CMETA_MAX_FUNCTIONS) {
        program->status = JINJA_CMETA_ERR_CAPACITY;
        return 0;
      }
      JINJA_TEMPLATE_BLOCK_HEADER header;
      size_t failure = 0u;
      if (jinja_expression_parse_block_header(header_body, &header, &failure) != JINJA_EXPRESSION_PARSE_OK)
        return 0;
      const size_t index = builder->count++, opener = JinjaInstructions_size(&program->instructions);
      const vstr name = vstr_from_buf(header_body.data + header.name.offset, header.name.length);
      if (!jinja_program_emit(program, (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_BLOCK,
          .target = index, .depth = *depth, .source_offset = source_offset}, name, NULL)) return 0;
      builder->functions[index] = (JINJA_CMETA_FUNCTION){.is_block = 1, .scoped = header.scoped,
          .required = header.required, .parent = SIZE_MAX, .call_expression = SIZE_MAX,
          .name_offset = JinjaInstructions_at(&program->instructions, opener)->offset,
          .name_length = name.len, .body_begin = opener + 1u};
      frames[(*depth)++] = (JINJA_BLOCK_FRAME){.kind = JINJA_BLOCK_NAMED,
          .opener = opener, .function = index, .source_offset = source_offset};
      return 1;
    }
    if (jinja_view_equal(keyword, "endblock")) {
      frame = &frames[--*depth];
      program->functions->functions[frame->function].body_end = JinjaInstructions_size(&program->instructions);
      return 1;
    }
    if (jinja_view_equal(keyword, "macro") || jinja_view_equal(keyword, "call"))
      return jinja_compile_function_begin(program, source_offset, frames, depth, expressions, error);
    if (jinja_view_equal(keyword, "endmacro") || jinja_view_equal(keyword, "endcall")) {
      if (*depth == 0u || frames[*depth - 1u].kind != JINJA_BLOCK_FUNCTION || header_body.len != 0u) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset, "unmatched function end");
        return 0;
      }
      frame = &frames[*depth - 1u];
      JINJA_CMETA_FUNCTION *function = &program->functions->functions[frame->function];
      if ((function->call_expression != SIZE_MAX) != jinja_view_equal(keyword, "endcall")) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset, "mismatched function end");
        return 0;
      }
      function->body_end = JinjaInstructions_size(&program->instructions);
      JinjaInstructions_at(&program->instructions, frame->opener)->end = function->body_end;
      --*depth;
      return 1;
    }
  }

  if (jinja_view_equal(keyword, "break") || jinja_view_equal(keyword, "continue")) {
    if (header_body.len == 0u) {
      for (size_t i = *depth; i != 0u;) {
        --i;
        if (frames[i].kind == JINJA_BLOCK_FUNCTION || frames[i].kind == JINJA_BLOCK_NAMED) break;
        if (frames[i].kind != JINJA_BLOCK_FOR) continue;
        if (frames[i].has_else) {
          if (JinjaInstructions_at(&program->instructions, frames[i].opener)->recursive) break;
          continue;
        }
        return jinja_program_emit(program,
            (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_LOOP_CONTROL,
                .target = frames[i].opener, .flag = jinja_view_equal(keyword, "break"),
                .depth = *depth, .source_offset = source_offset}, vstr_from_cstr(""), NULL);
      }
    }
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                          "loop control requires an enclosing loop body and no arguments");
    return 0;
  }

  if (is_print || jinja_view_equal(keyword, "do")) {
    JINJA_EXPRESSION_TREE tree;
    vstr body = header_body;
    if (is_print && body.len == 0u) return 1;
    if (is_print && body.data[body.len - 1u] == ',') {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset, "print requires an expression after comma");
      return 0;
    }
    JINJA_EXPRESSION_PARSE_STATUS parsed = jinja_expression_parse_tree(body, &tree, NULL);
    JINJA_CMETA_STATUS status = parsed == JINJA_EXPRESSION_PARSE_OK
        ? jinja_compile_expression_tree(body, &tree, frames, *depth, 0, expressions, &path)
        : parsed == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
        : parsed == JINJA_EXPRESSION_PARSE_CAPACITY ? JINJA_CMETA_ERR_CAPACITY
        : parsed == JINJA_EXPRESSION_PARSE_UNSUPPORTED ? JINJA_CMETA_ERR_UNSUPPORTED : JINJA_CMETA_ERR_SYNTAX;
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "invalid or unsupported statement expression");
      return 0;
    }
    const size_t root = expressions->count - 1u;
    const JINJA_CMETA_EXPRESSION_NODE *node = &expressions->nodes[root];
    const int separate = is_print && tree.unparenthesized_tuple;
    const size_t count = separate ? node->collection_item_count : 1u;
    for (size_t i = 0u; i < count; ++i) {
      const size_t index = JinjaInstructions_size(&program->instructions);
      const size_t expression = separate
          ? expressions->collection_items[node->first_collection_item + i].value_node : root;
      if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = is_print ? JINJA_CMETA_OP_OUTPUT : JINJA_CMETA_OP_EVALUATE,
                                  .depth = *depth, .source_offset = source_offset},
        vstr_from_buf("", 0u), NULL)) return 0;
      JinjaInstructions_at(&program->instructions, index)->expression = expression;
    }
    return 1;
  }

  if (jinja_view_equal(keyword, "filter")) {
    JINJA_EXPRESSION_TREE tree;
    vstr body = header_body;
    size_t opener = JinjaInstructions_size(&program->instructions);
    if (*depth == JINJA_CMETA_MAX_BLOCK_DEPTH) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, source_offset, "filter nesting limit exceeded");
      return 0;
    }
    JINJA_EXPRESSION_PARSE_STATUS parsed = jinja_expression_parse_filter_block(body, &tree, NULL);
    JINJA_CMETA_STATUS status = parsed == JINJA_EXPRESSION_PARSE_OK
        ? jinja_compile_expression_tree(body, &tree, frames, *depth, 0, expressions, &path)
        : parsed == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
        : parsed == JINJA_EXPRESSION_PARSE_CAPACITY ? JINJA_CMETA_ERR_CAPACITY
        : parsed == JINJA_EXPRESSION_PARSE_UNSUPPORTED ? JINJA_CMETA_ERR_UNSUPPORTED : JINJA_CMETA_ERR_SYNTAX;
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "invalid filter block header");
      return 0;
    }
    if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_FILTER_BEGIN,
                                  .depth = *depth + 1u, .source_offset = source_offset},
        vstr_from_buf("", 0u), NULL)) return 0;
    JinjaInstructions_at(&program->instructions, opener)->expression = expressions->count - 1u;
    frames[(*depth)++] = (JINJA_BLOCK_FRAME){.kind = JINJA_BLOCK_FILTER, .opener = opener, .source_offset = source_offset};
    return 1;
  }
  if (jinja_view_equal(statement, "endfilter")) {
    if (*depth == 0u || frames[*depth - 1u].kind != JINJA_BLOCK_FILTER) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset, "endfilter does not match filter");
      return 0;
    }
    JinjaInstructions_at(&program->instructions, frames[*depth - 1u].opener)->end =
        JinjaInstructions_size(&program->instructions);
    if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_FILTER_END,
                                  .target = frames[*depth - 1u].opener,
                                  .depth = *depth, .source_offset = source_offset},
        vstr_from_buf("", 0u), NULL)) return 0;
    --*depth;
    return 1;
  }

  if (jinja_view_equal(keyword, "with")) {
    size_t opener = JinjaInstructions_size(&program->instructions);
    vstr body = header_body;
    if (*depth == JINJA_CMETA_MAX_BLOCK_DEPTH) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, source_offset, "with nesting limit exceeded");
      return 0;
    }
    if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_WITH_BEGIN,
                                  .depth = *depth + 1u, .source_offset = source_offset},
        vstr_from_buf("", 0u), NULL)) return 0;
    while (body.len != 0u) {
      JINJA_EXPRESSION_TREE tree, targets;
      vstr rhs;
      size_t consumed, target = JinjaInstructions_size(&program->instructions);
      JINJA_EXPRESSION_PARSE_STATUS parsed = jinja_expression_parse_with_binding(body, &consumed, &rhs, &tree, &targets);
      JINJA_CMETA_STATUS status = parsed == JINJA_EXPRESSION_PARSE_OK
          ? jinja_compile_expression_tree(rhs, &tree, frames, *depth, 0, expressions, &path)
          : parsed == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
          : parsed == JINJA_EXPRESSION_PARSE_CAPACITY ? JINJA_CMETA_ERR_CAPACITY
          : parsed == JINJA_EXPRESSION_PARSE_UNSUPPORTED ? JINJA_CMETA_ERR_UNSUPPORTED : JINJA_CMETA_ERR_SYNTAX;
      if (status != JINJA_CMETA_OK) {
        jinja_cmeta_error_set(error, status, source_offset, "invalid with initializer");
        return 0;
      }
      if (!jinja_compile_assignment_target(program, body, &targets, targets.root, *depth + 1u, source_offset))
        return 0;
      JinjaInstructions_at(&program->instructions, target)->expression = expressions->count - 1u;
      if (consumed == body.len) break;
      body = jinja_trim(vstr_from_buf(body.data + consumed + 1u, body.len - consumed - 1u));
      if (body.len == 0u) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset, "with initializer expected after comma");
        return 0;
      }
    }
    JinjaInstructions_at(&program->instructions, opener)->end = JinjaInstructions_size(&program->instructions);
    frames[(*depth)++] = (JINJA_BLOCK_FRAME){.kind = JINJA_BLOCK_WITH, .opener = opener, .source_offset = source_offset};
    return 1;
  }
  if (jinja_view_equal(statement, "endwith")) {
    if (*depth == 0u || frames[*depth - 1u].kind != JINJA_BLOCK_WITH) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset, "endwith does not match with");
      return 0;
    }
    if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_SCOPE_END,
                                  .depth = *depth, .source_offset = source_offset},
        vstr_from_buf("", 0u), NULL)) return 0;
    --*depth;
    return 1;
  }

  if (jinja_view_equal(keyword, "set")) {
    JINJA_EXPRESSION_TREE tree, targets;
    vstr name, rhs;
    vstr body = header_body;
    size_t index;
    int capture;
    JINJA_EXPRESSION_PARSE_STATUS parsed = jinja_expression_parse_assignment(
        body, &name, &rhs, &tree, &targets, &capture, NULL);
    JINJA_CMETA_STATUS status = parsed == JINJA_EXPRESSION_PARSE_OK
        ? jinja_compile_expression_tree(rhs, &tree, frames, *depth, 0, expressions, &path)
        : parsed == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
        : parsed == JINJA_EXPRESSION_PARSE_CAPACITY ? JINJA_CMETA_ERR_CAPACITY
        : parsed == JINJA_EXPRESSION_PARSE_UNSUPPORTED ? JINJA_CMETA_ERR_UNSUPPORTED
        : JINJA_CMETA_ERR_SYNTAX;
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "invalid or unsupported assignment");
      return 0;
    }
    for (size_t target = 0u; target < targets.count; ++target) {
      const JINJA_EXPRESSION_CONDITION *node = &targets.nodes[target];
      if (node->kind != JINJA_EXPRESSION_CONDITION_PATH ||
          !jinja_view_equal(vstr_from_buf(body.data + node->path.offset, node->path.length), "loop"))
        continue;
      for (size_t i = 0u; i < *depth; ++i) {
        if (frames[i].kind == JINJA_BLOCK_FOR) {
          jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                                "cannot assign to loop inside a for block");
          return 0;
        }
      }
    }
    index = JinjaInstructions_size(&program->instructions);
    if (capture) {
      if (*depth == JINJA_CMETA_MAX_BLOCK_DEPTH) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, source_offset,
                              "capture block nesting limit exceeded");
        return 0;
      }
      if (!jinja_program_emit(program,
          (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_CAPTURE_BEGIN,
                                    .depth = *depth + 1u, .source_offset = source_offset},
          vstr_from_buf("", 0u), NULL)) return 0;
    }
    if (!jinja_compile_assignment_target(program, body, &targets, targets.root, *depth, source_offset))
      return 0;
    /* The parsed root is stored last; name bytes and RHS have distinct slots. */
    JinjaInstructions_at(&program->instructions, index)->expression = expressions->count - 1u;
    if (capture) {
      JinjaInstructions_at(&program->instructions, index)->end = JinjaInstructions_size(&program->instructions);
      frames[(*depth)++] = (JINJA_BLOCK_FRAME){.kind = JINJA_BLOCK_CAPTURE,
                                              .opener = index, .source_offset = source_offset};
    }
    return 1;
  }

  if (jinja_view_equal(statement, "endset")) {
    if (*depth == 0u || frames[*depth - 1u].kind != JINJA_BLOCK_CAPTURE) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                            "endset does not match a capture block");
      return 0;
    }
    if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_CAPTURE_END,
                                  .target = frames[*depth - 1u].opener,
                                  .depth = *depth, .source_offset = source_offset},
        vstr_from_buf("", 0u), NULL)) return 0;
    --*depth;
    return 1;
  }

  if (jinja_view_equal(keyword, "autoescape")) {
    JINJA_EXPRESSION_TREE tree;
    JINJA_EXPRESSION_PARSE_STATUS parsed;
    JINJA_CMETA_STATUS status;
    vstr expression = header_body;
    if (*depth == JINJA_CMETA_MAX_BLOCK_DEPTH) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, source_offset,
                            "control block nesting limit exceeded");
      return 0;
    }
    parsed = jinja_expression_parse_tree(expression, &tree, NULL);
    status = parsed == JINJA_EXPRESSION_PARSE_OK
        ? jinja_compile_expression_tree(expression, &tree, frames, *depth, 1, expressions, &path)
        : parsed == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
        : parsed == JINJA_EXPRESSION_PARSE_CAPACITY ? JINJA_CMETA_ERR_CAPACITY
        : parsed == JINJA_EXPRESSION_PARSE_UNSUPPORTED ? JINJA_CMETA_ERR_UNSUPPORTED
        : JINJA_CMETA_ERR_SYNTAX;
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "invalid autoescape expression");
      return 0;
    }
    frames[*depth] = (JINJA_BLOCK_FRAME){.kind = JINJA_BLOCK_AUTOESCAPE,
                                         .source_offset = source_offset};
    ++*depth;
    return jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_AUTOESCAPE_BEGIN,
                                  .depth = *depth,
                                  .source_offset = source_offset}, path, expressions);
  }

  if (jinja_view_equal(statement, "endautoescape")) {
    if (*depth == 0u || frames[*depth - 1u].kind != JINJA_BLOCK_AUTOESCAPE) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                            "autoescape closer does not match opener");
      return 0;
    }
    if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_AUTOESCAPE_END,
                                  .depth = *depth, .source_offset = source_offset},
        vstr_from_buf("", 0u), NULL)) return 0;
    --*depth;
    return 1;
  }

  if (is_if || is_elif) {
    if (is_elif && (*depth == 0u || frames[*depth - 1u].kind != JINJA_BLOCK_IF ||
                   frames[*depth - 1u].has_else)) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                            "elif has no matching if block or follows else");
      return 0;
    }
    if ((is_if && *depth == JINJA_CMETA_MAX_BLOCK_DEPTH) ||
        *branch_count == JINJA_CMETA_MAX_CONDITION_BRANCHES) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, source_offset,
                            "control block or conditional branch limit exceeded");
      return 0;
    }
    if (!jinja_lower_condition(header_body,
                               frames, *depth, expressions, &path, &truth_marker,
                               source_offset, error))
      return 0;
    if (is_if) {
      frame = &frames[*depth];
      *frame = (JINJA_BLOCK_FRAME){.kind = JINJA_BLOCK_IF, .exits = SIZE_MAX,
                                   .branch_start = *branch_count, .source_offset = source_offset};
      ++*depth;
    } else {
      frame = &frames[*depth - 1u];
      if (!jinja_program_close_branch(program, frame, *depth, source_offset)) return 0;
    }
    ++*branch_count;
    ++frame->branch_count;
    frame->pending_test = JinjaInstructions_size(&program->instructions);
    return jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_TEST, .target = SIZE_MAX,
                                  .flag = truth_marker == '^', .depth = *depth,
                                  .source_offset = source_offset},
        path, expressions);
  }

  if (jinja_view_equal(keyword, "for")) {
    vstr iterable;
    JINJA_TEMPLATE_FOR_HEADER header;
    JINJA_EXPRESSION_PARSE_STATUS parsed = jinja_parse_for(header_body, &header, &alias, &iterable);
    if (parsed != JINJA_EXPRESSION_PARSE_OK) {
      JINJA_CMETA_STATUS status = parsed == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
          : parsed == JINJA_EXPRESSION_PARSE_CAPACITY ? JINJA_CMETA_ERR_CAPACITY
          : parsed == JINJA_EXPRESSION_PARSE_UNSUPPORTED ? JINJA_CMETA_ERR_UNSUPPORTED : JINJA_CMETA_ERR_SYNTAX;
      jinja_cmeta_error_set(error, status, source_offset,
                            "for requires a valid supported header");
      return 0;
    }
    for (size_t i = 0u; i < header.targets.count; ++i) {
      const JINJA_EXPRESSION_CONDITION *target = &header.targets.nodes[i];
      if (target->kind != JINJA_EXPRESSION_CONDITION_PATH ||
          !jinja_view_equal(vstr_from_buf(header_body.data + header.target.offset + target->path.offset,
                                         target->path.length), "loop")) continue;
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                            "cannot use loop as a for target");
      return 0;
    }
    if (*depth == JINJA_CMETA_MAX_BLOCK_DEPTH) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, source_offset,
                            "control block nesting limit exceeded");
      return 0;
    }
    if (!jinja_lower_iterable(iterable, frames, *depth, expressions, &path, source_offset, error))
      return 0;
    expressions->nodes[expressions->count - 1u].loop_alias = alias;
    frame = &frames[*depth];
    *frame = (JINJA_BLOCK_FRAME){.kind = JINJA_BLOCK_FOR, .alias = alias,
                                 .source_offset = source_offset,
                                 .opener = JinjaInstructions_size(&program->instructions)};
    ++*depth;
    if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_FOR_BEGIN, .target = SIZE_MAX,
                                  .recursive = header.recursive,
                                  .depth = *depth, .source_offset = source_offset},
        path, expressions)) return 0;
    /* Target descriptors are consumed by both loop entry and advance, never
     * executed as standalone assignments. Their end marks the body start. */
    if (!jinja_compile_assignment_target(program,
        vstr_from_buf(header_body.data + header.target.offset, header.target.length),
        &header.targets, header.targets.root, *depth, source_offset)) return 0;
    if (header.has_test) {
      if (!jinja_lower_iterable(vstr_from_buf(header_body.data + header.test.offset, header.test.length),
          frames, *depth - 1u, expressions, &path, source_offset, error)) return 0;
      JINJA_CMETA_INSTRUCTION *opener = JinjaInstructions_at(&program->instructions, frame->opener);
      opener->flag = 1;
      opener->filter_expression = expressions->count - 1u;
    }
    return 1;
  }

  if (jinja_view_equal(keyword, "include") || jinja_view_equal(keyword, "import") ||
      jinja_view_equal(keyword, "from") || jinja_view_equal(keyword, "extends")) {
    const JINJA_TEMPLATE_REFERENCE_KIND kind =
        jinja_view_equal(keyword, "include") ? JINJA_TEMPLATE_REFERENCE_INCLUDE :
        jinja_view_equal(keyword, "import") ? JINJA_TEMPLATE_REFERENCE_IMPORT :
        jinja_view_equal(keyword, "from") ? JINJA_TEMPLATE_REFERENCE_FROM : JINJA_TEMPLATE_REFERENCE_EXTENDS;
    JINJA_TEMPLATE_REFERENCE reference;
    const JINJA_EXPRESSION_PARSE_STATUS parsed =
        jinja_expression_parse_template_reference(header_body, kind, &reference, NULL);
    const JINJA_CMETA_STATUS status = parsed == JINJA_EXPRESSION_PARSE_OK
                                           ? JINJA_CMETA_OK
                                           : parsed == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY
                                               ? JINJA_CMETA_ERR_OUT_OF_MEMORY
                                               : parsed == JINJA_EXPRESSION_PARSE_CAPACITY
                                                   ? JINJA_CMETA_ERR_CAPACITY
                                                   : parsed == JINJA_EXPRESSION_PARSE_UNSUPPORTED
                                                       ? JINJA_CMETA_ERR_UNSUPPORTED
                                                       : JINJA_CMETA_ERR_SYNTAX;
    if (status != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, status, source_offset, "invalid template reference statement");
      return 0;
    }
    const vstr expression = vstr_from_buf(header_body.data + reference.expression.offset,
        reference.expression.length);
    const JINJA_CMETA_STATUS lowered = jinja_compile_expression_tree(expression,
        &reference.tree, frames, *depth, 0, expressions, &path);
    if (lowered != JINJA_CMETA_OK) {
      jinja_cmeta_error_set(error, lowered, source_offset, "invalid template reference expression");
      return 0;
    }
    if (kind == JINJA_TEMPLATE_REFERENCE_EXTENDS) {
      for (size_t i = 0u; i < *depth; ++i) {
        if (frames[i].kind != JINJA_BLOCK_IF) {
          jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
              "extends requires template scope");
          return 0;
        }
      }
    }
    const JINJA_CMETA_OPCODE opcode =
        kind == JINJA_TEMPLATE_REFERENCE_INCLUDE ? JINJA_CMETA_OP_INCLUDE :
        kind == JINJA_TEMPLATE_REFERENCE_IMPORT ? JINJA_CMETA_OP_IMPORT :
        kind == JINJA_TEMPLATE_REFERENCE_FROM ? JINJA_CMETA_OP_FROM : JINJA_CMETA_OP_EXTENDS;
    if (!jinja_program_emit(program,
        (JINJA_CMETA_INSTRUCTION){.opcode = opcode,
            .with_context = reference.with_context, .ignore_missing = reference.ignore_missing,
            .flag = (int)reference.name_count, .depth = *depth, .source_offset = source_offset},
        path, expressions)) return 0;
    /* Name/alias pairs are immutable instruction data, never executable code. */
    for (size_t i = 0u; i < reference.name_count; ++i) {
      const vstr name = vstr_from_buf(header_body.data + reference.names[i].name.offset,
          reference.names[i].name.length);
      const vstr imported_alias = vstr_from_buf(header_body.data + reference.names[i].alias.offset,
          reference.names[i].alias.length);
      if (!jinja_program_emit(program,
          (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_IMPORT_NAME,
              .source_offset = source_offset}, name, NULL) ||
          !jinja_program_emit(program,
          (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_IMPORT_NAME,
              .source_offset = source_offset}, imported_alias, NULL)) return 0;
    }
    return 1;
  }

  if (jinja_view_equal(statement, "else")) {
    if (*depth == 0u || frames[*depth - 1u].has_else ||
        (frames[*depth - 1u].kind != JINJA_BLOCK_IF && frames[*depth - 1u].kind != JINJA_BLOCK_FOR)) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                            "else has no matching block or is duplicated");
      return 0;
    }
    frame = &frames[*depth - 1u];
    frame->has_else = 1;
    if (frame->kind == JINJA_BLOCK_FOR) {
      if (!jinja_program_end_iteration(program, frame, *depth, source_offset)) return 0;
      return jinja_program_emit(program,
          (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_SCOPE_BEGIN,
                                    .target = frame->opener,
                                    .depth = *depth, .source_offset = source_offset},
          vstr_from_buf("", 0u), NULL);
    }
    if (!jinja_program_close_branch(program, frame, *depth, source_offset)) return 0;
    frame->pending_test = SIZE_MAX;
    return 1;
  }

  if (jinja_view_equal(statement, "endif") || jinja_view_equal(statement, "endfor")) {
    JINJA_BLOCK_KIND expected =
        jinja_view_equal(statement, "endif") ? JINJA_BLOCK_IF : JINJA_BLOCK_FOR;
    size_t end;
    if (*depth == 0u || frames[*depth - 1u].kind != expected) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                            "control block closer does not match opener");
      return 0;
    }
    frame = &frames[*depth - 1u];
    if (frame->kind == JINJA_BLOCK_FOR && !frame->has_else &&
        !jinja_program_end_iteration(program, frame, *depth, source_offset))
      return 0;
    if (frame->kind == JINJA_BLOCK_FOR && frame->has_else &&
        !jinja_program_emit(program,
            (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_SCOPE_END,
                                      .depth = *depth, .source_offset = source_offset},
            vstr_from_buf("", 0u), NULL)) return 0;
    end = JinjaInstructions_size(&program->instructions);
    if (frame->kind == JINJA_BLOCK_FOR) {
      JinjaInstructions_at(&program->instructions, frame->loop_next)->end = end;
      JinjaInstructions_at(&program->instructions, frame->opener)->end = end;
    } else {
      size_t exit = frame->exits;
      if (frame->pending_test != SIZE_MAX)
        JinjaInstructions_at(&program->instructions, frame->pending_test)->target = end;
      while (exit != SIZE_MAX) {
        JINJA_CMETA_INSTRUCTION *jump = JinjaInstructions_at(&program->instructions, exit);
        size_t previous = jump->target;
        jump->target = end;
        exit = previous;
      }
      *branch_count = frame->branch_start;
    }
    --*depth;
    return 1;
  }

  if (jinja_view_equal(keyword, "debug") || jinja_view_equal(keyword, "trans") ||
      jinja_view_equal(keyword, "pluralize")) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_UNSUPPORTED, source_offset,
                          "unsupported Jinja statement");
    return 0;
  }

  /* Custom extension tags use expression-call syntax, for example
   * {% audit(user, level) %}. Compiling the original source view keeps all
   * expression spans owned by the template and avoids a transient buffer. */
  {
    if (header_body.len < 2u || header_body.data[0] != '(' ||
        header_body.data[header_body.len - 1u] != ')') {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, source_offset,
                            "custom extension tags require call syntax");
      return 0;
    }
    JINJA_EXPRESSION_TREE tree;
    const JINJA_EXPRESSION_PARSE_STATUS parsed = jinja_expression_parse_tree(statement, &tree, NULL);
    JINJA_CMETA_STATUS status = parsed == JINJA_EXPRESSION_PARSE_OK
        ? jinja_compile_expression_tree(statement, &tree, frames, *depth, 0, expressions, &path)
        : parsed == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
        : parsed == JINJA_EXPRESSION_PARSE_CAPACITY ? JINJA_CMETA_ERR_CAPACITY
        : parsed == JINJA_EXPRESSION_PARSE_UNSUPPORTED ? JINJA_CMETA_ERR_UNSUPPORTED : JINJA_CMETA_ERR_SYNTAX;
    if (status == JINJA_CMETA_OK) {
      const size_t index = JinjaInstructions_size(&program->instructions);
      if (!jinja_program_emit(program,
          (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_EVALUATE,
                                    .depth = *depth, .source_offset = source_offset},
          vstr_from_buf("", 0u), NULL)) return 0;
      JinjaInstructions_at(&program->instructions, index)->expression = expressions->count - 1u;
      return 1;
    }
  }

  jinja_cmeta_error_set(error, JINJA_CMETA_ERR_UNSUPPORTED, source_offset,
                        "unsupported Jinja statement");
  if (error != NULL && statement.len != 0u)
    (void)snprintf(error->message, sizeof(error->message), "unsupported Jinja statement: %.*s",
                   (int)(statement.len > 48u ? 48u : statement.len), statement.data);
  return 0;
}

void jinja_cmeta_error_clear(JINJA_CMETA_ERROR *error) {
  if (error != NULL) *error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
}

void jinja_cmeta_error_set(JINJA_CMETA_ERROR *error, JINJA_CMETA_STATUS status, size_t offset,
                           const char *message) {
  if (error == NULL) return;
  error->status = status;
  error->offset = offset;
  (void)snprintf(error->message, sizeof(error->message), "%s",
                 message != NULL ? message : "Jinja CMeta error");
}


/* O(input bytes), bounded by the existing program byte budget. Physical source
 * newlines only: escaped strings are handled separately by the decoder. */
static int jinja_program_append_literal(JINJA_PROGRAM_BUILDER *program, tstr *text,
    vstr value, vstr newline) {
  size_t begin = 0u;
  for (size_t i = 0u; i <= value.len; ++i) {
    if (i != value.len && value.data[i] != '\r' && value.data[i] != '\n') continue;
    size_t count = i - begin;
    size_t current = tstr_len(*text);
    if (current > JINJA_CMETA_MAX_PROGRAM_BYTES || count > JINJA_CMETA_MAX_PROGRAM_BYTES - current ||
        (i != value.len && newline.len > JINJA_CMETA_MAX_PROGRAM_BYTES - current - count)) {
      program->status = JINJA_CMETA_ERR_CAPACITY;
      return 0;
    }
    if (!jinja_append(text, value.data + begin, count) ||
        (i != value.len && !jinja_append(text, newline.data, newline.len))) {
      program->status = JINJA_CMETA_ERR_OUT_OF_MEMORY;
      return 0;
    }
    if (i != value.len && value.data[i] == '\r' && i + 1u < value.len && value.data[i + 1u] == '\n') ++i;
    begin = i + 1u;
  }
  return 1;
}

/* Public syntax diagnostics identify the owning tag, while the private parser
 * retains its exact token offset. Only failed compilations need this projection. */
static size_t jinja_compile_error_tag(vstr source, const JINJA_TEMPLATE_DELIMITERS *delimiters,
                                     size_t failure) {
  JINJA_TEMPLATE_LEXER lexer;
  JINJA_TEMPLATE_TOKEN token;
  source = jinja_template_lexical_source(source, delimiters);
  jinja_template_lexer_init(&lexer, source.data, source.len, delimiters);
  for (;;) {
    int status = jinja_template_lexer_open_next(&lexer, &token);
    if (status <= 0) return failure;
    if (token.kind == JINJA_TEMPLATE_TOKEN_CHARACTER) continue;
    JINJA_TEMPLATE_NODE node;
    size_t offset = token.offset;
    if (jinja_template_scan_tag(source, &lexer, token, &node, &offset) != JINJA_EXPRESSION_PARSE_OK)
      return token.offset;
    if (failure >= node.source.offset && failure < node.source.offset + node.source.length)
      return node.source.offset;
    size_t end = node.source.offset + node.source.length;
    JINJA_EXPRESSION_SPAN rest = jinja_template_content_left(source,
        (JINJA_EXPRESSION_SPAN){end, source.len - end}, node.right_control,
        delimiters->trim_blocks && node.kind != JINJA_TEMPLATE_OUTPUT &&
        !node.line_statement && !node.line_comment);
    jinja_template_lexer_set_offset(&lexer, rest.offset);
  }
}

/* The parser keeps declarations, including unreachable and nested blocks.
 * O(tree nodes + B log B name comparisons), O(B) compile-local storage.
 * Stable sorting keeps each name's declarations in original source order. */
static JINJA_CMETA_STATUS jinja_validate_block_names(vstr source,
    const JINJA_TEMPLATE_TREE *tree, JINJA_CMETA_ERROR *error) {
  JinjaBlockNames names = {0};
  size_t count = 0u;
  size_t failure = SIZE_MAX;
  stl_status status;

  for (size_t i = 0u; i < tree->count; ++i) {
    if (tree->nodes[i].kind != JINJA_TEMPLATE_BLOCK) continue;
    if (count == JINJA_CMETA_MAX_INSTRUCTIONS || count == SIZE_MAX / sizeof(JINJA_BLOCK_NAME)) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, tree->nodes[i].source.offset,
          "block declarations exceed the compile workspace limit");
      return JINJA_CMETA_ERR_CAPACITY;
    }
    ++count;
  }
  if (count < 2u) return JINJA_CMETA_OK;

  status = JinjaBlockNames_init(&names, count);
  if (status == STL_OK) status = JinjaBlockNames_reserve(&names, count);
  for (size_t i = 0u; status == STL_OK && i < tree->count; ++i) {
    const JINJA_TEMPLATE_NODE *node = &tree->nodes[i];
    if (node->kind != JINJA_TEMPLATE_BLOCK) continue;
    JINJA_BLOCK_NAME name = {
      vstr_from_buf(source.data + node->name.offset, node->name.length), node->source.offset};
    status = JinjaBlockNames_push(&names, name);
  }
  if (status == STL_OK)
    status = stable_sort(JinjaBlockNames_data(&names), count, &jinja_block_name_type,
        count * sizeof(JINJA_BLOCK_NAME));
  if (status == STL_OK) {
    const JINJA_BLOCK_NAME *entries = JinjaBlockNames_data(&names);
    for (size_t i = 1u; i < count; ++i) {
      if (entries[i].source_offset < failure &&
          jinja_block_name_compare(&entries[i - 1u], &entries[i]) == 0)
        failure = entries[i].source_offset;
    }
  }
  JinjaBlockNames_destroy(&names);
  if (status != STL_OK) {
    JINJA_CMETA_STATUS result = status == STL_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
        : status == STL_CAPACITY_EXCEEDED ? JINJA_CMETA_ERR_CAPACITY : JINJA_CMETA_ERR_METADATA;
    jinja_cmeta_error_set(error, result, 0u, "unable to validate block declarations");
    return result;
  }
  if (failure != SIZE_MAX) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, failure, "block name defined twice");
    return JINJA_CMETA_ERR_SYNTAX;
  }
  return JINJA_CMETA_OK;
}

JINJA_CMETA_STATUS jinja_cmeta_compile_config(const JINJA_CMETA_COMPILE_OPTIONS *config,
    JINJA_TEMPLATE_DELIMITERS *delimiters, JINJA_CMETA_ERROR *error) {
  *delimiters = (JINJA_TEMPLATE_DELIMITERS){
    {config->variable_start_string, config->variable_end_string,
     config->block_start_string, config->block_end_string,
     config->comment_start_string, config->comment_end_string},
    config->line_statement_prefix, config->line_comment_prefix,
    config->trim_blocks, config->lstrip_blocks, config->keep_trailing_newline};
  JINJA_EXPRESSION_PARSE_STATUS config_status = jinja_template_validate_delimiters(delimiters);
  if (config_status != JINJA_EXPRESSION_PARSE_OK || !vstr_is_valid(config->newline_sequence) ||
      !(jinja_view_equal(config->newline_sequence, "\n") ||
        jinja_view_equal(config->newline_sequence, "\r\n") ||
        jinja_view_equal(config->newline_sequence, "\r"))) {
    JINJA_CMETA_STATUS status = config_status == JINJA_EXPRESSION_PARSE_CAPACITY
        ? JINJA_CMETA_ERR_CAPACITY : JINJA_CMETA_ERR_INVALID_ARGUMENT;
    jinja_cmeta_error_set(error, status, 0u, "invalid compile options");
    return status;
  }
  return JINJA_CMETA_OK;
}

static JINJA_CMETA_STATUS jinja_validate_registered_extensions(
    const JINJA_CMETA_TEMPLATE *templ, const JINJA_CMETA_ENV *env,
    const size_t source_offsets[JINJA_CMETA_MAX_COMPILED_EXPRESSIONS],
    JINJA_CMETA_ERROR *error) {
  for (size_t i = 0u; i < templ->expression_count; ++i) {
    const JINJA_CMETA_EXPRESSION_NODE *node = &templ->expressions[i];
    if (node->kind == JINJA_CMETA_EXPRESSION_HOST_FILTER &&
        (env == NULL || jinja_cmeta_env_find_filter(env, node->path) == NULL)) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_UNSUPPORTED, source_offsets[i],
          "filter is not registered in the template environment");
      return JINJA_CMETA_ERR_UNSUPPORTED;
    }
    if (node->kind == JINJA_CMETA_EXPRESSION_TEST && node->test == JINJA_CMETA_TEST_HOST &&
        (env == NULL || jinja_cmeta_env_find_test(env, node->path) == NULL)) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_UNSUPPORTED, source_offsets[i],
          "test is not registered in the template environment");
      return JINJA_CMETA_ERR_UNSUPPORTED;
    }
  }
  return JINJA_CMETA_OK;
}

JINJA_CMETA_TEMPLATE *jinja_cmeta_compile_with_environment(vstr source,
    const JINJA_CMETA_COMPILE_OPTIONS *options, const JINJA_CMETA_ENV *env,
    JINJA_CMETA_ERROR *error) {
  static const JINJA_CMETA_COMPILE_OPTIONS defaults = JINJA_CMETA_COMPILE_OPTIONS_INIT;
  const JINJA_CMETA_COMPILE_OPTIONS *config = options != NULL ? options : &defaults;
  JINJA_TEMPLATE_DELIMITERS delimiters;
  JINJA_TEMPLATE_NODE previous = {.kind = JINJA_TEMPLATE_OUTPUT};

  JINJA_BLOCK_FRAME frames[JINJA_CMETA_MAX_BLOCK_DEPTH];
  JINJA_EXPRESSION_BUILDER expressions = {0};
  JINJA_PROGRAM_BUILDER program = {0};
  JINJA_TEMPLATE_LEXER lexer;
  JINJA_TEMPLATE_TOKEN token;
  JINJA_CMETA_TEMPLATE *templ = NULL;
  tstr pending_text = NULL;
  const size_t original_source_bytes = source.len;
  size_t depth = 0u;
  size_t branch_count = 0u;
  size_t cursor = 0u;
  size_t parsed_statement = 0u;
  size_t invalid_utf8_offset;
  JINJA_CMETA_STATUS expression_status;
  stl_status instruction_status;

  jinja_cmeta_error_clear(error);
  if ((config->extensions & ~(JINJA_CMETA_EXTENSION_TAG_DO |
      JINJA_CMETA_EXTENSION_TAG_LOOP_CONTROLS |
      JINJA_CMETA_EXTENSION_TAG_CUSTOM |
      JINJA_CMETA_EXTENSION_TAG_I18N)) != 0u) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
        "compile options contain unsupported extension tags");
    return NULL;
  }
  if (!vstr_is_valid(source)) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_INVALID_ARGUMENT, 0u,
                          "template source is an invalid view");
    return NULL;
  }
  if (source.len > JINJA_CMETA_MAX_TEMPLATE_BYTES) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, 0u,
                          "template source exceeds the byte limit");
    return NULL;
  }
  invalid_utf8_offset = vstr_utf8_invalid_offset(source);
  if (invalid_utf8_offset != VSTR_NPOS) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, invalid_utf8_offset,
                          "template source contains malformed UTF-8");
    return NULL;
  }

  if (jinja_cmeta_compile_config(config, &delimiters, error) != JINJA_CMETA_OK) return NULL;
  expressions.newline_sequence = config->newline_sequence;
  {
    program.functions = (JINJA_FUNCTION_BUILDER *)calloc(1u, sizeof(*program.functions));
    if (program.functions == NULL) goto oom;
    program.functions->source = source;
    size_t failure = 0u;
    unsigned extensions = 0u;
    if (config->extensions == 0u || (config->extensions & JINJA_CMETA_EXTENSION_TAG_DO) != 0u)
      extensions |= JINJA_TEMPLATE_EXTENSION_DO;
    if (config->extensions == 0u ||
        (config->extensions & JINJA_CMETA_EXTENSION_TAG_LOOP_CONTROLS) != 0u)
      extensions |= JINJA_TEMPLATE_EXTENSION_LOOP_CONTROLS;
    if ((config->extensions & JINJA_CMETA_EXTENSION_TAG_CUSTOM) != 0u)
      extensions |= JINJA_TEMPLATE_EXTENSION_CUSTOM;
    if ((config->extensions & JINJA_CMETA_EXTENSION_TAG_I18N) != 0u)
      extensions |= JINJA_TEMPLATE_EXTENSION_I18N;
    JINJA_CMETA_STATUS status = jinja_function_parse_status(jinja_template_parse(&delimiters,
        extensions,
        source, &program.functions->tree, &failure));
    if (status != JINJA_CMETA_OK) {
      if (status == JINJA_CMETA_ERR_SYNTAX)
        failure = jinja_compile_error_tag(source, &delimiters, failure);
      jinja_cmeta_error_set(error, status, failure, "unable to parse native functions");
      goto fail;
    }
    if (jinja_validate_block_names(source, &program.functions->tree, error) != JINJA_CMETA_OK)
      goto fail;
  }
  if (!config->keep_trailing_newline && source.len != 0u) {
    if (source.data[source.len - 1u] == '\n') {
      --source.len;
      if (source.len != 0u && source.data[source.len - 1u] == '\r') --source.len;
    } else if (source.data[source.len - 1u] == '\r') --source.len;
  }
  pending_text = tstr_new();
  if (pending_text == NULL) goto oom;
  program.strings = tstr_new();
  if (program.strings == NULL) goto oom;
  instruction_status = JinjaInstructions_init(&program.instructions, JINJA_CMETA_MAX_INSTRUCTIONS);
  if (instruction_status != STL_OK) {
    program.status = instruction_status == STL_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
                     : instruction_status == STL_CAPACITY_EXCEEDED ? JINJA_CMETA_ERR_CAPACITY
                                                                   : JINJA_CMETA_ERR_METADATA;
    goto program_fail;
  }
  instruction_status = JinjaTranslations_init(&program.translations, JINJA_CMETA_MAX_INSTRUCTIONS);
  if (instruction_status != STL_OK) {
    program.status = instruction_status == STL_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
                     : instruction_status == STL_CAPACITY_EXCEEDED ? JINJA_CMETA_ERR_CAPACITY
                                                                   : JINJA_CMETA_ERR_METADATA;
    goto program_fail;
  }
  instruction_status = JinjaTranslationBindings_init(&program.translation_bindings,
      JINJA_CMETA_MAX_INSTRUCTIONS);
  if (instruction_status != STL_OK) {
    program.status = instruction_status == STL_OUT_OF_MEMORY ? JINJA_CMETA_ERR_OUT_OF_MEMORY
                     : instruction_status == STL_CAPACITY_EXCEEDED ? JINJA_CMETA_ERR_CAPACITY
                                                                   : JINJA_CMETA_ERR_METADATA;
    goto program_fail;
  }
  jinja_template_lexer_init(&lexer, source.data, source.len, &delimiters);
  for (;;) {
    int lex_status = jinja_template_lexer_open_next(&lexer, &token);
    size_t tag;
    size_t close;
    char kind;
    vstr body;
    vstr path;
    const JINJA_TEMPLATE_TREE *tree = &program.functions->tree;
    const JINJA_TEMPLATE_NODE *parsed_node = NULL;
    size_t statement_index = SIZE_MAX;
    JINJA_EXPRESSION_PARSE_STATUS interpolation_status;

    if (lex_status < 0) {
      jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, token.offset,
                            "template tokenization failed after UTF-8 admission");
      goto fail;
    }
    if (lex_status == 0) {
      JINJA_EXPRESSION_SPAN tail = jinja_template_content_left(source,
          (JINJA_EXPRESSION_SPAN){cursor, source.len - cursor}, previous.right_control,
          config->trim_blocks && previous.kind != JINJA_TEMPLATE_OUTPUT &&
          !previous.line_statement && !previous.line_comment);
      if (tail.length != 0u && !jinja_program_append_literal(&program, &pending_text,
          vstr_from_buf(source.data + tail.offset, tail.length), config->newline_sequence)) goto program_fail;
      if (!jinja_program_flush_text(&program, pending_text, depth, cursor)) goto program_fail;
      break;
    }
    if (token.kind == JINJA_TEMPLATE_TOKEN_CHARACTER) continue;
    JINJA_TEMPLATE_NODE scanned;
    size_t scan_failure = token.offset;
    JINJA_EXPRESSION_PARSE_STATUS scan_status =
        jinja_template_scan_tag(source, &lexer, token, &scanned, &scan_failure);
    tag = token.offset;
    if (scan_status != JINJA_EXPRESSION_PARSE_OK) {
      jinja_cmeta_error_set(error, scan_status == JINJA_EXPRESSION_PARSE_CAPACITY
          ? JINJA_CMETA_ERR_CAPACITY : JINJA_CMETA_ERR_SYNTAX, tag,
          "invalid or incomplete template tag");
      goto fail;
    }
    JINJA_EXPRESSION_SPAN literal = jinja_template_content_left(source,
        (JINJA_EXPRESSION_SPAN){cursor, tag - cursor}, previous.right_control,
        config->trim_blocks && previous.kind != JINJA_TEMPLATE_OUTPUT &&
        !previous.line_statement && !previous.line_comment);
    literal = jinja_template_content_right(source, literal, scanned.left_control,
        (config->lstrip_blocks || scanned.line_statement) && scanned.kind != JINJA_TEMPLATE_OUTPUT);
    if (literal.length != 0u && !jinja_program_append_literal(&program, &pending_text,
        vstr_from_buf(source.data + literal.offset, literal.length), config->newline_sequence)) goto program_fail;
    previous = scanned;
    /* Consumed whitespace must not participate in the next lexical match.
     * Keep cursor at the original tag end for source-span projection. */
    size_t tag_end = scanned.source.offset + scanned.source.length;
    JINJA_EXPRESSION_SPAN remaining = jinja_template_content_left(source,
        (JINJA_EXPRESSION_SPAN){tag_end, source.len - tag_end}, scanned.right_control,
        config->trim_blocks && scanned.kind != JINJA_TEMPLATE_OUTPUT &&
        !scanned.line_statement && !scanned.line_comment);
    jinja_template_lexer_set_offset(&lexer, remaining.offset);
    if (scanned.kind == JINJA_TEMPLATE_RAW) {
      JINJA_EXPRESSION_SPAN raw = jinja_template_content_left(source, scanned.header, scanned.raw_left_control, 0);
      raw = jinja_template_content_right(source, raw, scanned.raw_right_control, config->lstrip_blocks);
      if (!jinja_program_flush_text(&program, pending_text, depth, cursor)) goto program_fail;
      if (raw.length != 0u && !jinja_program_append_literal(&program, &pending_text,
          vstr_from_buf(source.data + raw.offset, raw.length), config->newline_sequence)) goto program_fail;
      if (!jinja_program_flush_text(&program, pending_text, depth, raw.offset)) goto program_fail;
      cursor = scanned.source.offset + scanned.source.length;
      continue;
    }
    kind = scanned.kind == JINJA_TEMPLATE_OUTPUT ? '{' :
        scanned.kind == JINJA_TEMPLATE_COMMENT ? '#' : '%';
    body = vstr_from_buf(source.data + scanned.header.offset, scanned.header.length);
    if (kind == '%') {
      /* The parsed header owns optional-colon syntax. Match source-ordered
       * statements in O(nodes) total, retaining the keyword from the scan. */
      while (parsed_statement < tree->count && tree->nodes[parsed_statement].source.offset < tag)
        ++parsed_statement;
      if (parsed_statement == tree->count || tree->nodes[parsed_statement].source.offset != tag) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_METADATA, tag,
            "statement scan does not match the parsed template");
        goto fail;
      }
      statement_index = parsed_statement++;
      parsed_node = &tree->nodes[statement_index];
      const JINJA_EXPRESSION_SPAN header = parsed_node->header;
      body.len = header.offset + header.length - scanned.header.offset;
      /* Empty payloads can leave whitespace between the keyword and colon. */
      body.len = jinja_template_content_right(source,
          (JINJA_EXPRESSION_SPAN){scanned.header.offset, body.len}, -1, 0).length;
    }
    close = scanned.source.offset + scanned.source.length;
    if (kind != '#' && !jinja_program_flush_text(&program, pending_text, depth, cursor))
      goto program_fail;
    if (kind == '{') {
      interpolation_status =
          jinja_parse_interpolation_value(jinja_trim(body), &expressions, frames, depth, &path);
      if (interpolation_status == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY) goto oom;
      if (interpolation_status == JINJA_EXPRESSION_PARSE_CAPACITY) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, tag,
                              "interpolation parser or expression limit exceeded");
        goto fail;
      }
      if (interpolation_status == JINJA_EXPRESSION_PARSE_INVALID) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, tag,
                              "invalid interpolation expression syntax");
        goto fail;
      }
      if (interpolation_status != JINJA_EXPRESSION_PARSE_OK ||
          !jinja_lower_alias_path(path, frames, depth, &path)) {
        jinja_cmeta_error_set(error, JINJA_CMETA_ERR_UNSUPPORTED, tag,
                              "interpolation requires a supported expression and optional "
                              "safe/escape filter");
        goto fail;
      }
      if (!jinja_program_emit(&program,
          (JINJA_CMETA_INSTRUCTION){.opcode = JINJA_CMETA_OP_OUTPUT,
                                    .depth = depth, .source_offset = tag},
          path, &expressions))
        goto program_fail;
    } else if (kind == '%' && parsed_node->kind == JINJA_TEMPLATE_TRANS) {
      if (!jinja_compile_translation(&program, source, tree, statement_index, tag,
              frames, depth, &expressions, env, error)) {
        if (program.status != JINJA_CMETA_OK) goto program_fail;
        goto fail;
      }
      if (parsed_node->match >= tree->count) {
        program.status = JINJA_CMETA_ERR_METADATA;
        goto program_fail;
      }
      const JINJA_TEMPLATE_NODE *end_node = &tree->nodes[parsed_node->match];
      close = end_node->source.offset + end_node->source.length;
      previous = *end_node;
      JINJA_EXPRESSION_SPAN after_translation = jinja_template_content_left(source,
          (JINJA_EXPRESSION_SPAN){close, source.len - close}, end_node->right_control,
          config->trim_blocks && end_node->kind != JINJA_TEMPLATE_OUTPUT &&
          !end_node->line_statement && !end_node->line_comment);
      jinja_template_lexer_set_offset(&lexer, after_translation.offset);
      cursor = close;
      continue;
    } else if (kind == '%') {
      if (!jinja_compile_statement(&program, body, tag, frames, &depth, &branch_count,
                                   &expressions, error)) {
        if (program.status != JINJA_CMETA_OK) goto program_fail;
        if (error == NULL || error->status == JINJA_CMETA_OK) goto oom;
        goto fail;
      }
    }
    cursor = close;
  }

  if (depth != 0u) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_SYNTAX, frames[depth - 1u].source_offset,
                          "unclosed Jinja control block");
    goto fail;
  }

  templ = jinja_cmeta_artifact_create(NULL, &expression_status);
  if (templ == NULL) goto oom;
  templ->source_bytes = original_source_bytes;
  memcpy(templ->newline_sequence, config->newline_sequence.data, config->newline_sequence.len);
  expression_status = jinja_template_copy_expressions(templ, &expressions);
  if (expression_status == JINJA_CMETA_ERR_CAPACITY) {
    jinja_cmeta_error_set(error, JINJA_CMETA_ERR_CAPACITY, 0u,
                          "compiled expression storage exceeds addressable memory");
    goto fail;
  }
  if (expression_status == JINJA_CMETA_ERR_OUT_OF_MEMORY) goto oom;
  if (expression_status == JINJA_CMETA_ERR_UNSUPPORTED) {
    jinja_cmeta_error_set(error, expression_status, 0u,
                          "string literal contains an unsupported escape");
    goto fail;
  }
  if (expression_status == JINJA_CMETA_ERR_SYNTAX) {
    jinja_cmeta_error_set(error, expression_status, 0u,
                          "string literal contains a malformed hex or Unicode escape");
    goto fail;
  }
  if (expression_status != JINJA_CMETA_OK) {
    jinja_cmeta_error_set(error, expression_status, 0u,
                          "unable to finalize compiled expression storage");
    goto fail;
  }
  templ->instruction_count = JinjaInstructions_size(&program.instructions);
  if (templ->instruction_count != 0u) {
    if (templ->instruction_count > SIZE_MAX / sizeof(*templ->instructions)) {
      program.status = JINJA_CMETA_ERR_CAPACITY;
      goto program_fail;
    }
    templ->instructions = (JINJA_CMETA_INSTRUCTION *)jinja_cmeta_artifact_allocate(templ, templ->instruction_count, sizeof(*templ->instructions));
    if (templ->instructions == NULL) goto oom;
    memcpy(templ->instructions, JinjaInstructions_data(&program.instructions),
           templ->instruction_count * sizeof(*templ->instructions));
  }
  templ->translation_count = JinjaTranslations_size(&program.translations);
  if (templ->translation_count != 0u) {
    templ->translations = (JINJA_CMETA_TRANSLATION *)jinja_cmeta_artifact_allocate(
        templ, templ->translation_count, sizeof(*templ->translations));
    if (templ->translations == NULL) goto oom;
    memcpy(templ->translations, JinjaTranslations_data(&program.translations),
           templ->translation_count * sizeof(*templ->translations));
  }
  templ->translation_binding_count = JinjaTranslationBindings_size(&program.translation_bindings);
  if (templ->translation_binding_count != 0u) {
    templ->translation_bindings = (JINJA_CMETA_TRANSLATION_BINDING *)jinja_cmeta_artifact_allocate(
        templ, templ->translation_binding_count, sizeof(*templ->translation_bindings));
    if (templ->translation_bindings == NULL) goto oom;
    memcpy(templ->translation_bindings, JinjaTranslationBindings_data(&program.translation_bindings),
           templ->translation_binding_count * sizeof(*templ->translation_bindings));
  }
  templ->program_strings = (char *)jinja_cmeta_artifact_allocate(templ, tstr_len(program.strings) + 1u, sizeof(char));
  if (templ->program_strings == NULL) goto oom;
  memcpy(templ->program_strings, program.strings, tstr_len(program.strings) + 1u);
  if (program.functions != NULL) {
    JINJA_FUNCTION_BUILDER *builder = program.functions;
    if (builder->count != 0u) {
      templ->functions = (JINJA_CMETA_FUNCTION *)jinja_cmeta_artifact_allocate(templ, builder->count, sizeof(*templ->functions));
      if (templ->functions == NULL) goto oom;
      memcpy(templ->functions, builder->functions, builder->count * sizeof(*templ->functions));
      templ->function_count = builder->count;
    }
    if (builder->parameter_count != 0u) {
      templ->parameters = (JINJA_CMETA_PARAMETER *)jinja_cmeta_artifact_allocate(templ, builder->parameter_count, sizeof(*templ->parameters));
      if (templ->parameters == NULL) goto oom;
      memcpy(templ->parameters, builder->parameters, builder->parameter_count * sizeof(*templ->parameters));
      templ->parameter_count = builder->parameter_count;
    }
    JINJA_CMETA_STATUS layout_status = jinja_cmeta_build_layout(builder->source, &builder->tree, templ, error);
    if (layout_status != JINJA_CMETA_OK) goto fail;
  }
  if (program.functions != NULL) jinja_template_tree_destroy(&program.functions->tree);
  free(program.functions);
  JinjaInstructions_destroy(&program.instructions);
  JinjaTranslations_destroy(&program.translations);
  JinjaTranslationBindings_destroy(&program.translation_bindings);
  tstr_free(program.strings);
  tstr_free(pending_text);
  if (jinja_validate_registered_extensions(templ, env, expressions.source_offsets, error) != JINJA_CMETA_OK) {
    jinja_cmeta_release(templ);
    return NULL;
  }
  return templ;

program_fail:
  jinja_cmeta_error_set(error, program.status, cursor, "unable to store native Jinja program");
  goto fail;
oom:
  jinja_cmeta_error_set(error, templ != NULL && templ->allocation_status != JINJA_CMETA_OK
                            ? templ->allocation_status : JINJA_CMETA_ERR_OUT_OF_MEMORY, cursor,
                        "unable to allocate compiled template storage");
fail:
  if (program.functions != NULL) jinja_template_tree_destroy(&program.functions->tree);
  free(program.functions);
  jinja_cmeta_release(templ);
  JinjaInstructions_destroy(&program.instructions);
  JinjaTranslations_destroy(&program.translations);
  JinjaTranslationBindings_destroy(&program.translation_bindings);
  tstr_free(program.strings);
  tstr_free(pending_text);
  return NULL;
}

JINJA_CMETA_TEMPLATE *jinja_cmeta_compile(vstr source,
    const JINJA_CMETA_COMPILE_OPTIONS *options, JINJA_CMETA_ERROR *error) {
  return jinja_cmeta_compile_with_environment(source, options, NULL, error);
}

void jinja_cmeta_release(JINJA_CMETA_TEMPLATE *templ) {
  if (templ != NULL && templ->references != 0u) {
    --templ->references;
    if (templ->references != 0u) return;
  }
  jinja_cmeta_artifact_destroy(templ);
}
