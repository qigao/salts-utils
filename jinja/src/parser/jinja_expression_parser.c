#include "jinja_expression_parser.h"
#include "jinja_parser_memory.h"
#include "jinja_expression_grammar_gen.h"
#include "jinja_float.h"
#include <salts_unicode.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

size_t JinjaExpressionParseWorkspaceSize(void);
void JinjaExpressionParseInit(void *parser);
void JinjaExpressionParseFinalize(void *parser);
void JinjaExpressionParse(void *parser, int token_kind, JINJA_EXPRESSION_TOKEN token,
                          JINJA_EXPRESSION_PARSE_CONTEXT *context);

#define JINJA_EXPRESSION_BINARY_RADIX UINT64_C(2)
#define JINJA_EXPRESSION_OCTAL_RADIX UINT64_C(8)
#define JINJA_EXPRESSION_DECIMAL_RADIX UINT64_C(10)
#define JINJA_EXPRESSION_HEXADECIMAL_RADIX UINT64_C(16)

typedef struct JINJA_EXPRESSION_TEST_NAME {
  const char *name;
  size_t length;
  JINJA_EXPRESSION_TEST_KIND kind;
} JINJA_EXPRESSION_TEST_NAME;

#define JINJA_EXPRESSION_TEST_NAME_ENTRY(name_value, kind_value)                                   \
  {name_value, sizeof(name_value) - 1u, kind_value}

static const JINJA_EXPRESSION_TEST_NAME jinja_expression_test_names[] = {
    JINJA_EXPRESSION_TEST_NAME_ENTRY("defined", JINJA_EXPRESSION_TEST_DEFINED),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("undefined", JINJA_EXPRESSION_TEST_UNDEFINED),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("none", JINJA_EXPRESSION_TEST_NONE),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("boolean", JINJA_EXPRESSION_TEST_BOOLEAN),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("true", JINJA_EXPRESSION_TEST_TRUE),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("false", JINJA_EXPRESSION_TEST_FALSE),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("integer", JINJA_EXPRESSION_TEST_INTEGER),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("float", JINJA_EXPRESSION_TEST_FLOAT),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("number", JINJA_EXPRESSION_TEST_NUMBER),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("string", JINJA_EXPRESSION_TEST_STRING),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("mapping", JINJA_EXPRESSION_TEST_MAPPING),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("sequence", JINJA_EXPRESSION_TEST_SEQUENCE),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("iterable", JINJA_EXPRESSION_TEST_ITERABLE),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("callable", JINJA_EXPRESSION_TEST_CALLABLE),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("escaped", JINJA_EXPRESSION_TEST_ESCAPED),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("filter", JINJA_EXPRESSION_TEST_FILTER),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("test", JINJA_EXPRESSION_TEST_TEST),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("odd", JINJA_EXPRESSION_TEST_ODD),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("even", JINJA_EXPRESSION_TEST_EVEN),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("divisibleby", JINJA_EXPRESSION_TEST_DIVISIBLEBY),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("sameas", JINJA_EXPRESSION_TEST_SAMEAS),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("eq", JINJA_EXPRESSION_TEST_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("==", JINJA_EXPRESSION_TEST_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("!=", JINJA_EXPRESSION_TEST_NOT_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("<", JINJA_EXPRESSION_TEST_LESS),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("<=", JINJA_EXPRESSION_TEST_LESS_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY(">", JINJA_EXPRESSION_TEST_GREATER),
    JINJA_EXPRESSION_TEST_NAME_ENTRY(">=", JINJA_EXPRESSION_TEST_GREATER_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("equalto", JINJA_EXPRESSION_TEST_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("ne", JINJA_EXPRESSION_TEST_NOT_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("lt", JINJA_EXPRESSION_TEST_LESS),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("lessthan", JINJA_EXPRESSION_TEST_LESS),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("le", JINJA_EXPRESSION_TEST_LESS_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("gt", JINJA_EXPRESSION_TEST_GREATER),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("greaterthan", JINJA_EXPRESSION_TEST_GREATER),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("ge", JINJA_EXPRESSION_TEST_GREATER_EQUAL),
    JINJA_EXPRESSION_TEST_NAME_ENTRY("in", JINJA_EXPRESSION_TEST_IN)};

#undef JINJA_EXPRESSION_TEST_NAME_ENTRY

int jinja_expression_test_from_token(JINJA_EXPRESSION_TOKEN token, JINJA_EXPRESSION_TEST *test) {
  size_t i;

  if (test == NULL || token.text == NULL || token.length == 0u) return 0;
  *test = (JINJA_EXPRESSION_TEST){JINJA_EXPRESSION_TEST_DEFINED, token.offset, 0, token.length};
  for (i = 0u; i < sizeof(jinja_expression_test_names) / sizeof(jinja_expression_test_names[0]);
       ++i) {
    if (token.length == jinja_expression_test_names[i].length &&
        memcmp(token.text, jinja_expression_test_names[i].name, token.length) == 0) {
      test->kind = jinja_expression_test_names[i].kind;
      test->supported = 1;
      return 1;
    }
  }
  return 1;
}

int jinja_expression_parse_integer_token(JINJA_EXPRESSION_TOKEN token, int64_t *value) {
  const uint64_t negative_limit = (uint64_t)INT64_MAX + UINT64_C(1);
  uint64_t limit = (uint64_t)INT64_MAX;
  uint64_t magnitude = 0u;
  uint64_t radix = JINJA_EXPRESSION_DECIMAL_RADIX;
  size_t cursor = 0u;
  int negative = 0;

  if (value == NULL || token.text == NULL || token.length == 0u) return 0;
  if (token.text[cursor] == '+' || token.text[cursor] == '-') {
    negative = token.text[cursor] == '-';
    limit = negative ? negative_limit : (uint64_t)INT64_MAX;
    ++cursor;
  }
  if (cursor == token.length) return 0;
  if (token.length - cursor >= 2u && token.text[cursor] == '0') {
    const char prefix = token.text[cursor + 1u];
    if (prefix == 'b' || prefix == 'B') {
      radix = JINJA_EXPRESSION_BINARY_RADIX;
      cursor += 2u;
    } else if (prefix == 'o' || prefix == 'O') {
      radix = JINJA_EXPRESSION_OCTAL_RADIX;
      cursor += 2u;
    } else if (prefix == 'x' || prefix == 'X') {
      radix = JINJA_EXPRESSION_HEXADECIMAL_RADIX;
      cursor += 2u;
    }
  }
  if (cursor == token.length) return 0;

  for (; cursor < token.length; ++cursor) {
    uint64_t digit;
    if (token.text[cursor] == '_') continue;
    if (token.text[cursor] >= '0' && token.text[cursor] <= '9') {
      digit = (uint64_t)(token.text[cursor] - '0');
    } else if (token.text[cursor] >= 'a' && token.text[cursor] <= 'f') {
      digit = UINT64_C(10) + (uint64_t)(token.text[cursor] - 'a');
    } else if (token.text[cursor] >= 'A' && token.text[cursor] <= 'F') {
      digit = UINT64_C(10) + (uint64_t)(token.text[cursor] - 'A');
    } else {
      salts_unicode_scalar scalar;
      uint32_t decimal;
      size_t next = cursor;
      if (salts_unicode_utf8_next(vstr_from_buf(token.text, token.length), &next, &scalar) != SALTS_UNICODE_OK ||
          salts_unicode_decimal_value(scalar.value, &decimal) != SALTS_UNICODE_OK)
        return 0;
      digit = decimal;
      cursor = next - 1u;
    }
    if (digit >= radix || magnitude > (limit - digit) / radix) return 0;
    magnitude = magnitude * radix + digit;
  }

  if (!negative) {
    *value = (int64_t)magnitude;
  } else if (magnitude == negative_limit) {
    *value = INT64_MIN;
  } else {
    *value = -(int64_t)magnitude;
  }
  return 1;
}

int jinja_expression_parse_float_token(JINJA_EXPRESSION_TOKEN token, double *value) {
  if (value == NULL || token.text == NULL || token.length == 0u) return 0;
  return jinja_float_parse(token.text, token.length, value);
}

static int jinja_expression_numeric_value(const JINJA_EXPRESSION_CONDITION *condition,
                                          int64_t *value) {
  if (condition == NULL || value == NULL) return 0;
  if (condition->kind == JINJA_EXPRESSION_CONDITION_INTEGER) {
    *value = condition->integer;
    return 1;
  }
  if (condition->kind == JINJA_EXPRESSION_CONDITION_BOOL) {
    *value = condition->boolean != 0 ? INT64_C(1) : INT64_C(0);
    return 1;
  }
  return 0;
}

int jinja_expression_compare_numeric(const JINJA_EXPRESSION_CONDITION *left,
                                     JINJA_EXPRESSION_COMPARISON_KIND comparison,
                                     const JINJA_EXPRESSION_CONDITION *right, int *result) {
  int64_t left_value;
  int64_t right_value;

  if (result == NULL || !jinja_expression_numeric_value(left, &left_value) ||
      !jinja_expression_numeric_value(right, &right_value))
    return 0;

  switch (comparison) {
  case JINJA_EXPRESSION_COMPARISON_EQUAL:
    *result = left_value == right_value;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_NOT_EQUAL:
    *result = left_value != right_value;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_LESS:
    *result = left_value < right_value;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_LESS_EQUAL:
    *result = left_value <= right_value;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_GREATER:
    *result = left_value > right_value;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_GREATER_EQUAL:
    *result = left_value >= right_value;
    return 1;
  case JINJA_EXPRESSION_COMPARISON_IN:
  case JINJA_EXPRESSION_COMPARISON_NOT_IN:
    return 0;
  }
  return 0;
}

int jinja_expression_condition_operand(const JINJA_EXPRESSION_CONDITION *condition,
                                       JINJA_EXPRESSION_OPERAND *operand) {
  if (condition == NULL || operand == NULL) return 0;
  *operand = (JINJA_EXPRESSION_OPERAND){0};
  switch (condition->kind) {
  case JINJA_EXPRESSION_CONDITION_PATH:
    if (condition->unary_not_count != 0u) return 0;
    operand->kind = JINJA_EXPRESSION_OPERAND_PATH;
    operand->span = condition->path;
    return 1;
  case JINJA_EXPRESSION_CONDITION_BOOL:
    operand->kind = JINJA_EXPRESSION_OPERAND_BOOL;
    operand->boolean = condition->boolean != 0;
    return 1;
  case JINJA_EXPRESSION_CONDITION_INTEGER:
    operand->kind = JINJA_EXPRESSION_OPERAND_INTEGER;
    operand->integer = condition->integer;
    return 1;
  case JINJA_EXPRESSION_CONDITION_FLOAT:
    operand->kind = JINJA_EXPRESSION_OPERAND_FLOAT;
    operand->floating = condition->floating;
    return 1;
  case JINJA_EXPRESSION_CONDITION_STRING:
    if (condition->unary_not_count != 0u) return 0;
    operand->kind = JINJA_EXPRESSION_OPERAND_STRING;
    operand->span = condition->string;
    return 1;
  default:
    return 0;
  }
}

int jinja_expression_store_condition(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                     JINJA_EXPRESSION_CONDITION condition, size_t *index) {
  if (context == NULL || index == NULL || context->failed) return 0;
  if (context->tree.count == JINJA_EXPRESSION_MAX_NODES) {
    context->failed = 1;
    context->capacity_exceeded = 1;
    return 0;
  }
  *index = context->tree.count;
  context->tree.nodes[context->tree.count++] = condition;
  return 1;
}

int jinja_expression_make_unary_arithmetic(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                           JINJA_EXPRESSION_ARITHMETIC_KIND arithmetic,
                                           JINJA_EXPRESSION_CONDITION operand,
                                           JINJA_EXPRESSION_CONDITION *condition) {
  size_t operand_condition;

  if (context == NULL || condition == NULL || context->failed ||
      (arithmetic != JINJA_EXPRESSION_ARITHMETIC_POSITIVE &&
       arithmetic != JINJA_EXPRESSION_ARITHMETIC_NEGATE))
    return 0;
  if (!jinja_expression_store_condition(context, operand, &operand_condition)) return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_UNARY_ARITHMETIC;
  condition->arithmetic = arithmetic;
  condition->left_condition = operand_condition;
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_make_binary_arithmetic(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                            JINJA_EXPRESSION_ARITHMETIC_KIND arithmetic,
                                            JINJA_EXPRESSION_CONDITION left,
                                            JINJA_EXPRESSION_CONDITION right,
                                            JINJA_EXPRESSION_CONDITION *condition) {
  size_t left_condition;
  size_t right_condition;

  if (context == NULL || condition == NULL || context->failed ||
      arithmetic < JINJA_EXPRESSION_ARITHMETIC_ADD ||
      arithmetic > JINJA_EXPRESSION_ARITHMETIC_POWER)
    return 0;
  if (!jinja_expression_store_condition(context, left, &left_condition) ||
      !jinja_expression_store_condition(context, right, &right_condition))
    return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_BINARY_ARITHMETIC;
  condition->arithmetic = arithmetic;
  condition->left_condition = left_condition;
  condition->right_condition = right_condition;
  condition->truth_marker = '#';
  return 1;
}

static int jinja_expression_make_binary_value(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                              JINJA_EXPRESSION_CONDITION_KIND kind,
                                              JINJA_EXPRESSION_CONDITION base,
                                              JINJA_EXPRESSION_CONDITION key,
                                              JINJA_EXPRESSION_CONDITION *condition) {
  size_t base_condition;
  size_t key_condition;

  if (context == NULL || condition == NULL || context->failed) return 0;
  if (!jinja_expression_store_condition(context, base, &base_condition) ||
      !jinja_expression_store_condition(context, key, &key_condition))
    return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = kind;
  condition->left_condition = base_condition;
  condition->right_condition = key_condition;
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_make_item_lookup(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                      JINJA_EXPRESSION_CONDITION base,
                                      JINJA_EXPRESSION_CONDITION key,
                                      JINJA_EXPRESSION_CONDITION *condition) {
  return jinja_expression_make_binary_value(context, JINJA_EXPRESSION_CONDITION_ITEM_LOOKUP, base,
                                            key, condition);
}

int jinja_expression_make_concat(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                 JINJA_EXPRESSION_CONDITION left, JINJA_EXPRESSION_CONDITION right,
                                 JINJA_EXPRESSION_CONDITION *condition) {
  return jinja_expression_make_binary_value(context, JINJA_EXPRESSION_CONDITION_CONCAT, left, right,
                                            condition);
}

int jinja_expression_make_slice_value(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                       JINJA_EXPRESSION_SLICE slice,
                                       JINJA_EXPRESSION_CONDITION *condition) {
  JINJA_EXPRESSION_COLLECTION items = {3u, SIZE_MAX};
  if (!jinja_expression_store_collection_item(context, slice.step, items.first_item,
                                              &items.first_item) ||
      !jinja_expression_store_collection_item(context, slice.stop, items.first_item,
                                              &items.first_item) ||
      !jinja_expression_store_collection_item(context, slice.start, items.first_item,
                                              &items.first_item) ||
      !jinja_expression_make_tuple(context, items, condition))
    return 0;
  condition->kind = JINJA_EXPRESSION_CONDITION_SLICE;
  return 1;
}

int jinja_expression_make_subscript_lookup(JINJA_EXPRESSION_PARSE_CONTEXT *context,
    JINJA_EXPRESSION_CONDITION base, JINJA_EXPRESSION_CONDITION key,
    JINJA_EXPRESSION_CONDITION *condition) {
  const int slice = key.kind == JINJA_EXPRESSION_CONDITION_SLICE;
  if (slice) key.kind = JINJA_EXPRESSION_CONDITION_TUPLE;
  if (!jinja_expression_make_item_lookup(context, base, key, condition)) return 0;
  if (slice) condition->kind = JINJA_EXPRESSION_CONDITION_SLICE_LOOKUP;
  return 1;
}

int jinja_expression_make_integer_item_lookup(JINJA_EXPRESSION_PARSE_CONTEXT *context,
    JINJA_EXPRESSION_CONDITION base, JINJA_EXPRESSION_TOKEN index,
    JINJA_EXPRESSION_CONDITION *condition) {
  JINJA_EXPRESSION_CONDITION key = {0};
  key.kind = JINJA_EXPRESSION_CONDITION_INTEGER;
  key.truth_marker = '#';
  if (index.length == 0u || index.text[0] == '+' || index.text[0] == '-') {
    context->failed = 1;
    return 0;
  }
  if (!jinja_expression_parse_integer_token(index, &key.integer)) {
    context->failed = context->capacity_exceeded = 1;
    return 0;
  }
  return jinja_expression_make_item_lookup(context, base, key, condition);
}

int jinja_expression_make_filter(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                 JINJA_EXPRESSION_CONDITION base, JINJA_EXPRESSION_TOKEN name,
                                 JINJA_EXPRESSION_CONDITION *condition) {
  size_t operand;
  if (context == NULL || condition == NULL || context->failed || name.length == 0u) return 0;
  if (!jinja_expression_store_condition(context, base, &operand)) return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_FILTER;
  condition->first_collection_item = SIZE_MAX;
  condition->left_condition = operand;
  condition->path = (JINJA_EXPRESSION_SPAN){name.offset, name.length};
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_make_attribute_lookup(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                           JINJA_EXPRESSION_CONDITION base,
                                           JINJA_EXPRESSION_TOKEN attribute,
                                           JINJA_EXPRESSION_CONDITION *condition) {
  size_t attribute_end;
  size_t base_condition;

  if (context == NULL || condition == NULL || context->failed || attribute.length == 0u) return 0;
  if (attribute.offset > SIZE_MAX - attribute.length) {
    context->failed = 1;
    context->capacity_exceeded = 1;
    return 0;
  }
  attribute_end = attribute.offset + attribute.length;
  if (base.kind == JINJA_EXPRESSION_CONDITION_PATH && !base.grouped) {
    if (attribute_end < base.path.offset) {
      context->failed = 1;
      return 0;
    }
    base.path.length = attribute_end - base.path.offset;
    *condition = base;
    return 1;
  }
  if (!jinja_expression_store_condition(context, base, &base_condition)) return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_ATTRIBUTE_LOOKUP;
  condition->path = (JINJA_EXPRESSION_SPAN){.offset = attribute.offset, .length = attribute.length};
  condition->left_condition = base_condition;
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_make_call(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                               JINJA_EXPRESSION_CONDITION callable,
                               JINJA_EXPRESSION_COLLECTION arguments,
                               JINJA_EXPRESSION_CONDITION *condition) {
  size_t callable_condition;

  if (context == NULL || condition == NULL || context->failed ||
      (arguments.count == 0u) != (arguments.first_item == SIZE_MAX) ||
      (arguments.count != 0u && arguments.first_item >= context->tree.collection_item_count))
    return 0;
  if (!jinja_expression_store_condition(context, callable, &callable_condition)) return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_CALL;
  condition->left_condition = callable_condition;
  condition->first_collection_item = arguments.first_item;
  condition->collection_item_count = arguments.count;
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_store_collection_item(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                           JINJA_EXPRESSION_CONDITION value, size_t next,
                                           size_t *index) {
  size_t value_condition;

  if (context == NULL || index == NULL || context->failed) return 0;
  if (context->tree.collection_item_count == JINJA_EXPRESSION_MAX_NODES) {
    context->failed = 1;
    context->capacity_exceeded = 1;
    return 0;
  }
  if (next != SIZE_MAX && next >= context->tree.collection_item_count) {
    context->failed = 1;
    return 0;
  }
  if (!jinja_expression_store_condition(context, value, &value_condition)) return 0;
  *index = context->tree.collection_item_count;
  context->tree.collection_items[context->tree.collection_item_count++] =
      (JINJA_EXPRESSION_COLLECTION_ITEM){.value_condition = value_condition, .next = next};
  return 1;
}

int jinja_expression_make_list(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                               JINJA_EXPRESSION_COLLECTION items,
                               JINJA_EXPRESSION_CONDITION *condition) {
  if (context == NULL || condition == NULL || context->failed ||
      (items.count == 0u) != (items.first_item == SIZE_MAX) ||
      (items.count != 0u && items.first_item >= context->tree.collection_item_count))
    return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_LIST;
  condition->first_collection_item = items.first_item;
  condition->collection_item_count = items.count;
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_make_tuple(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                JINJA_EXPRESSION_COLLECTION items,
                                JINJA_EXPRESSION_CONDITION *condition) {
  if (!jinja_expression_make_list(context, items, condition)) return 0;
  condition->kind = JINJA_EXPRESSION_CONDITION_TUPLE;
  return 1;
}

int jinja_expression_store_dict_item(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                     JINJA_EXPRESSION_CONDITION key,
                                     JINJA_EXPRESSION_CONDITION value, size_t next, size_t *index) {
  size_t key_condition;
  size_t value_condition;

  if (context == NULL || index == NULL || context->failed) return 0;
  if (context->tree.dict_item_count == JINJA_EXPRESSION_MAX_NODES) {
    context->failed = 1;
    context->capacity_exceeded = 1;
    return 0;
  }
  if (next != SIZE_MAX && next >= context->tree.dict_item_count) {
    context->failed = 1;
    return 0;
  }
  if (!jinja_expression_store_condition(context, key, &key_condition) ||
      !jinja_expression_store_condition(context, value, &value_condition))
    return 0;
  *index = context->tree.dict_item_count;
  context->tree.dict_items[context->tree.dict_item_count++] =
      (JINJA_EXPRESSION_DICT_ITEM){key_condition, value_condition, next};
  return 1;
}

int jinja_expression_make_dict(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                               JINJA_EXPRESSION_COLLECTION items,
                               JINJA_EXPRESSION_CONDITION *condition) {
  if (context == NULL || condition == NULL || context->failed ||
      (items.count == 0u) != (items.first_item == SIZE_MAX) ||
      (items.count != 0u && items.first_item >= context->tree.dict_item_count))
    return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_DICT;
  condition->first_collection_item = items.first_item;
  condition->collection_item_count = items.count;
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_make_conditional(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                      JINJA_EXPRESSION_CONDITION true_value,
                                      JINJA_EXPRESSION_CONDITION test,
                                      const JINJA_EXPRESSION_CONDITIONAL_ELSE *false_value,
                                      JINJA_EXPRESSION_CONDITION *condition) {
  size_t true_condition;
  size_t test_condition;
  size_t false_condition = 0u;

  if (context == NULL || false_value == NULL || condition == NULL || context->failed) return 0;
  if (!jinja_expression_store_condition(context, true_value, &true_condition) ||
      !jinja_expression_store_condition(context, test, &test_condition) ||
      (false_value->has_else &&
       !jinja_expression_store_condition(context, false_value->condition, &false_condition)))
    return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_CONDITIONAL;
  condition->left_condition = true_condition;
  condition->right_condition = false_condition;
  condition->test_condition = test_condition;
  condition->has_else = false_value->has_else != 0;
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_make_test(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                               JINJA_EXPRESSION_CONDITION operand, JINJA_EXPRESSION_TEST_KIND test,
                               int negated, JINJA_EXPRESSION_CONDITION *condition) {
  size_t operand_condition;

  if (context == NULL || condition == NULL || context->failed ||
      test < JINJA_EXPRESSION_TEST_DEFINED || test > JINJA_EXPRESSION_TEST_IN)
    return 0;
  if (operand.kind == JINJA_EXPRESSION_CONDITION_TEST && !operand.grouped &&
      !operand.test_arguments_supplied) {
    context->failed = 1;
    return 0;
  }
  if (!jinja_expression_store_condition(context, operand, &operand_condition)) return 0;
  *condition = (JINJA_EXPRESSION_CONDITION){0};
  condition->kind = JINJA_EXPRESSION_CONDITION_TEST;
  condition->test = test;
  condition->left_condition = operand_condition;
  condition->unary_not_count = negated != 0 ? 1u : 0u;
  condition->truth_marker = '#';
  return 1;
}

int jinja_expression_store_comparison_step(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                           JINJA_EXPRESSION_COMPARISON_KIND comparison,
                                           JINJA_EXPRESSION_CONDITION operand, size_t next,
                                           size_t *index) {
  size_t operand_condition;

  if (context == NULL || index == NULL || context->failed) return 0;
  if (context->tree.comparison_step_count == JINJA_EXPRESSION_MAX_NODES) {
    context->failed = 1;
    context->capacity_exceeded = 1;
    return 0;
  }
  if (!jinja_expression_store_condition(context, operand, &operand_condition)) return 0;
  *index = context->tree.comparison_step_count;
  context->tree.comparison_steps[context->tree.comparison_step_count++] =
      (JINJA_EXPRESSION_COMPARISON_STEP){comparison, operand_condition, next};
  return 1;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_parser_allocate(const stl_allocator *allocator,
    size_t bytes, void **out) {
  *out = NULL;
  if (allocator != NULL && (allocator->allocate == NULL || allocator->deallocate == NULL))
    return JINJA_EXPRESSION_PARSE_INVALID;
  if (allocator == NULL) {
    *out = malloc(bytes);
    return *out != NULL ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY;
  }
  switch (allocator->allocate(allocator->context, bytes, out)) {
    case STL_OK: return JINJA_EXPRESSION_PARSE_OK;
    case STL_CAPACITY_EXCEEDED: return JINJA_EXPRESSION_PARSE_CAPACITY;
    case STL_OUT_OF_MEMORY: return JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY;
    default: return JINJA_EXPRESSION_PARSE_INVALID;
  }
}

void jinja_parser_deallocate(const stl_allocator *allocator, void *data, size_t bytes) {
  if (allocator != NULL) allocator->deallocate(allocator->context, data, bytes);
  else free(data);
}

static JINJA_EXPRESSION_PARSE_STATUS
jinja_expression_parse_tree_mode_allocated(vstr expression, JINJA_EXPRESSION_TREE *tree,
                                  size_t *error_offset, int assignment_target,
                                  size_t capture_offset, int inline_filter,
                                  const stl_allocator *allocator) {
  JINJA_EXPRESSION_PARSE_CONTEXT context = {0};
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  char *terminated_expression;
  void *parser;
  int token_kind = 0;
  int lex_status;
  int syntax_error = 0;
  int unsupported = 0;
  size_t parentheses = 0u;
  int previous_kind = 0;

  if (tree == NULL || !vstr_is_valid(expression)) return JINJA_EXPRESSION_PARSE_INVALID;
  if (allocator != NULL && (allocator->allocate == NULL || allocator->deallocate == NULL))
    return JINJA_EXPRESSION_PARSE_INVALID;
  if (expression.len == SIZE_MAX) return JINJA_EXPRESSION_PARSE_CAPACITY;

  void *source_storage = NULL;
  JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_allocate(allocator, expression.len + 1u, &source_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  terminated_expression = (char *)source_storage;
  if (expression.len != 0u) memcpy(terminated_expression, expression.data, expression.len);
  terminated_expression[expression.len] = '\0';

  const size_t parser_bytes = JinjaExpressionParseWorkspaceSize();
  allocation_status = jinja_parser_allocate(allocator, parser_bytes, &parser);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) {
    jinja_parser_deallocate(allocator, terminated_expression, expression.len + 1u);
    return allocation_status;
  }
  JinjaExpressionParseInit(parser);

  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated_expression, expression.len));
  if (capture_offset != SIZE_MAX) {
    JINJA_EXPRESSION_TOKEN captured = {0};
    captured.text = terminated_expression;
    JinjaExpressionParse(parser, JINJA_EXPRESSION_TOKEN_IDENTIFIER, captured, &context);
    if (inline_filter) JinjaExpressionParse(parser, JINJA_EXPRESSION_TOKEN_PIPE, captured, &context);
    lexer.cursor = terminated_expression + capture_offset;
    lexer.expects_operand = inline_filter;
    if (inline_filter) lexer.filter_state = JINJA_EXPRESSION_TEST_LEX_NAME;
  }
  while ((lex_status = jinja_expression_lexer_next(&lexer, &token_kind, &token)) > 0) {
    /* Upstream's outer assignment tuple parses primary names, while grouping
     * re-enters expression syntax (so bare not is assignable, (not) is not). */
    if (assignment_target && parentheses == 0u && token_kind == JINJA_EXPRESSION_TOKEN_NOT) {
      token_kind = JINJA_EXPRESSION_TOKEN_IDENTIFIER;
      token.length = sizeof("not") - 1u;
    }
    /* Attribute names are not literals/operators, in reads as well as writes.
     * Restore operand state too: a following sign or in belongs to the expression. */
    if (previous_kind == JINJA_EXPRESSION_TOKEN_DOT &&
        (token_kind == JINJA_EXPRESSION_TOKEN_TRUE || token_kind == JINJA_EXPRESSION_TOKEN_FALSE ||
         token_kind == JINJA_EXPRESSION_TOKEN_NONE || token_kind == JINJA_EXPRESSION_TOKEN_NOT)) {
      if (token_kind == JINJA_EXPRESSION_TOKEN_NOT) token.length = sizeof("not") - 1u;
      token_kind = JINJA_EXPRESSION_TOKEN_IDENTIFIER;
      lexer.expects_operand = 0;
      lexer.expects_membership_in = 0;
    }
    previous_kind = token_kind;
    if (token_kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN) ++parentheses;
    else if (token_kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN && parentheses != 0u) --parentheses;
    JinjaExpressionParse(parser, token_kind, token, &context);
    if (context.failed) break;
  }
  if (lex_status < 0) {
    context.error_offset = token.offset;
    if (lex_status == JINJA_EXPRESSION_LEX_SYNTAX) syntax_error = 1;
    else unsupported = 1;
  } else if (!context.failed) {
    JINJA_EXPRESSION_TOKEN eof_token = {0};
    eof_token.offset = expression.len;
    JinjaExpressionParse(parser, 0, eof_token, &context);
  }
  JinjaExpressionParseFinalize(parser);
  jinja_parser_deallocate(allocator, parser, parser_bytes);
  jinja_parser_deallocate(allocator, terminated_expression, expression.len + 1u);

  if (error_offset != NULL) *error_offset = context.error_offset;
  if (context.capacity_exceeded) return JINJA_EXPRESSION_PARSE_CAPACITY;
  if (syntax_error || context.failed) return JINJA_EXPRESSION_PARSE_INVALID;
  if (unsupported) return JINJA_EXPRESSION_PARSE_UNSUPPORTED;
  if (!context.accepted) return JINJA_EXPRESSION_PARSE_INVALID;

  if (capture_offset != SIZE_MAX) {
    size_t base = context.tree.root;
    while (context.tree.nodes[base].kind == JINJA_EXPRESSION_CONDITION_FILTER) {
      size_t next = context.tree.nodes[base].left_condition;
      if (next >= base) return JINJA_EXPRESSION_PARSE_INVALID;
      base = next;
    }
    if (context.tree.nodes[base].kind != JINJA_EXPRESSION_CONDITION_PATH ||
        context.tree.nodes[base].path.length != 0u || context.tree.nodes[base].unary_not_count != 0u)
      return JINJA_EXPRESSION_PARSE_INVALID;
    context.tree.nodes[base].kind = JINJA_EXPRESSION_CONDITION_CAPTURE;
  }

  *tree = context.tree;
  return JINJA_EXPRESSION_PARSE_OK;
}

JINJA_EXPRESSION_PARSE_STATUS
jinja_expression_parse_tree(vstr expression, JINJA_EXPRESSION_TREE *tree, size_t *error_offset) {
  return jinja_expression_parse_tree_allocated(expression, tree, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_tree_allocated(vstr expression,
    JINJA_EXPRESSION_TREE *tree, size_t *error_offset, const stl_allocator *allocator) {
  return jinja_expression_parse_tree_mode_allocated(expression, tree, error_offset,
      0, SIZE_MAX, 0, allocator);
}


JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_filter_block(
    vstr expression, JINJA_EXPRESSION_TREE *tree, size_t *error_offset) {
  return jinja_expression_parse_filter_block_allocated(expression, tree, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_filter_block_allocated(
    vstr expression, JINJA_EXPRESSION_TREE *tree, size_t *error_offset, const stl_allocator *allocator) {
  return jinja_expression_parse_tree_mode_allocated(expression, tree, error_offset, 0, 0u, 1, allocator);
}

JINJA_EXPRESSION_PARSE_STATUS
jinja_expression_parse_condition(vstr expression, JINJA_EXPRESSION_CONDITION *condition,
                                 size_t *error_offset) {
  return jinja_expression_parse_condition_allocated(expression, condition, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS
jinja_expression_parse_condition_allocated(vstr expression, JINJA_EXPRESSION_CONDITION *condition,
                                 size_t *error_offset, const stl_allocator *allocator) {
  JINJA_EXPRESSION_TREE tree;
  JINJA_EXPRESSION_PARSE_STATUS status;

  if (condition == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  status = jinja_expression_parse_tree_allocated(expression, &tree, error_offset, allocator);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  if (tree.count == 0u || tree.root >= tree.count) return JINJA_EXPRESSION_PARSE_INVALID;
  *condition = tree.nodes[tree.root];
  return JINJA_EXPRESSION_PARSE_OK;
}

/* O(header bytes) per namespace target, with at most MAX_NODES targets.
 * Reuse lexer boundaries so whitespace is excluded from both borrowed names.
 * Parenthesized expressions do not admit namespace assignment in Jinja. */
static int jinja_expression_namespace_target(vstr input, JINJA_EXPRESSION_CONDITION *node) {
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN owner = {0}, dot = {0}, attribute = {0};
  size_t parentheses = 0u;
  int kind;
  if (node->grouped) return 0;
  jinja_expression_lexer_init(&lexer, input);
  for (;;) {
    if (jinja_expression_lexer_next(&lexer, &kind, &owner) != JINJA_EXPRESSION_LEX_TOKEN)
      return 0;
    if (owner.offset == node->path.offset) break;
    if (owner.offset > node->path.offset) return 0;
    if (kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN) ++parentheses;
    else if (kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN) {
      if (parentheses == 0u) return 0;
      --parentheses;
    }
  }
  if (parentheses != 0u) return 0;
  if (kind == JINJA_EXPRESSION_TOKEN_NOT) owner.length = sizeof("not") - 1u;
  if (jinja_expression_lexer_next(&lexer, &kind, &dot) != JINJA_EXPRESSION_LEX_TOKEN ||
      kind != JINJA_EXPRESSION_TOKEN_DOT ||
      jinja_expression_lexer_next(&lexer, &kind, &attribute) != JINJA_EXPRESSION_LEX_TOKEN)
    return 0;
  if (kind == JINJA_EXPRESSION_TOKEN_NOT) attribute.length = sizeof("not") - 1u;
  if (attribute.offset + attribute.length != node->path.offset + node->path.length)
    return 0;
  node->kind = JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET;
  node->path = (JINJA_EXPRESSION_SPAN){owner.offset, owner.length};
  node->namespace_attribute = (JINJA_EXPRESSION_SPAN){attribute.offset, attribute.length};
  return 1;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_assignment(
    vstr input, vstr *name, vstr *rhs, JINJA_EXPRESSION_TREE *tree,
    JINJA_EXPRESSION_TREE *targets, int *capture, size_t *error_offset) {
  return jinja_expression_parse_assignment_allocated(input, name, rhs, tree, targets, capture, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_assignment_allocated(
    vstr input, vstr *name, vstr *rhs, JINJA_EXPRESSION_TREE *tree,
    JINJA_EXPRESSION_TREE *targets, int *capture, size_t *error_offset, const stl_allocator *allocator) {
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  JINJA_EXPRESSION_SPAN target = {0};
  JINJA_EXPRESSION_TREE parsed_targets;
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_INVALID;
  char *terminated;
  size_t rhs_offset = 0u, rhs_error = 0u;
  size_t parentheses = 0u;
  int is_capture = 0;
  int kind = 0, previous = 0;
  vstr expression;

  if (error_offset != NULL) *error_offset = 0u;
  if (!vstr_is_valid(input) || name == NULL || rhs == NULL || tree == NULL || targets == NULL || capture == NULL)
    return JINJA_EXPRESSION_PARSE_INVALID;
  if (input.len == SIZE_MAX) return JINJA_EXPRESSION_PARSE_CAPACITY;
  void *source_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_allocate(allocator, input.len + 1u, &source_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  terminated = (char *)source_storage;
  if (input.len != 0u) memcpy(terminated, input.data, input.len);
  terminated[input.len] = '\0';
  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated, input.len));
  do {
    int lex_status;
    previous = kind;
    lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
    if (lex_status == JINJA_EXPRESSION_LEX_EOF) {
      token.offset = input.len;
      token.length = 0u;
      is_capture = 1;
      break;
    }
    if (lex_status != JINJA_EXPRESSION_LEX_TOKEN) goto cleanup;
    if (kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN) ++parentheses;
    else if (kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN && parentheses != 0u) --parentheses;
    if (kind == JINJA_EXPRESSION_TOKEN_PIPE && parentheses == 0u) {
      is_capture = 1;
      break;
    }
  } while (kind != JINJA_EXPRESSION_TOKEN_ASSIGN);
  if (previous == JINJA_EXPRESSION_TOKEN_COMMA && token.length != 0u) goto cleanup;
  status = jinja_expression_parse_tree_mode_allocated(vstr_from_buf(terminated, token.offset),
                                            &parsed_targets, &rhs_error, 1, SIZE_MAX, 0, allocator);
  if (status != JINJA_EXPRESSION_PARSE_OK) {
    token.offset = rhs_error;
    goto cleanup;
  }
  for (size_t i = 0u; i < parsed_targets.count; ++i) {
    JINJA_EXPRESSION_CONDITION *node = &parsed_targets.nodes[i];
    if (node->unary_not_count != 0u ||
        (node->kind != JINJA_EXPRESSION_CONDITION_PATH &&
         node->kind != JINJA_EXPRESSION_CONDITION_TUPLE)) {
      status = JINJA_EXPRESSION_PARSE_INVALID;
      goto cleanup;
    }
    if (node->kind == JINJA_EXPRESSION_CONDITION_PATH &&
        memchr(terminated + node->path.offset, '.', node->path.length) != NULL) {
      if (!jinja_expression_namespace_target(vstr_from_buf(terminated, input.len), node)) {
        status = JINJA_EXPRESSION_PARSE_INVALID;
        goto cleanup;
      }
    }
  }
  target = parsed_targets.nodes[parsed_targets.root].kind == JINJA_EXPRESSION_CONDITION_PATH
               ? parsed_targets.nodes[parsed_targets.root].path
               : (JINJA_EXPRESSION_SPAN){0u, token.offset};
  rhs_offset = is_capture ? token.offset : token.offset + token.length;
  status = JINJA_EXPRESSION_PARSE_OK;

cleanup:
  if (error_offset != NULL) *error_offset = token.offset;
  jinja_parser_deallocate(allocator, terminated, input.len + 1u);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  expression = is_capture ? input : vstr_from_buf(input.data + rhs_offset, input.len - rhs_offset);
  status = jinja_expression_parse_tree_mode_allocated(expression, tree, &rhs_error, 0,
                                            is_capture ? rhs_offset : SIZE_MAX, 0, allocator);
  if (error_offset != NULL) *error_offset = (is_capture ? 0u : rhs_offset) + rhs_error;
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  *name = vstr_from_buf(input.data + target.offset, target.length);
  *rhs = expression;
  *targets = parsed_targets;
  *capture = is_capture;
  return JINJA_EXPRESSION_PARSE_OK;
}

static int jinja_signature_name(JINJA_EXPRESSION_LEXER *lexer, int kind,
    JINJA_EXPRESSION_TOKEN *token, JINJA_EXPRESSION_SPAN *name) {
  switch (kind) {
  case JINJA_EXPRESSION_TOKEN_IDENTIFIER:
  case JINJA_EXPRESSION_TOKEN_AND:
  case JINJA_EXPRESSION_TOKEN_OR:
  case JINJA_EXPRESSION_TOKEN_IN:
  case JINJA_EXPRESSION_TOKEN_IF:
  case JINJA_EXPRESSION_TOKEN_ELSE:
  case JINJA_EXPRESSION_TOKEN_IS:
  case JINJA_EXPRESSION_TOKEN_NOT:
    if (kind == JINJA_EXPRESSION_TOKEN_NOT) token->length = sizeof("not") - 1u;
    *name = (JINJA_EXPRESSION_SPAN){token->offset, token->length};
    lexer->expects_operand = 0;
    lexer->expects_membership_in = 0;
    lexer->test_state = JINJA_EXPRESSION_TEST_LEX_NONE;
    return 1;
  default:
    return 0;
  }
}

/* The parser has validated the path; re2c extracts its root name without
 * treating whitespace, dots, or keyword spellings as part of the binding. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_binding_reference(vstr input,
    JINJA_EXPRESSION_SPAN path, JINJA_EXPRESSION_SPAN *references, size_t *reference_count) {
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  JINJA_EXPRESSION_SPAN name;
  int kind = 0;
  size_t position = 0u;
  if (path.offset > input.len || path.length > input.len - path.offset || path.length == 0u)
    return JINJA_EXPRESSION_PARSE_INVALID;
  jinja_expression_lexer_init(&lexer, vstr_from_buf(input.data + path.offset, path.length));
  if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0 ||
      !jinja_signature_name(&lexer, kind, &token, &name) ||
      name.offset > path.length || name.length > path.length - name.offset)
    return JINJA_EXPRESSION_PARSE_INVALID;
  name.offset += path.offset;
  for (size_t i = 0u; i < *reference_count; ++i) {
    JINJA_EXPRESSION_SPAN previous = references[i];
    if (previous.length == name.length &&
        memcmp(input.data + previous.offset, input.data + name.offset, name.length) == 0) {
      if (previous.offset <= name.offset) return JINJA_EXPRESSION_PARSE_OK;
      --(*reference_count);
      memmove(&references[i], &references[i + 1u],
          (*reference_count - i) * sizeof(name));
      break;
    }
  }
  if (*reference_count == JINJA_EXPRESSION_MAX_REFERENCES)
    return JINJA_EXPRESSION_PARSE_CAPACITY;
  while (position < *reference_count &&
         references[position].offset < name.offset) ++position;
  memmove(&references[position + 1u], &references[position],
      (*reference_count - position) * sizeof(name));
  references[position] = name;
  ++(*reference_count);
  return JINJA_EXPRESSION_PARSE_OK;
}

/* AST storage includes reads in both short-circuit branches. Simple comparisons
 * keep PATH operands inline. O(nodes * references * name length), all bounded. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_collect_binding_facts(vstr input,
    size_t start, const JINJA_EXPRESSION_TREE *tree, int targets,
    JINJA_EXPRESSION_BINDING_ACCESSES *facts, size_t *error_offset) {
  if (!vstr_is_valid(input) || tree == NULL || start > input.len)
    return JINJA_EXPRESSION_PARSE_INVALID;
  if (tree->count > JINJA_EXPRESSION_MAX_NODES) return JINJA_EXPRESSION_PARSE_CAPACITY;
  if (tree->count == 0u || tree->root >= tree->count) return JINJA_EXPRESSION_PARSE_INVALID;
  for (size_t i = 0u; i < tree->count; ++i) {
    const JINJA_EXPRESSION_CONDITION *node = &tree->nodes[i];
    JINJA_EXPRESSION_SPAN paths[2];
    size_t count = 0u;
    int write = 0;
    if (targets) {
      if (node->kind == JINJA_EXPRESSION_CONDITION_PATH) {
        paths[count++] = node->path;
        write = 1;
      } else if (node->kind == JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET)
        paths[count++] = node->path;
      else if (node->kind != JINJA_EXPRESSION_CONDITION_TUPLE)
        return JINJA_EXPRESSION_PARSE_INVALID;
    } else if (node->kind == JINJA_EXPRESSION_CONDITION_PATH) paths[count++] = node->path;
    else if (node->kind == JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET)
      return JINJA_EXPRESSION_PARSE_INVALID;
    else if (node->kind == JINJA_EXPRESSION_CONDITION_COMPARISON) {
      if (node->left_operand.kind == JINJA_EXPRESSION_OPERAND_PATH)
        paths[count++] = node->left_operand.span;
      if (node->right_operand.kind == JINJA_EXPRESSION_OPERAND_PATH)
        paths[count++] = node->right_operand.span;
    }
    for (size_t j = 0u; j < count; ++j) {
      if (paths[j].offset > input.len - start) return JINJA_EXPRESSION_PARSE_INVALID;
      paths[j].offset += start;
      JINJA_EXPRESSION_PARSE_STATUS status = jinja_binding_reference(input, paths[j],
          write ? facts->writes : facts->reads, write ? &facts->write_count : &facts->read_count);
      if (status != JINJA_EXPRESSION_PARSE_OK) {
        if (error_offset != NULL) *error_offset = paths[j].offset;
        return status;
      }
    }
  }
  return JINJA_EXPRESSION_PARSE_OK;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_expression_collect_bindings(vstr input,
    const JINJA_EXPRESSION_TREE *tree, int targets, JINJA_EXPRESSION_BINDING_ACCESSES *result,
    size_t *error_offset) {
  JINJA_EXPRESSION_BINDING_ACCESSES parsed = {0};
  if (error_offset != NULL) *error_offset = 0u;
  if (result == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_collect_binding_facts(
      input, 0u, tree, targets, &parsed, error_offset);
  if (status == JINJA_EXPRESSION_PARSE_OK) *result = parsed;
  return status;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_collect_reads(vstr input,
    const JINJA_EXPRESSION_TREE *tree, JINJA_EXPRESSION_BINDING_ACCESSES *result,
    size_t *error_offset) {
  return jinja_expression_collect_bindings(input, tree, 0, result, error_offset);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_collect_targets(vstr input,
    const JINJA_EXPRESSION_TREE *tree, JINJA_EXPRESSION_BINDING_ACCESSES *result,
    size_t *error_offset) {
  return jinja_expression_collect_bindings(input, tree, 1, result, error_offset);
}

static int jinja_scope_name_valid(vstr input, JINJA_EXPRESSION_SPAN name) {
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  JINJA_EXPRESSION_SPAN parsed;
  int kind = 0;
  if (name.offset > input.len || name.length == 0u || name.length > input.len - name.offset)
    return 0;
  jinja_expression_lexer_init(&lexer, vstr_from_buf(input.data + name.offset, name.length));
  return jinja_expression_lexer_next(&lexer, &kind, &token) > 0 &&
      jinja_signature_name(&lexer, kind, &token, &parsed) &&
      parsed.offset == 0u && parsed.length == name.length;
}

static size_t jinja_scope_local(const JINJA_EXPRESSION_SCOPE *scope, vstr name) {
  for (size_t i = 0u; i < scope->count; ++i) {
    JINJA_EXPRESSION_SPAN stored = scope->symbols[i].name;
    if (stored.length == name.len &&
        memcmp(scope->source.data + stored.offset, name.data, name.len) == 0) return i;
  }
  return SIZE_MAX;
}

static JINJA_EXPRESSION_SCOPE_REFERENCE jinja_scope_lookup(
    const JINJA_EXPRESSION_SCOPE *scope, vstr name) {
  size_t depth = 0u;
  for (; scope != NULL; scope = scope->parent, ++depth) {
    size_t symbol = jinja_scope_local(scope, name);
    if (symbol != SIZE_MAX) return (JINJA_EXPRESSION_SCOPE_REFERENCE){depth, symbol};
  }
  return (JINJA_EXPRESSION_SCOPE_REFERENCE){0u, SIZE_MAX};
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_resolve_scope(
    const JINJA_EXPRESSION_SCOPE *scope, vstr name, JINJA_EXPRESSION_SCOPE_REFERENCE *result) {
  size_t depth = 0u;
  if (scope == NULL || result == NULL || !vstr_is_valid(name) ||
      !jinja_scope_name_valid(name, (JINJA_EXPRESSION_SPAN){0u, name.len}))
    return JINJA_EXPRESSION_PARSE_INVALID;
  /* Validate the whole borrowed chain before publishing even a local match. */
  for (const JINJA_EXPRESSION_SCOPE *frame = scope; frame != NULL; frame = frame->parent) {
    if (depth++ == JINJA_EXPRESSION_MAX_SCOPE_DEPTH) return JINJA_EXPRESSION_PARSE_CAPACITY;
    if (!vstr_is_valid(frame->source) || frame->count > JINJA_EXPRESSION_MAX_REFERENCES)
      return JINJA_EXPRESSION_PARSE_INVALID;
    for (size_t i = 0u; i < frame->count; ++i) {
      JINJA_EXPRESSION_SPAN span = frame->symbols[i].name;
      if (span.length == 0u || span.offset > frame->source.len ||
          span.length > frame->source.len - span.offset) return JINJA_EXPRESSION_PARSE_INVALID;
    }
  }
  *result = jinja_scope_lookup(scope, name);
  return JINJA_EXPRESSION_PARSE_OK;
}

/* Parent frames have finished analysis. Only the new frame mutates; no runtime
 * cell is borrowed. O(events * ancestors * symbols * name bytes), all bounded. */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_analyze_scope(vstr input,
    const JINJA_EXPRESSION_SCOPE *parent, const JINJA_EXPRESSION_SCOPE_EVENT *events,
    size_t event_count, JINJA_EXPRESSION_SCOPE *result, size_t *error_offset) {
  JINJA_EXPRESSION_SCOPE parsed = {0};
  size_t depth = 1u;
  int body_started = 0;
  if (error_offset != NULL) *error_offset = 0u;
  if (!vstr_is_valid(input) || result == NULL || (event_count != 0u && events == NULL))
    return JINJA_EXPRESSION_PARSE_INVALID;
  if (event_count > JINJA_EXPRESSION_MAX_SCOPE_EVENTS) return JINJA_EXPRESSION_PARSE_CAPACITY;
  for (const JINJA_EXPRESSION_SCOPE *ancestor = parent; ancestor != NULL; ancestor = ancestor->parent) {
    if (ancestor == result) return JINJA_EXPRESSION_PARSE_INVALID;
    if (depth++ == JINJA_EXPRESSION_MAX_SCOPE_DEPTH) return JINJA_EXPRESSION_PARSE_CAPACITY;
    if (!vstr_is_valid(ancestor->source) || ancestor->count > JINJA_EXPRESSION_MAX_REFERENCES)
      return JINJA_EXPRESSION_PARSE_INVALID;
  }
  parsed.source = input;
  parsed.parent = parent;
  for (size_t i = 0u; i < event_count; ++i) {
    const JINJA_EXPRESSION_SCOPE_EVENT *event = &events[i];
    if (error_offset != NULL) *error_offset = event->name.offset <= input.len ? event->name.offset : input.len;
    if (event->kind < JINJA_EXPRESSION_SCOPE_READ || event->kind > JINJA_EXPRESSION_SCOPE_BRANCH_STORE ||
        !jinja_scope_name_valid(input, event->name)) return JINJA_EXPRESSION_PARSE_INVALID;
    const int parameter = event->kind == JINJA_EXPRESSION_SCOPE_PARAMETER;
    if (parameter && body_started) return JINJA_EXPRESSION_PARSE_INVALID;
    if (!parameter) body_started = 1;
    const vstr name = vstr_from_buf(input.data + event->name.offset, event->name.length);
    size_t local = jinja_scope_local(&parsed, name);
    if (local != SIZE_MAX) {
      if (parameter) return JINJA_EXPRESSION_PARSE_INVALID;
      if (event->kind != JINJA_EXPRESSION_SCOPE_READ) parsed.symbols[local].stored = 1;
      continue;
    }
    size_t parent_slot = SIZE_MAX, parent_depth = 0u;
    if (!parameter) {
      JINJA_EXPRESSION_SCOPE_REFERENCE reference = jinja_scope_lookup(parent, name);
      parent_slot = reference.symbol;
      if (parent_slot != SIZE_MAX) parent_depth = reference.depth + 1u;
    }
    if (event->kind == JINJA_EXPRESSION_SCOPE_READ && parent_slot != SIZE_MAX) continue;
    if (parsed.count == JINJA_EXPRESSION_MAX_REFERENCES) return JINJA_EXPRESSION_PARSE_CAPACITY;
    JINJA_EXPRESSION_SCOPE_LOAD load = parameter ? JINJA_EXPRESSION_SCOPE_ARGUMENT :
        parent_slot != SIZE_MAX ? JINJA_EXPRESSION_SCOPE_ALIAS :
        event->kind == JINJA_EXPRESSION_SCOPE_STORE ? JINJA_EXPRESSION_SCOPE_UNDEFINED :
        JINJA_EXPRESSION_SCOPE_RESOLVE;
    parsed.symbols[parsed.count++] = (JINJA_EXPRESSION_SCOPE_SYMBOL){
        event->name, load, event->kind != JINJA_EXPRESSION_SCOPE_READ, parent_depth, parent_slot};
  }
  *result = parsed;
  if (error_offset != NULL) *error_offset = 0u;
  return JINJA_EXPRESSION_PARSE_OK;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_signature_default_references(vstr input,
    size_t start, const JINJA_EXPRESSION_TREE *tree, JINJA_EXPRESSION_MACRO_SIGNATURE *signature,
    size_t *error_offset) {
  JINJA_EXPRESSION_BINDING_ACCESSES facts = {0};
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_collect_binding_facts(
      input, start, tree, 0, &facts, error_offset);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  for (size_t i = 0u; i < facts.read_count; ++i) {
    status = jinja_binding_reference(input, facts.reads[i],
        signature->default_references, &signature->default_reference_count);
    if (status != JINJA_EXPRESSION_PARSE_OK) {
      *error_offset = facts.reads[i].offset;
      return status;
    }
  }
  return JINJA_EXPRESSION_PARSE_OK;
}

/* Delimit only at signature depth. Lemon owns matching brackets and expression
 * semantics; this scan must not treat commas inside strings/calls as parameters. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_signature_default(JINJA_EXPRESSION_LEXER *lexer,
    int *kind, JINJA_EXPRESSION_TOKEN *token, JINJA_EXPRESSION_SPAN *span,
    JINJA_EXPRESSION_MACRO_SIGNATURE *signature, size_t *error_offset, const stl_allocator *allocator) {
  JINJA_EXPRESSION_TREE tree;
  size_t start = (size_t)(lexer->cursor - lexer->input), nesting = 0u, local_error = 0u;
  int lex_status;
  for (;;) {
    lex_status = jinja_expression_lexer_next(lexer, kind, token);
    if (lex_status <= 0) return lex_status == JINJA_EXPRESSION_LEX_UNSUPPORTED
        ? JINJA_EXPRESSION_PARSE_UNSUPPORTED : JINJA_EXPRESSION_PARSE_INVALID;
    if (nesting == 0u && (*kind == JINJA_EXPRESSION_TOKEN_COMMA || *kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN))
      break;
    if (*kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN || *kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACKET ||
        *kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACE) ++nesting;
    else if (*kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN || *kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET ||
             *kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACE) {
      if (nesting == 0u) return JINJA_EXPRESSION_PARSE_INVALID;
      --nesting;
    }
  }
  *span = (JINJA_EXPRESSION_SPAN){start, token->offset - start};
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_tree_allocated(
      vstr_from_buf(lexer->input + start, span->length), &tree, &local_error, allocator);
  if (status != JINJA_EXPRESSION_PARSE_OK) *error_offset = start + local_error;
  else status = jinja_signature_default_references(
      vstr_from_buf(lexer->input, (size_t)(lexer->limit - lexer->input)), start, &tree, signature, error_offset);
  return status;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_signature_header(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature,
    JINJA_EXPRESSION_SPAN *call_span, JINJA_EXPRESSION_TREE *call_tree, size_t *error_offset, const stl_allocator *allocator) {
  JINJA_EXPRESSION_MACRO_SIGNATURE parsed = {0};
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_INVALID;
  size_t failure_offset = SIZE_MAX;
  size_t call_offset = 0u;
  int kind = 0, saw_default = 0;
  char *terminated;
  if (!vstr_is_valid(input) || signature == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  if (input.len == SIZE_MAX) return JINJA_EXPRESSION_PARSE_CAPACITY;
  void *source_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_allocate(allocator, input.len + 1u, &source_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  terminated = (char *)source_storage;
  if (input.len != 0u) memcpy(terminated, input.data, input.len);
  terminated[input.len] = '\0';
  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated, input.len));
  if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0) goto cleanup;
  if (call_span == NULL) {
    if (!jinja_signature_name(&lexer, kind, &token, &parsed.name) ||
        jinja_expression_lexer_next(&lexer, &kind, &token) <= 0 ||
        kind != JINJA_EXPRESSION_TOKEN_LEFT_PAREN) goto cleanup;
  } else if (kind != JINJA_EXPRESSION_TOKEN_LEFT_PAREN) goto parse_call;
  if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0) goto cleanup;
  while (kind != JINJA_EXPRESSION_TOKEN_RIGHT_PAREN) {
    JINJA_EXPRESSION_PARAMETER parameter = {0};
    if (!jinja_signature_name(&lexer, kind, &token, &parameter.name)) goto cleanup;
    if (parsed.parameter_count == JINJA_EXPRESSION_MAX_NODES) {
      status = JINJA_EXPRESSION_PARSE_CAPACITY;
      goto cleanup;
    }
    for (size_t i = 0u; i < parsed.parameter_count; ++i) {
      JINJA_EXPRESSION_SPAN previous = parsed.parameters[i].name;
      if (previous.length == parameter.name.length &&
          memcmp(input.data + previous.offset, input.data + parameter.name.offset, previous.length) == 0)
        goto cleanup;
    }
    if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0) goto cleanup;
    if (kind == JINJA_EXPRESSION_TOKEN_ASSIGN) {
      status = jinja_signature_default(&lexer, &kind, &token, &parameter.default_expression, &parsed, &failure_offset, allocator);
      if (status != JINJA_EXPRESSION_PARSE_OK) goto cleanup;
      status = JINJA_EXPRESSION_PARSE_INVALID;
      saw_default = 1;
    } else if (saw_default) goto cleanup;
    parsed.parameters[parsed.parameter_count++] = parameter;
    if (kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN) break;
    if (kind != JINJA_EXPRESSION_TOKEN_COMMA ||
        jinja_expression_lexer_next(&lexer, &kind, &token) <= 0 ||
        kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN) goto cleanup;
  }
  call_offset = (size_t)(lexer.cursor - lexer.input);
  if (call_span == NULL &&
      jinja_expression_lexer_next(&lexer, &kind, &token) != JINJA_EXPRESSION_LEX_EOF) goto cleanup;
  /* All parameters shadow the definition environment in defaults, including
   * self and forward references. Missing arguments are a runtime state, not
   * permission to resolve a parameter name from the outer scope. */
  for (size_t i = 0u; i < parsed.default_reference_count; ++i) {
    const JINJA_EXPRESSION_SPAN reference = parsed.default_references[i];
    parsed.default_reference_parameters[i] = SIZE_MAX;
    for (size_t j = 0u; j < parsed.parameter_count; ++j) {
      const JINJA_EXPRESSION_SPAN parameter = parsed.parameters[j].name;
      if (reference.length == parameter.length &&
          memcmp(input.data + reference.offset, input.data + parameter.offset, reference.length) == 0) {
        parsed.default_reference_parameters[i] = j;
        break;
      }
    }
  }
parse_call:
  if (call_span != NULL) {
    JINJA_EXPRESSION_TREE tree;
    size_t local_error = 0u;
    status = jinja_expression_parse_tree_allocated(vstr_from_buf(input.data + call_offset, input.len - call_offset),
                                         &tree, &local_error, allocator);
    failure_offset = call_offset + local_error;
    if (status != JINJA_EXPRESSION_PARSE_OK) goto cleanup;
    if (tree.nodes[tree.root].kind != JINJA_EXPRESSION_CONDITION_CALL ||
        tree.nodes[tree.root].unary_not_count != 0u) {
      status = JINJA_EXPRESSION_PARSE_INVALID;
      failure_offset = call_offset;
      goto cleanup;
    }
    *call_tree = tree;
    *call_span = (JINJA_EXPRESSION_SPAN){call_offset, input.len - call_offset};
  }
  *signature = parsed;
  status = JINJA_EXPRESSION_PARSE_OK;
cleanup:
  if (error_offset != NULL) *error_offset = failure_offset == SIZE_MAX ? token.offset : failure_offset;
  jinja_parser_deallocate(allocator, terminated, input.len + 1u);
  return status;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_macro_signature(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature, size_t *error_offset) {
  return jinja_expression_parse_macro_signature_allocated(input, signature, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_macro_signature_allocated(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature, size_t *error_offset, const stl_allocator *allocator) {
  return jinja_expression_parse_signature_header(input, signature, NULL, NULL, error_offset, allocator);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_call_header(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature,
    JINJA_EXPRESSION_SPAN *call_span, JINJA_EXPRESSION_TREE *tree, size_t *error_offset) {
  return jinja_expression_parse_call_header_allocated(input, signature, call_span, tree, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_call_header_allocated(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature,
    JINJA_EXPRESSION_SPAN *call_span, JINJA_EXPRESSION_TREE *tree, size_t *error_offset, const stl_allocator *allocator) {
  if (call_span == NULL || tree == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  return jinja_expression_parse_signature_header(input, signature, call_span, tree, error_offset, allocator);
}

static int jinja_reference_word(JINJA_EXPRESSION_TOKEN token, const char *word) {
  const size_t length = strlen(word);
  return token.length == length && memcmp(token.text, word, length) == 0;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_parse_named_block_header(vstr input,
    vstr opening_name, JINJA_TEMPLATE_BLOCK_HEADER *result, size_t *error_offset, const stl_allocator *allocator) {
  JINJA_TEMPLATE_BLOCK_HEADER parsed = {0};
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_INVALID;
  int kind = 0, lex_status;
  char *terminated;
  if (!vstr_is_valid(input)) return JINJA_EXPRESSION_PARSE_INVALID;
  if (input.len == SIZE_MAX) return JINJA_EXPRESSION_PARSE_CAPACITY;
  void *source_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_allocate(allocator, input.len + 1u, &source_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  terminated = (char *)source_storage;
  if (input.len != 0u) memcpy(terminated, input.data, input.len);
  terminated[input.len] = '\0';
  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated, input.len));
  lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
  if (lex_status == JINJA_EXPRESSION_LEX_EOF && result == NULL) goto success;
  if (lex_status <= 0) goto cleanup;
  /* Block names are lexical names, not assignment targets: true/none are valid. */
  if (kind == JINJA_EXPRESSION_TOKEN_TRUE || kind == JINJA_EXPRESSION_TOKEN_FALSE ||
      kind == JINJA_EXPRESSION_TOKEN_NONE) kind = JINJA_EXPRESSION_TOKEN_IDENTIFIER;
  if (!jinja_signature_name(&lexer, kind, &token, &parsed.name)) goto cleanup;
  if (result == NULL && (parsed.name.length != opening_name.len ||
      memcmp(input.data + parsed.name.offset, opening_name.data, opening_name.len) != 0)) goto cleanup;
  lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
  if (result != NULL && lex_status > 0 && jinja_reference_word(token, "scoped")) {
    parsed.scoped = 1;
    lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
  }
  if (result != NULL && lex_status > 0 && jinja_reference_word(token, "required")) {
    parsed.required = 1;
    lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
  }
  if (lex_status != JINJA_EXPRESSION_LEX_EOF) goto cleanup;
success:
  if (result != NULL) *result = parsed;
  status = JINJA_EXPRESSION_PARSE_OK;
cleanup:
  if (lex_status == JINJA_EXPRESSION_LEX_UNSUPPORTED) status = JINJA_EXPRESSION_PARSE_UNSUPPORTED;
  if (error_offset != NULL) *error_offset = token.offset;
  jinja_parser_deallocate(allocator, terminated, input.len + 1u);
  return status;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_block_header(
    vstr input, JINJA_TEMPLATE_BLOCK_HEADER *result, size_t *error_offset) {
  return jinja_expression_parse_block_header_allocated(input, result, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_block_header_allocated(
    vstr input, JINJA_TEMPLATE_BLOCK_HEADER *result, size_t *error_offset, const stl_allocator *allocator) {
  if (result == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  return jinja_parse_named_block_header(input, (vstr){0}, result, error_offset, allocator);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_endblock(
    vstr input, vstr opening_name, size_t *error_offset) {
  return jinja_expression_parse_endblock_allocated(input, opening_name, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_endblock_allocated(
    vstr input, vstr opening_name, size_t *error_offset, const stl_allocator *allocator) {
  if (!vstr_is_valid(opening_name) || opening_name.len == 0u) return JINJA_EXPRESSION_PARSE_INVALID;
  return jinja_parse_named_block_header(input, opening_name, NULL, error_offset, allocator);
}

/* Consume a complete context suffix only; a name spelled 'with' can also be
 * an imported symbol. Lexer lookahead distinguishes these without rewriting. */
static int jinja_reference_context(JINJA_EXPRESSION_LEXER *lexer,
    JINJA_EXPRESSION_TOKEN token, int *with_context) {
  JINJA_EXPRESSION_LEXER look = *lexer;
  JINJA_EXPRESSION_TOKEN next = {0};
  int kind = 0;
  const int enabled = jinja_reference_word(token, "with");
  if (!enabled && !jinja_reference_word(token, "without")) return 0;
  if (jinja_expression_lexer_next(&look, &kind, &next) <= 0 ||
      !jinja_reference_word(next, "context") ||
      jinja_expression_lexer_next(&look, &kind, &next) != JINJA_EXPRESSION_LEX_EOF) return 0;
  *lexer = look;
  *with_context = enabled;
  return 1;
}

/* A modifier is a boundary only after a complete expression. This also keeps
 * keyword spellings in conditional expressions, calls, and attributes intact.
 * Worst case O(tokens * expression parse cost); each candidate AST is bounded. */
typedef enum JINJA_HEADER_BOUNDARY {
  JINJA_HEADER_BOUNDARY_NONE,
  JINJA_HEADER_BOUNDARY_INCLUDE,
  JINJA_HEADER_BOUNDARY_IMPORT,
  JINJA_HEADER_BOUNDARY_FROM,
  JINJA_HEADER_BOUNDARY_FOR_ITERABLE,
  JINJA_HEADER_BOUNDARY_FOR_TEST
} JINJA_HEADER_BOUNDARY;

static JINJA_EXPRESSION_PARSE_STATUS jinja_header_expression(vstr input,
    JINJA_HEADER_BOUNDARY boundary_kind, JINJA_EXPRESSION_LEXER *lexer,
    int *kind, JINJA_EXPRESSION_TOKEN *token, JINJA_EXPRESSION_SPAN *span, JINJA_EXPRESSION_TREE *tree,
    size_t *error_offset, const stl_allocator *allocator) {
  size_t nesting = 0u;
  int previous = 0, lex_status;
  for (;;) {
    const int test_argument = lexer->test_state == JINJA_EXPRESSION_TEST_LEX_ARGUMENT;
    lex_status = jinja_expression_lexer_next(lexer, kind, token);
    if (lex_status <= 0) break;
    int boundary = 0;
    if (!test_argument && nesting == 0u && token->offset != 0u &&
        previous != JINJA_EXPRESSION_TOKEN_DOT && previous != JINJA_EXPRESSION_TOKEN_COMMA) {
      if (boundary_kind == JINJA_HEADER_BOUNDARY_INCLUDE)
        boundary = jinja_reference_word(*token, "ignore") || jinja_reference_word(*token, "with") ||
                   jinja_reference_word(*token, "without");
      else if (boundary_kind == JINJA_HEADER_BOUNDARY_IMPORT)
        boundary = jinja_reference_word(*token, "as");
      else if (boundary_kind == JINJA_HEADER_BOUNDARY_FROM)
        boundary = jinja_reference_word(*token, "import");
      else if (boundary_kind == JINJA_HEADER_BOUNDARY_FOR_ITERABLE)
        boundary = jinja_reference_word(*token, "if") || jinja_reference_word(*token, "recursive");
      else if (boundary_kind == JINJA_HEADER_BOUNDARY_FOR_TEST)
        boundary = jinja_reference_word(*token, "recursive");
    }
    if (boundary) {
      JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_tree_allocated(
          vstr_from_buf(input.data, token->offset), tree, error_offset, allocator);
      if (status == JINJA_EXPRESSION_PARSE_OK &&
          (boundary_kind == JINJA_HEADER_BOUNDARY_FOR_ITERABLE || !tree->unparenthesized_tuple)) {
        *span = (JINJA_EXPRESSION_SPAN){0u, token->offset};
        return status;
      }
      if (status == JINJA_EXPRESSION_PARSE_CAPACITY || status == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY)
        return status;
    }
    if (*kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN || *kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACKET ||
        *kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACE) ++nesting;
    else if (*kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN || *kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET ||
             *kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACE) {
      if (nesting == 0u) {
        *error_offset = token->offset;
        return JINJA_EXPRESSION_PARSE_INVALID;
      }
      --nesting;
    }
    previous = *kind;
  }
  if (lex_status < 0) {
    *error_offset = token->offset;
    return lex_status == JINJA_EXPRESSION_LEX_UNSUPPORTED
        ? JINJA_EXPRESSION_PARSE_UNSUPPORTED : JINJA_EXPRESSION_PARSE_INVALID;
  }
  *kind = 0;
  *span = (JINJA_EXPRESSION_SPAN){0u, input.len};
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_tree_allocated(input, tree, error_offset, allocator);
  if (status == JINJA_EXPRESSION_PARSE_OK && tree->unparenthesized_tuple &&
      boundary_kind != JINJA_HEADER_BOUNDARY_FOR_ITERABLE)
    return JINJA_EXPRESSION_PARSE_INVALID;
  return status;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_for_header(
    vstr input, JINJA_TEMPLATE_FOR_HEADER *result, size_t *error_offset) {
  return jinja_expression_parse_for_header_allocated(input, result, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_for_header_allocated(
    vstr input, JINJA_TEMPLATE_FOR_HEADER *result, size_t *error_offset, const stl_allocator *allocator) {
  JINJA_TEMPLATE_FOR_HEADER parsed = {0};
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_INVALID;
  size_t nesting = 0u, start = 0u, failure = 0u;
  int kind = 0, lex_status, previous = 0;
  char *terminated;
  if (!vstr_is_valid(input) || result == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  if (input.len == SIZE_MAX) return JINJA_EXPRESSION_PARSE_CAPACITY;
  void *source_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_allocate(allocator, input.len + 1u, &source_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  terminated = (char *)source_storage;
  if (input.len != 0u) memcpy(terminated, input.data, input.len);
  terminated[input.len] = '\0';
  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated, input.len));
  while ((lex_status = jinja_expression_lexer_next(&lexer, &kind, &token)) > 0) {
    failure = token.offset;
    if (nesting == 0u && previous != JINJA_EXPRESSION_TOKEN_COMMA && jinja_reference_word(token, "in")) {
      status = jinja_expression_parse_tree_mode_allocated(vstr_from_buf(input.data, token.offset),
          &parsed.targets, &failure, 1, SIZE_MAX, 0, allocator);
      if (status == JINJA_EXPRESSION_PARSE_CAPACITY || status == JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY) goto cleanup;
      if (status == JINJA_EXPRESSION_PARSE_OK) {
        parsed.target = (JINJA_EXPRESSION_SPAN){0u, token.offset};
        start = (size_t)(lexer.cursor - lexer.input);
        break;
      }
    }
    if (kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN || kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACKET ||
        kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACE) ++nesting;
    else if (kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN || kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET ||
             kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACE) {
      if (nesting == 0u) goto cleanup;
      --nesting;
    }
    previous = kind;
  }
  if (lex_status <= 0 || status != JINJA_EXPRESSION_PARSE_OK) {
    status = lex_status == JINJA_EXPRESSION_LEX_UNSUPPORTED
        ? JINJA_EXPRESSION_PARSE_UNSUPPORTED : JINJA_EXPRESSION_PARSE_INVALID;
    failure = token.offset;
    goto cleanup;
  }
  for (size_t i = 0u; i < parsed.targets.count; ++i) {
    const JINJA_EXPRESSION_CONDITION *node = &parsed.targets.nodes[i];
    if (node->unary_not_count != 0u ||
        (node->kind == JINJA_EXPRESSION_CONDITION_PATH &&
         memchr(input.data + node->path.offset, '.', node->path.length) != NULL) ||
        (node->kind != JINJA_EXPRESSION_CONDITION_PATH && node->kind != JINJA_EXPRESSION_CONDITION_TUPLE)) {
      status = JINJA_EXPRESSION_PARSE_INVALID;
      failure = 0u;
      goto cleanup;
    }
  }
  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated + start, input.len - start));
  status = jinja_header_expression(vstr_from_buf(input.data + start, input.len - start),
      JINJA_HEADER_BOUNDARY_FOR_ITERABLE, &lexer, &kind, &token, &parsed.iterable,
      &parsed.iterable_tree, &failure, allocator);
  failure += start;
  parsed.iterable.offset += start;
  if (status != JINJA_EXPRESSION_PARSE_OK) goto cleanup;
  if (kind != 0 && jinja_reference_word(token, "if")) {
    start += (size_t)(lexer.cursor - lexer.input);
    parsed.has_test = 1;
    jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated + start, input.len - start));
    status = jinja_header_expression(vstr_from_buf(input.data + start, input.len - start),
        JINJA_HEADER_BOUNDARY_FOR_TEST, &lexer, &kind, &token, &parsed.test, &parsed.test_tree, &failure, allocator);
    failure += start;
    parsed.test.offset += start;
    if (status != JINJA_EXPRESSION_PARSE_OK) goto cleanup;
  }
  if (kind != 0) {
    failure = start + token.offset;
    if (!jinja_reference_word(token, "recursive") ||
        jinja_expression_lexer_next(&lexer, &kind, &token) != JINJA_EXPRESSION_LEX_EOF) {
      status = JINJA_EXPRESSION_PARSE_INVALID;
      goto cleanup;
    }
    parsed.recursive = 1;
  }
  *result = parsed;
cleanup:
  if (error_offset != NULL) *error_offset = failure;
  jinja_parser_deallocate(allocator, terminated, input.len + 1u);
  return status;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_template_reference(
    vstr input, JINJA_TEMPLATE_REFERENCE_KIND reference_kind,
    JINJA_TEMPLATE_REFERENCE *result, size_t *error_offset) {
  return jinja_expression_parse_template_reference_allocated(input, reference_kind, result, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_template_reference_allocated(
    vstr input, JINJA_TEMPLATE_REFERENCE_KIND reference_kind,
    JINJA_TEMPLATE_REFERENCE *result, size_t *error_offset, const stl_allocator *allocator) {
  JINJA_TEMPLATE_REFERENCE parsed = {0};
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  JINJA_EXPRESSION_PARSE_STATUS status;
  size_t failure = 0u;
  int kind = 0, lex_status, expression_failed = 1;
  char *terminated;
  if (!vstr_is_valid(input) || result == NULL || reference_kind < JINJA_TEMPLATE_REFERENCE_EXTENDS ||
      reference_kind > JINJA_TEMPLATE_REFERENCE_FROM) return JINJA_EXPRESSION_PARSE_INVALID;
  if (input.len == SIZE_MAX) return JINJA_EXPRESSION_PARSE_CAPACITY;
  void *source_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_allocate(allocator, input.len + 1u, &source_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  terminated = (char *)source_storage;
  if (input.len != 0u) memcpy(terminated, input.data, input.len);
  terminated[input.len] = '\0';
  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated, input.len));
  parsed.with_context = reference_kind == JINJA_TEMPLATE_REFERENCE_INCLUDE;
  static const JINJA_HEADER_BOUNDARY boundaries[] = {JINJA_HEADER_BOUNDARY_NONE,
      JINJA_HEADER_BOUNDARY_INCLUDE, JINJA_HEADER_BOUNDARY_IMPORT, JINJA_HEADER_BOUNDARY_FROM};
  status = jinja_header_expression(input, boundaries[reference_kind], &lexer, &kind, &token,
      &parsed.expression, &parsed.tree, &failure, allocator);
  if (status != JINJA_EXPRESSION_PARSE_OK) goto cleanup;
  expression_failed = 0;
  status = JINJA_EXPRESSION_PARSE_INVALID;
  if (reference_kind == JINJA_TEMPLATE_REFERENCE_EXTENDS) goto success;
  if (reference_kind == JINJA_TEMPLATE_REFERENCE_INCLUDE) {
    if (kind == 0) goto success;
    if (jinja_reference_word(token, "ignore")) {
      if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0 ||
          !jinja_reference_word(token, "missing")) goto cleanup;
      parsed.ignore_missing = 1;
      lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
      if (lex_status == JINJA_EXPRESSION_LEX_EOF) goto success;
      if (lex_status < 0) goto cleanup;
    }
    if (jinja_reference_context(&lexer, token, &parsed.with_context)) goto success;
    goto cleanup;
  }
  if (kind == 0) goto cleanup;
  if (reference_kind == JINJA_TEMPLATE_REFERENCE_IMPORT) {
    if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0 ||
        !jinja_signature_name(&lexer, kind, &token, &parsed.names[0].alias)) goto cleanup;
    parsed.name_count = 1u;
    lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
    if (lex_status == JINJA_EXPRESSION_LEX_EOF) goto success;
    if (lex_status > 0 && jinja_reference_context(&lexer, token, &parsed.with_context)) goto success;
    goto cleanup;
  }
  if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0) goto cleanup;
  for (;;) {
    JINJA_TEMPLATE_IMPORT_NAME name = {0};
    if (jinja_reference_context(&lexer, token, &parsed.with_context)) goto success;
    if (!jinja_signature_name(&lexer, kind, &token, &name.name) || input.data[name.name.offset] == '_')
      goto cleanup;
    name.alias = name.name;
    lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
    if (lex_status > 0 && jinja_reference_word(token, "as")) {
      if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0 ||
          !jinja_signature_name(&lexer, kind, &token, &name.alias)) goto cleanup;
      lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
    }
    if (parsed.name_count == JINJA_EXPRESSION_MAX_NODES) {
      status = JINJA_EXPRESSION_PARSE_CAPACITY;
      goto cleanup;
    }
    parsed.names[parsed.name_count++] = name;
    if (lex_status == JINJA_EXPRESSION_LEX_EOF) goto success;
    if (lex_status < 0) goto cleanup;
    if (jinja_reference_context(&lexer, token, &parsed.with_context)) goto success;
    if (kind != JINJA_EXPRESSION_TOKEN_COMMA ||
        jinja_expression_lexer_next(&lexer, &kind, &token) <= 0) goto cleanup;
  }
success:
  *result = parsed;
  status = JINJA_EXPRESSION_PARSE_OK;
cleanup:
  if (error_offset != NULL) *error_offset = expression_failed ? failure : token.offset;
  jinja_parser_deallocate(allocator, terminated, input.len + 1u);
  return status;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_with_binding(
    vstr input, size_t *consumed, vstr *rhs, JINJA_EXPRESSION_TREE *tree,
    JINJA_EXPRESSION_TREE *targets) {
  return jinja_expression_parse_with_binding_allocated(input, consumed, rhs, tree, targets, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_with_binding_allocated(
    vstr input, size_t *consumed, vstr *rhs, JINJA_EXPRESSION_TREE *tree,
    JINJA_EXPRESSION_TREE *targets, const stl_allocator *allocator) {
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token;
  JINJA_EXPRESSION_TREE parsed_tree, parsed_targets;
  JINJA_EXPRESSION_PARSE_STATUS status;
  vstr name, parsed_rhs;
  size_t nesting = 0u, end = input.len;
  int kind, lex_status, assigned = 0, capture;
  char *terminated;
  if (!vstr_is_valid(input) || consumed == NULL || rhs == NULL || tree == NULL || targets == NULL)
    return JINJA_EXPRESSION_PARSE_INVALID;
  if (input.len == SIZE_MAX) return JINJA_EXPRESSION_PARSE_CAPACITY;
  void *source_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_allocate(allocator, input.len + 1u, &source_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  terminated = (char *)source_storage;
  if (input.len != 0u) memcpy(terminated, input.data, input.len);
  terminated[input.len] = '\0';
  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated, input.len));
  while ((lex_status = jinja_expression_lexer_next(&lexer, &kind, &token)) > 0) {
    if (kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN || kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACKET ||
        kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACE) ++nesting;
    else if (kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN || kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET ||
             kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACE) {
      if (nesting == 0u) break;
      --nesting;
    } else if (nesting == 0u && kind == JINJA_EXPRESSION_TOKEN_ASSIGN) assigned = 1;
    else if (nesting == 0u && assigned && kind == JINJA_EXPRESSION_TOKEN_COMMA) {
      end = token.offset;
      break;
    }
  }
  jinja_parser_deallocate(allocator, terminated, input.len + 1u);
  if (!assigned) return JINJA_EXPRESSION_PARSE_INVALID;
  status = jinja_expression_parse_assignment_allocated(vstr_from_buf(input.data, end), &name, &parsed_rhs,
                                              &parsed_tree, &parsed_targets, &capture, NULL, allocator);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  if (capture) return JINJA_EXPRESSION_PARSE_INVALID;
  for (size_t i = 0u; i < parsed_targets.count; ++i)
    if (parsed_targets.nodes[i].kind == JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET)
      return JINJA_EXPRESSION_PARSE_INVALID;
  *consumed = end;
  *rhs = parsed_rhs;
  *tree = parsed_tree;
  *targets = parsed_targets;
  return JINJA_EXPRESSION_PARSE_OK;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_legacy_condition(vstr expression, vstr *path,
                                                                      char *truth_marker,
                                                                      size_t *error_offset) {
  return jinja_expression_parse_legacy_condition_allocated(expression, path, truth_marker, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_legacy_condition_allocated(vstr expression, vstr *path,
                                                                      char *truth_marker,
                                                                      size_t *error_offset, const stl_allocator *allocator) {
  JINJA_EXPRESSION_CONDITION condition;
  JINJA_EXPRESSION_PARSE_STATUS status;

  if (path == NULL || truth_marker == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  status = jinja_expression_parse_condition_allocated(expression, &condition, error_offset, allocator);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  if (condition.kind != JINJA_EXPRESSION_CONDITION_PATH) return JINJA_EXPRESSION_PARSE_UNSUPPORTED;

  *path = vstr_from_buf(expression.data + condition.path.offset, condition.path.length);
  *truth_marker = condition.truth_marker;
  return JINJA_EXPRESSION_PARSE_OK;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_legacy_path(vstr expression, vstr *path,
                                                                 size_t *error_offset) {
  return jinja_expression_parse_legacy_path_allocated(expression, path, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_legacy_path_allocated(vstr expression, vstr *path,
                                                                 size_t *error_offset, const stl_allocator *allocator) {
  JINJA_EXPRESSION_CONDITION condition;
  JINJA_EXPRESSION_PARSE_STATUS status;

  if (path == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  status = jinja_expression_parse_condition_allocated(expression, &condition, error_offset, allocator);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  if (condition.kind != JINJA_EXPRESSION_CONDITION_PATH || condition.unary_not_count != 0u)
    return JINJA_EXPRESSION_PARSE_UNSUPPORTED;

  *path = vstr_from_buf(expression.data + condition.path.offset, condition.path.length);
  return JINJA_EXPRESSION_PARSE_OK;
}
