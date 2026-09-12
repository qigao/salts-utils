%name JinjaExpressionParse
%token_prefix JINJA_EXPRESSION_TOKEN_
%token_type {JINJA_EXPRESSION_TOKEN}
%default_type {JINJA_EXPRESSION_TOKEN}
%extra_argument {JINJA_EXPRESSION_PARSE_CONTEXT *context}
%stack_size 192

%include {
#include "jinja_expression_parser.h"
}

%type condition {JINJA_EXPRESSION_CONDITION}
%type conditional_expression {JINJA_EXPRESSION_CONDITION}
%type conditional_else {JINJA_EXPRESSION_CONDITIONAL_ELSE}
%type or_expression {JINJA_EXPRESSION_CONDITION}
%type and_expression {JINJA_EXPRESSION_CONDITION}
%type not_expression {JINJA_EXPRESSION_CONDITION}
%type comparison {JINJA_EXPRESSION_CONDITION}
%type additive {JINJA_EXPRESSION_CONDITION}
%type filtered {JINJA_EXPRESSION_CONDITION}
%type filter_result {JINJA_EXPRESSION_CONDITION}
%type concatenation {JINJA_EXPRESSION_CONDITION}
%type multiplicative {JINJA_EXPRESSION_CONDITION}
%type power {JINJA_EXPRESSION_CONDITION}
%type test_expression {JINJA_EXPRESSION_CONDITION}
%type tested_expression {JINJA_EXPRESSION_CONDITION}
%type test_operand {JINJA_EXPRESSION_CONDITION}
%type unary {JINJA_EXPRESSION_CONDITION}
%type postfix {JINJA_EXPRESSION_CONDITION}
%type slice_bound {JINJA_EXPRESSION_CONDITION}
%type slice_step {JINJA_EXPRESSION_CONDITION}
%type slice_spec {JINJA_EXPRESSION_SLICE}
%type subscript_key {JINJA_EXPRESSION_CONDITION}
%type subscript_component {JINJA_EXPRESSION_CONDITION}
%type subscript_tail {JINJA_EXPRESSION_COLLECTION}
%type primary {JINJA_EXPRESSION_CONDITION}
%type atom {JINJA_EXPRESSION_CONDITION}
%type test_name {JINJA_EXPRESSION_TEST}
%type filter_name {JINJA_EXPRESSION_TOKEN}
%type optional_test_argument {JINJA_EXPRESSION_TEST_ARGUMENTS}
%type test_argument {JINJA_EXPRESSION_CONDITION}
%type comparison_operator {JINJA_EXPRESSION_COMPARISON}
%type comparison_tail {JINJA_EXPRESSION_COMPARISON_TAIL}
%type list_items {JINJA_EXPRESSION_COLLECTION}
%type filter_arguments {JINJA_EXPRESSION_COLLECTION}
%type filter_argument_tail {JINJA_EXPRESSION_COLLECTION}
%type filter_argument {JINJA_EXPRESSION_FILTER_ARGUMENT}
%type argument_name {JINJA_EXPRESSION_TOKEN}
%type list_tail {JINJA_EXPRESSION_COLLECTION}
%type tuple_tail {JINJA_EXPRESSION_COLLECTION}
%type dict_items {JINJA_EXPRESSION_COLLECTION}
%type dict_tail {JINJA_EXPRESSION_COLLECTION}
%type path {JINJA_EXPRESSION_SPAN}
%token_destructor { (void)context; (void)$$; }
%default_destructor { (void)context; (void)$$; }

%token NOT AND OR IN IF ELSE IS DOT LEFT_PAREN RIGHT_PAREN LEFT_BRACKET RIGHT_BRACKET LEFT_BRACE RIGHT_BRACE COMMA COLON TRUE FALSE NONE INTEGER FLOAT STRING IDENTIFIER.
%token EQUAL NOT_EQUAL LESS LESS_EQUAL GREATER GREATER_EQUAL.
%token PLUS MINUS MULTIPLY TRUE_DIVIDE FLOOR_DIVIDE MODULO POWER.
%right IF.
%right ELSE.
%left PIPE.
%left LEFT_PAREN.

input ::= condition(C). {
  if (!context->failed) {
    size_t root;
    if (jinja_expression_store_condition(context, C, &root)) {
      context->tree.root = root;
      context->accepted = 1;
    }
  }
}

input ::= condition(V) COMMA tuple_tail(T). {
  if (!context->failed) {
    JINJA_EXPRESSION_COLLECTION items = {T.count + 1u, SIZE_MAX};
    JINJA_EXPRESSION_CONDITION tuple;
    size_t root;
    if (jinja_expression_store_collection_item(context, V, T.first_item, &items.first_item) &&
        jinja_expression_make_tuple(context, items, &tuple) &&
        jinja_expression_store_condition(context, tuple, &root)) {
      context->tree.root = root;
      context->tree.unparenthesized_tuple = 1;
      context->accepted = 1;
    }
  }
}

condition(C) ::= conditional_expression(I). {
  C = I;
}

conditional_expression(C) ::= or_expression(I). {
  C = I;
}

conditional_expression(C) ::= conditional_expression(V) IF(OP) or_expression(T)
                               conditional_else(E). {
  if (!jinja_expression_make_conditional(context, V, T, &E, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

conditional_else(E) ::= . [IF] {
  E = (JINJA_EXPRESSION_CONDITIONAL_ELSE){0};
}

conditional_else(E) ::= ELSE conditional_expression(I). {
  E = (JINJA_EXPRESSION_CONDITIONAL_ELSE){0};
  E.condition = I;
  E.has_else = 1;
}

or_expression(C) ::= and_expression(I). {
  C = I;
}

or_expression(C) ::= or_expression(L) OR(OP) and_expression(R). {
  size_t left;
  size_t right;
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_LOGICAL_OR;
  C.truth_marker = '#';
  if (!jinja_expression_store_condition(context, L, &left) ||
      !jinja_expression_store_condition(context, R, &right)) {
    context->error_offset = OP.offset;
  } else {
    C.left_condition = left;
    C.right_condition = right;
  }
}

and_expression(C) ::= not_expression(I). {
  C = I;
}

and_expression(C) ::= and_expression(L) AND(OP) not_expression(R). {
  size_t left;
  size_t right;
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_LOGICAL_AND;
  C.truth_marker = '#';
  if (!jinja_expression_store_condition(context, L, &left) ||
      !jinja_expression_store_condition(context, R, &right)) {
    context->error_offset = OP.offset;
  } else {
    C.left_condition = left;
    C.right_condition = right;
  }
}

not_expression(C) ::= comparison(I). {
  C = I;
}

not_expression(C) ::= NOT not_expression(I). {
  C = I;
  C.unary_not_count = I.unary_not_count + 1u;
  if (C.kind == JINJA_EXPRESSION_CONDITION_BOOL) {
    C.boolean = !I.boolean;
  } else if (C.kind == JINJA_EXPRESSION_CONDITION_INTEGER) {
    C.kind = JINJA_EXPRESSION_CONDITION_BOOL;
    C.boolean = I.integer == 0;
    C.integer = 0;
  } else if (C.kind == JINJA_EXPRESSION_CONDITION_FLOAT) {
    C.kind = JINJA_EXPRESSION_CONDITION_BOOL;
    C.boolean = I.floating == 0.0;
    C.floating = 0.0;
  } else if (C.kind == JINJA_EXPRESSION_CONDITION_NONE) {
    C.kind = JINJA_EXPRESSION_CONDITION_BOOL;
    C.boolean = 1;
  }
  C.truth_marker = I.truth_marker == '#' ? '^' : '#';
}

comparison(C) ::= additive(I). {
  C = I;
}

comparison(C) ::= additive(L) comparison_operator(OP) additive(R) comparison_tail(T). {
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_BOOL;
  C.path = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.unary_not_count = 0u;
  C.boolean = 0;
  C.integer = 0;
  C.string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.right_string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.left_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.right_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.comparison = JINJA_EXPRESSION_COMPARISON_EQUAL;
  C.truth_marker = '#';
  if (T.count != 0u) {
    size_t left;
    size_t first_step;
    if (!jinja_expression_store_condition(context, L, &left) ||
        !jinja_expression_store_comparison_step(context, OP.kind, R, T.first_step,
                                                &first_step)) {
      context->error_offset = OP.offset;
    } else {
      C.kind = JINJA_EXPRESSION_CONDITION_COMPARISON_CHAIN;
      C.left_condition = left;
      C.first_comparison_step = first_step;
      C.comparison_step_count = T.count + 1u;
    }
  } else if (OP.kind == JINJA_EXPRESSION_COMPARISON_IN ||
             OP.kind == JINJA_EXPRESSION_COMPARISON_NOT_IN) {
    size_t left;
    size_t right;
    if (!jinja_expression_store_condition(context, L, &left) ||
        !jinja_expression_store_condition(context, R, &right)) {
      context->error_offset = OP.offset;
    } else {
      C.kind = JINJA_EXPRESSION_CONDITION_NESTED_COMPARISON;
      C.comparison = OP.kind;
      C.left_condition = left;
      C.right_condition = right;
    }
  } else if (L.kind == JINJA_EXPRESSION_CONDITION_STRING &&
             R.kind == JINJA_EXPRESSION_CONDITION_STRING &&
             L.unary_not_count == 0u && R.unary_not_count == 0u) {
    C.kind = JINJA_EXPRESSION_CONDITION_STRING_COMPARISON;
    C.string = L.string;
    C.right_string = R.string;
    C.comparison = OP.kind;
  } else if (!jinja_expression_compare_numeric(&L, OP.kind, &R, &C.boolean)) {
    if (jinja_expression_condition_operand(&L, &C.left_operand) &&
        jinja_expression_condition_operand(&R, &C.right_operand)) {
      C.kind = JINJA_EXPRESSION_CONDITION_COMPARISON;
      C.comparison = OP.kind;
    } else {
      size_t left;
      size_t right;
      if (!jinja_expression_store_condition(context, L, &left) ||
          !jinja_expression_store_condition(context, R, &right)) {
        context->error_offset = OP.offset;
      } else {
        C.kind = JINJA_EXPRESSION_CONDITION_NESTED_COMPARISON;
        C.comparison = OP.kind;
        C.left_condition = left;
        C.right_condition = right;
      }
    }
  }
}

comparison_tail(T) ::= . {
  T.count = 0u;
  T.first_offset = 0u;
  T.first_step = SIZE_MAX;
}

comparison_tail(T) ::= comparison_operator(OP) additive(I) comparison_tail(R). {
  T.count = R.count + 1u;
  T.first_offset = OP.offset;
  T.first_step = SIZE_MAX;
  if (!jinja_expression_store_comparison_step(context, OP.kind, I, R.first_step,
                                              &T.first_step)) {
    context->error_offset = OP.offset;
  }
}

comparison_operator(OP) ::= EQUAL(I). {
  OP.kind = JINJA_EXPRESSION_COMPARISON_EQUAL;
  OP.offset = I.offset;
}

comparison_operator(OP) ::= NOT_EQUAL(I). {
  OP.kind = JINJA_EXPRESSION_COMPARISON_NOT_EQUAL;
  OP.offset = I.offset;
}

comparison_operator(OP) ::= LESS(I). {
  OP.kind = JINJA_EXPRESSION_COMPARISON_LESS;
  OP.offset = I.offset;
}

comparison_operator(OP) ::= LESS_EQUAL(I). {
  OP.kind = JINJA_EXPRESSION_COMPARISON_LESS_EQUAL;
  OP.offset = I.offset;
}

comparison_operator(OP) ::= GREATER(I). {
  OP.kind = JINJA_EXPRESSION_COMPARISON_GREATER;
  OP.offset = I.offset;
}

comparison_operator(OP) ::= GREATER_EQUAL(I). {
  OP.kind = JINJA_EXPRESSION_COMPARISON_GREATER_EQUAL;
  OP.offset = I.offset;
}

comparison_operator(OP) ::= IN(I). {
  OP.kind = JINJA_EXPRESSION_COMPARISON_IN;
  OP.offset = I.offset;
}

comparison_operator(OP) ::= NOT(I) IN. {
  OP.kind = JINJA_EXPRESSION_COMPARISON_NOT_IN;
  OP.offset = I.offset;
}

additive(C) ::= concatenation(I). {
  C = I;
}

additive(C) ::= additive(L) PLUS(OP) concatenation(R). {
  if (!jinja_expression_make_binary_arithmetic(context, JINJA_EXPRESSION_ARITHMETIC_ADD, L, R,
                                               &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

additive(C) ::= additive(L) MINUS(OP) concatenation(R). {
  if (!jinja_expression_make_binary_arithmetic(context, JINJA_EXPRESSION_ARITHMETIC_SUBTRACT, L,
                                               R, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

concatenation(C) ::= multiplicative(I). { C = I; }
concatenation(C) ::= concatenation(L) TILDE(OP) multiplicative(R). {
  if (!jinja_expression_make_concat(context, L, R, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

multiplicative(C) ::= power(I). {
  C = I;
}

multiplicative(C) ::= multiplicative(L) MULTIPLY(OP) power(R). {
  if (!jinja_expression_make_binary_arithmetic(context, JINJA_EXPRESSION_ARITHMETIC_MULTIPLY, L,
                                               R, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

multiplicative(C) ::= multiplicative(L) TRUE_DIVIDE(OP) power(R). {
  if (!jinja_expression_make_binary_arithmetic(
          context, JINJA_EXPRESSION_ARITHMETIC_TRUE_DIVIDE, L, R, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

multiplicative(C) ::= multiplicative(L) FLOOR_DIVIDE(OP) power(R). {
  if (!jinja_expression_make_binary_arithmetic(
          context, JINJA_EXPRESSION_ARITHMETIC_FLOOR_DIVIDE, L, R, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

multiplicative(C) ::= multiplicative(L) MODULO(OP) power(R). {
  if (!jinja_expression_make_binary_arithmetic(context, JINJA_EXPRESSION_ARITHMETIC_MODULO, L, R,
                                               &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

power(C) ::= test_expression(I). {
  C = I;
}

power(C) ::= power(L) POWER(OP) test_expression(R). {
  if (!jinja_expression_make_binary_arithmetic(context, JINJA_EXPRESSION_ARITHMETIC_POWER, L, R,
                                               &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

test_expression(C) ::= test_operand(I). { C = I; }
test_operand(C) ::= filtered(I). { C = I; }
test_operand(C) ::= tested_expression(I). { C = I; }

tested_expression(C) ::= test_operand(V) IS(OP) test_name(T) optional_test_argument(A). {
  if (!jinja_expression_make_test(context, V, T.kind, 0, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  } else {
    C.test_supported = T.supported;
    C.test_name = (JINJA_EXPRESSION_SPAN){T.offset, T.length};
    C.first_collection_item = A.values.first_item;
    C.collection_item_count = A.values.count;
    C.test_arguments_supplied = A.supplied;
  }
}

tested_expression(C) ::= test_operand(V) IS(OP) NOT test_name(T) optional_test_argument(A). {
  if (!jinja_expression_make_test(context, V, T.kind, 1, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  } else {
    C.test_supported = T.supported;
    C.test_name = (JINJA_EXPRESSION_SPAN){T.offset, T.length};
    C.first_collection_item = A.values.first_item;
    C.collection_item_count = A.values.count;
    C.test_arguments_supplied = A.supplied;
  }
}

tested_expression(C) ::= tested_expression(B) PIPE(OP) filter_name(N) LEFT_PAREN filter_arguments(A) RIGHT_PAREN. {
  if (!jinja_expression_make_filter(context, B, N, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  } else {
    C.first_collection_item = A.first_item;
    C.collection_item_count = A.count;
  }
}

tested_expression(C) ::= tested_expression(B) PIPE(OP) filter_name(N). {
  if (!jinja_expression_make_filter(context, B, N, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

tested_expression(C) ::= tested_expression(B) LEFT_PAREN(OP) filter_arguments(A) RIGHT_PAREN. {
  if (!jinja_expression_make_call(context, B, A, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

test_name(T) ::= IDENTIFIER(I). {
  if (!jinja_expression_test_from_token(I, &T)) {
    T = (JINJA_EXPRESSION_TEST){JINJA_EXPRESSION_TEST_DEFINED, I.offset, 0, I.length};
    context->failed = 1;
    context->error_offset = I.offset;
  }
}

test_name(T) ::= TRUE(I). {
  (void)jinja_expression_test_from_token(I, &T);
}

test_name(T) ::= FALSE(I). {
  (void)jinja_expression_test_from_token(I, &T);
}

test_name(T) ::= NONE(I). {
  (void)jinja_expression_test_from_token(I, &T);
}

test_name(T) ::= IN(I). {
  (void)jinja_expression_test_from_token(I, &T);
}

test_name(T) ::= test_name(B) DOT IDENTIFIER(I). {
  T = B;
  T.supported = 0;
  T.length = I.offset + I.length - T.offset;
}

filter_name(N) ::= IDENTIFIER(I). { N = I; }
filter_name(N) ::= filter_name(B) DOT IDENTIFIER(I). {
  N = B;
  N.length = I.offset + I.length - B.offset;
}

optional_test_argument(A) ::= . [PIPE] {
  A = (JINJA_EXPRESSION_TEST_ARGUMENTS){{0u, SIZE_MAX}, 0};
}

optional_test_argument(A) ::= LEFT_PAREN filter_arguments(I) RIGHT_PAREN. {
  A = (JINJA_EXPRESSION_TEST_ARGUMENTS){I, 1};
}

optional_test_argument(A) ::= test_argument(P). [PIPE] {
  A = (JINJA_EXPRESSION_TEST_ARGUMENTS){{1u, SIZE_MAX}, 1};
  if (!jinja_expression_store_collection_item(context, P, SIZE_MAX, &A.values.first_item))
    context->failed = 1;
}

/* A leading parenthesis belongs to the test call, not the shorthand argument. */
test_argument(C) ::= atom(A). { C = A; }

test_argument(C) ::= test_argument(B) LEFT_BRACKET(OP) subscript_key(K) RIGHT_BRACKET. {
  if (!jinja_expression_make_subscript_lookup(context, B, K, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

test_argument(C) ::= test_argument(B) DOT(OP) INTEGER(I). {
  if (!jinja_expression_make_integer_item_lookup(context, B, I, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

test_argument(C) ::= test_argument(B) DOT(OP) IDENTIFIER(A). {
  if (!jinja_expression_make_attribute_lookup(context, B, A, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

test_argument(C) ::= test_argument(B) LEFT_PAREN(OP) filter_arguments(A) RIGHT_PAREN. {
  if (!jinja_expression_make_call(context, B, A, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

filtered(C) ::= unary(I). { C = I; }
filtered(C) ::= filter_result(I). { C = I; }
filter_result(C) ::= filtered(B) PIPE(OP) filter_name(N) LEFT_PAREN filter_arguments(A) RIGHT_PAREN. {
  if (!jinja_expression_make_filter(context, B, N, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  } else {
    C.first_collection_item = A.first_item;
    C.collection_item_count = A.count;
  }
}
filter_result(C) ::= filtered(B) PIPE(OP) filter_name(N). {
  if (!jinja_expression_make_filter(context, B, N, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

filter_result(C) ::= filter_result(B) LEFT_PAREN(OP) filter_arguments(A) RIGHT_PAREN. {
  if (!jinja_expression_make_call(context, B, A, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

unary(C) ::= postfix(I). {
  C = I;
}

unary(C) ::= PLUS(OP) unary(I). {
  if (!jinja_expression_make_unary_arithmetic(context, JINJA_EXPRESSION_ARITHMETIC_POSITIVE, I,
                                              &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

unary(C) ::= MINUS(OP) unary(I). {
  if (!jinja_expression_make_unary_arithmetic(context, JINJA_EXPRESSION_ARITHMETIC_NEGATE, I,
                                              &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

postfix(C) ::= primary(I). {
  C = I;
}

postfix(C) ::= postfix(B) LEFT_BRACKET(OP) subscript_key(K) RIGHT_BRACKET. {
  if (!jinja_expression_make_subscript_lookup(context, B, K, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

postfix(C) ::= postfix(B) DOT(OP) INTEGER(I). {
  if (!jinja_expression_make_integer_item_lookup(context, B, I, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

slice_bound(B) ::= . { B = (JINJA_EXPRESSION_CONDITION){0}; B.kind = JINJA_EXPRESSION_CONDITION_NONE; B.truth_marker = '#'; }
slice_bound(B) ::= conditional_expression(E). { B = E; }
slice_step(S) ::= . { S = (JINJA_EXPRESSION_CONDITION){0}; S.kind = JINJA_EXPRESSION_CONDITION_NONE; S.truth_marker = '#'; }
slice_step(S) ::= COLON slice_bound(B). { S = B; }
slice_spec(S) ::= slice_bound(A) COLON slice_bound(B) slice_step(C). { S = (JINJA_EXPRESSION_SLICE){A, B, C}; }

subscript_component(C) ::= conditional_expression(E). { C = E; }
subscript_component(C) ::= slice_spec(S). {
  if (!jinja_expression_make_slice_value(context, S, &C)) C = (JINJA_EXPRESSION_CONDITION){0};
}
subscript_key(C) ::= . {
  if (!jinja_expression_make_tuple(context, (JINJA_EXPRESSION_COLLECTION){0u, SIZE_MAX}, &C))
    C = (JINJA_EXPRESSION_CONDITION){0};
}
subscript_key(C) ::= subscript_component(E). { C = E; }
subscript_key(C) ::= subscript_component(E) COMMA subscript_tail(T). {
  JINJA_EXPRESSION_COLLECTION items = {T.count + 1u, SIZE_MAX};
  if (!jinja_expression_store_collection_item(context, E, T.first_item, &items.first_item) ||
      !jinja_expression_make_tuple(context, items, &C)) C = (JINJA_EXPRESSION_CONDITION){0};
}
subscript_tail(T) ::= subscript_component(E). {
  T = (JINJA_EXPRESSION_COLLECTION){1u, SIZE_MAX};
  if (!jinja_expression_store_collection_item(context, E, SIZE_MAX, &T.first_item)) T.count = 0u;
}
subscript_tail(T) ::= subscript_component(E) COMMA subscript_tail(R). {
  T = (JINJA_EXPRESSION_COLLECTION){R.count + 1u, SIZE_MAX};
  if (!jinja_expression_store_collection_item(context, E, R.first_item, &T.first_item)) T.count = 0u;
}

postfix(C) ::= postfix(B) DOT(OP) IDENTIFIER(A). {
  if (!jinja_expression_make_attribute_lookup(context, B, A, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

postfix(C) ::= postfix(B) LEFT_PAREN(OP) filter_arguments(A) RIGHT_PAREN. {
  if (!jinja_expression_make_call(context, B, A, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
    context->error_offset = OP.offset;
  }
}

primary(C) ::= atom(A). { C = A; }

atom(C) ::= path(P). {
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_PATH;
  C.path = P;
  C.unary_not_count = 0u;
  C.boolean = 0;
  C.integer = 0;
  C.string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.right_string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.left_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.right_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.comparison = JINJA_EXPRESSION_COMPARISON_EQUAL;
  C.truth_marker = '#';
}

atom(C) ::= TRUE. {
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_BOOL;
  C.path = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.unary_not_count = 0u;
  C.boolean = 1;
  C.integer = 0;
  C.string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.right_string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.left_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.right_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.comparison = JINJA_EXPRESSION_COMPARISON_EQUAL;
  C.truth_marker = '#';
}

atom(C) ::= FALSE. {
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_BOOL;
  C.path = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.unary_not_count = 0u;
  C.boolean = 0;
  C.integer = 0;
  C.string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.right_string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.left_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.right_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.comparison = JINJA_EXPRESSION_COMPARISON_EQUAL;
  C.truth_marker = '#';
}

atom(C) ::= NONE. {
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_NONE;
  C.truth_marker = '#';
}

atom(C) ::= INTEGER(I). {
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_INTEGER;
  C.path = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.unary_not_count = 0u;
  C.boolean = 0;
  C.integer = 0;
  C.string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.right_string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.left_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.right_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.comparison = JINJA_EXPRESSION_COMPARISON_EQUAL;
  C.truth_marker = '#';
  if (!jinja_expression_parse_integer_token(I, &C.integer)) {
    context->failed = 1;
    context->capacity_exceeded = 1;
    context->error_offset = I.offset;
  }
}

atom(C) ::= FLOAT(I). {
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_FLOAT;
  C.truth_marker = '#';
  if (!jinja_expression_parse_float_token(I, &C.floating)) {
    context->failed = 1;
    context->capacity_exceeded = 1;
    context->error_offset = I.offset;
  }
}

atom(C) ::= STRING(I). {
  C = (JINJA_EXPRESSION_CONDITION){0};
  C.kind = JINJA_EXPRESSION_CONDITION_STRING;
  C.path = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.unary_not_count = 0u;
  C.boolean = 0;
  C.integer = 0;
  C.string = I.length >= 2u
                 ? (JINJA_EXPRESSION_SPAN){I.offset, I.length}
                 : (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.right_string = (JINJA_EXPRESSION_SPAN){0u, 0u};
  C.left_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.right_operand = (JINJA_EXPRESSION_OPERAND){0};
  C.comparison = JINJA_EXPRESSION_COMPARISON_EQUAL;
  C.truth_marker = '#';
  if (I.length < 2u) {
    context->failed = 1;
    context->error_offset = I.offset;
  }
}

primary(C) ::= LEFT_PAREN condition(I) RIGHT_PAREN. {
  C = I;
  C.grouped = 1;
}

primary(C) ::= LEFT_PAREN RIGHT_PAREN. {
  JINJA_EXPRESSION_COLLECTION items = {0u, SIZE_MAX};
  if (!jinja_expression_make_tuple(context, items, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
  }
}

primary(C) ::= LEFT_PAREN conditional_expression(V) COMMA tuple_tail(T) RIGHT_PAREN. {
  JINJA_EXPRESSION_COLLECTION items = {T.count + 1u, SIZE_MAX};
  if (!jinja_expression_store_collection_item(context, V, T.first_item, &items.first_item) ||
      !jinja_expression_make_tuple(context, items, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
  }
}

tuple_tail(T) ::= . {
  T.count = 0u;
  T.first_item = SIZE_MAX;
}

tuple_tail(T) ::= conditional_expression(V) list_tail(R). {
  T.count = R.count + 1u;
  T.first_item = SIZE_MAX;
  if (!jinja_expression_store_collection_item(context, V, R.first_item, &T.first_item)) {
    T.count = 0u;
  }
}

atom(C) ::= LEFT_BRACKET list_items(I) RIGHT_BRACKET. {
  if (!jinja_expression_make_list(context, I, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
  }
}

atom(C) ::= LEFT_BRACE dict_items(I) RIGHT_BRACE. {
  if (!jinja_expression_make_dict(context, I, &C)) {
    C = (JINJA_EXPRESSION_CONDITION){0};
  }
}

dict_items(I) ::= . {
  I.count = 0u;
  I.first_item = SIZE_MAX;
}

dict_items(I) ::= conditional_expression(K) COLON conditional_expression(V) dict_tail(T). {
  I.count = T.count + 1u;
  I.first_item = SIZE_MAX;
  if (!jinja_expression_store_dict_item(context, K, V, T.first_item, &I.first_item)) {
    I.count = 0u;
  }
}

dict_tail(T) ::= . {
  T.count = 0u;
  T.first_item = SIZE_MAX;
}

dict_tail(T) ::= COMMA. {
  T.count = 0u;
  T.first_item = SIZE_MAX;
}

dict_tail(T) ::= COMMA conditional_expression(K) COLON conditional_expression(V) dict_tail(R). {
  T.count = R.count + 1u;
  T.first_item = SIZE_MAX;
  if (!jinja_expression_store_dict_item(context, K, V, R.first_item, &T.first_item)) {
    T.count = 0u;
  }
}

list_items(I) ::= . {
  I.count = 0u;
  I.first_item = SIZE_MAX;
}

filter_argument(A) ::= conditional_expression(V). {
  A = (JINJA_EXPRESSION_FILTER_ARGUMENT){.value = V};
}
filter_argument(A) ::= argument_name(N) ASSIGN conditional_expression(V). {
  A = (JINJA_EXPRESSION_FILTER_ARGUMENT){.value = V, .keyword = {N.offset, N.length}};
}
filter_argument(A) ::= MULTIPLY conditional_expression(V). {
  A = (JINJA_EXPRESSION_FILTER_ARGUMENT){.value = V, .expansion = JINJA_EXPRESSION_EXPANSION_POSITIONAL};
}
filter_argument(A) ::= POWER conditional_expression(V). {
  A = (JINJA_EXPRESSION_FILTER_ARGUMENT){.value = V, .expansion = JINJA_EXPRESSION_EXPANSION_KEYWORD};
}
argument_name(N) ::= IDENTIFIER(I). { N = I; }
argument_name(N) ::= TRUE(I). { N = I; }
argument_name(N) ::= FALSE(I). { N = I; }
argument_name(N) ::= NONE(I). { N = I; }
argument_name(N) ::= NOT(I). {
  N = I;
  /* The unary-not lexer rule may consume trailing whitespace, not part of a name. */
  N.length = sizeof("not") - 1u;
}
argument_name(N) ::= AND(I). { N = I; }
argument_name(N) ::= OR(I). { N = I; }
argument_name(N) ::= IN(I). { N = I; }
argument_name(N) ::= IF(I). { N = I; }
argument_name(N) ::= ELSE(I). { N = I; }
argument_name(N) ::= IS(I). { N = I; }
filter_arguments(A) ::= . { A = (JINJA_EXPRESSION_COLLECTION){0u, SIZE_MAX}; }
filter_arguments(A) ::= filter_argument(V) filter_argument_tail(T). {
  A = (JINJA_EXPRESSION_COLLECTION){T.count + 1u, SIZE_MAX};
  /* Right-recursive tails preserve source order. At most 64 arguments make
   * these O(n^2) ordering checks bounded without a second grammar/state store. */
  for (size_t item = T.first_item; item != SIZE_MAX && !context->failed;
       item = context->tree.collection_items[item].next) {
    const JINJA_EXPRESSION_COLLECTION_ITEM *next = &context->tree.collection_items[item];
    if (V.expansion == JINJA_EXPRESSION_EXPANSION_KEYWORD ||
        (V.expansion == JINJA_EXPRESSION_EXPANSION_POSITIONAL &&
         next->expansion == JINJA_EXPRESSION_EXPANSION_POSITIONAL) ||
        ((V.keyword.length != 0u || V.expansion == JINJA_EXPRESSION_EXPANSION_POSITIONAL) &&
         next->keyword.length == 0u && next->expansion == JINJA_EXPRESSION_EXPANSION_NONE))
      context->failed = 1;
  }
  if (jinja_expression_store_collection_item(context, V.value, T.first_item, &A.first_item)) {
    context->tree.collection_items[A.first_item].keyword = V.keyword;
    context->tree.collection_items[A.first_item].expansion = V.expansion;
  } else A.count = 0u;
}
filter_argument_tail(T) ::= . { T = (JINJA_EXPRESSION_COLLECTION){0u, SIZE_MAX}; }
filter_argument_tail(T) ::= COMMA filter_arguments(A). { T = A; }

list_items(I) ::= conditional_expression(V) list_tail(T). {
  I.count = T.count + 1u;
  I.first_item = SIZE_MAX;
  if (!jinja_expression_store_collection_item(context, V, T.first_item, &I.first_item)) {
    I.count = 0u;
  }
}

list_tail(T) ::= . {
  T.count = 0u;
  T.first_item = SIZE_MAX;
}

list_tail(T) ::= COMMA. {
  T.count = 0u;
  T.first_item = SIZE_MAX;
}

list_tail(T) ::= COMMA conditional_expression(V) list_tail(R). {
  T.count = R.count + 1u;
  T.first_item = SIZE_MAX;
  if (!jinja_expression_store_collection_item(context, V, R.first_item, &T.first_item)) {
    T.count = 0u;
  }
}

path(P) ::= IDENTIFIER(I). {
  P.offset = I.offset;
  P.length = I.length;
}

%syntax_error {
  (void)yymajor;
  context->failed = 1;
  context->error_offset = TOKEN.offset;
}

%parse_failure {
  context->failed = 1;
}

%stack_overflow {
  context->failed = 1;
  context->capacity_exceeded = 1;
}

%code {
#if YYGROWABLESTACK
#error "Jinja expression workspace accounting requires a fixed parser stack"
#endif
/* Keep the generated parser layout private while letting its caller own the
 * complete allocation. No generated-file edits or global allocator hooks. */
size_t JinjaExpressionParseWorkspaceSize(void) {
  return sizeof(yyParser);
}
}
