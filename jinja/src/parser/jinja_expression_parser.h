#ifndef JINJA_EXPRESSION_PARSER_H
#define JINJA_EXPRESSION_PARSER_H

#include <vstr.h>
#include <cstl/allocator.h>

#include <stddef.h>
#include <stdint.h>

typedef struct JINJA_EXPRESSION_TOKEN {
  const char *text;
  size_t offset;
  size_t length;
} JINJA_EXPRESSION_TOKEN;

typedef struct JINJA_EXPRESSION_SPAN {
  size_t offset;
  size_t length;
} JINJA_EXPRESSION_SPAN;

typedef enum JINJA_EXPRESSION_COMPARISON_KIND {
  JINJA_EXPRESSION_COMPARISON_EQUAL,
  JINJA_EXPRESSION_COMPARISON_NOT_EQUAL,
  JINJA_EXPRESSION_COMPARISON_LESS,
  JINJA_EXPRESSION_COMPARISON_LESS_EQUAL,
  JINJA_EXPRESSION_COMPARISON_GREATER,
  JINJA_EXPRESSION_COMPARISON_GREATER_EQUAL,
  JINJA_EXPRESSION_COMPARISON_IN,
  JINJA_EXPRESSION_COMPARISON_NOT_IN
} JINJA_EXPRESSION_COMPARISON_KIND;

typedef enum JINJA_EXPRESSION_ARITHMETIC_KIND {
  JINJA_EXPRESSION_ARITHMETIC_POSITIVE,
  JINJA_EXPRESSION_ARITHMETIC_NEGATE,
  JINJA_EXPRESSION_ARITHMETIC_ADD,
  JINJA_EXPRESSION_ARITHMETIC_SUBTRACT,
  JINJA_EXPRESSION_ARITHMETIC_MULTIPLY,
  JINJA_EXPRESSION_ARITHMETIC_TRUE_DIVIDE,
  JINJA_EXPRESSION_ARITHMETIC_FLOOR_DIVIDE,
  JINJA_EXPRESSION_ARITHMETIC_MODULO,
  JINJA_EXPRESSION_ARITHMETIC_POWER
} JINJA_EXPRESSION_ARITHMETIC_KIND;

typedef enum JINJA_EXPRESSION_TEST_KIND {
  JINJA_EXPRESSION_TEST_DEFINED,
  JINJA_EXPRESSION_TEST_UNDEFINED,
  JINJA_EXPRESSION_TEST_NONE,
  JINJA_EXPRESSION_TEST_BOOLEAN,
  JINJA_EXPRESSION_TEST_TRUE,
  JINJA_EXPRESSION_TEST_FALSE,
  JINJA_EXPRESSION_TEST_INTEGER,
  JINJA_EXPRESSION_TEST_FLOAT,
  JINJA_EXPRESSION_TEST_NUMBER,
  JINJA_EXPRESSION_TEST_STRING,
  JINJA_EXPRESSION_TEST_MAPPING,
  JINJA_EXPRESSION_TEST_SEQUENCE,
  JINJA_EXPRESSION_TEST_ITERABLE,
  JINJA_EXPRESSION_TEST_CALLABLE,
  JINJA_EXPRESSION_TEST_ESCAPED,
  JINJA_EXPRESSION_TEST_FILTER,
  JINJA_EXPRESSION_TEST_TEST,
  JINJA_EXPRESSION_TEST_ODD,
  JINJA_EXPRESSION_TEST_EVEN,
  JINJA_EXPRESSION_TEST_DIVISIBLEBY,
  JINJA_EXPRESSION_TEST_SAMEAS,
  JINJA_EXPRESSION_TEST_EQUAL,
  JINJA_EXPRESSION_TEST_NOT_EQUAL,
  JINJA_EXPRESSION_TEST_LESS,
  JINJA_EXPRESSION_TEST_LESS_EQUAL,
  JINJA_EXPRESSION_TEST_GREATER,
  JINJA_EXPRESSION_TEST_GREATER_EQUAL,
  JINJA_EXPRESSION_TEST_IN
} JINJA_EXPRESSION_TEST_KIND;

typedef struct JINJA_EXPRESSION_TEST {
  JINJA_EXPRESSION_TEST_KIND kind;
  size_t offset;
  int supported;
  size_t length;
} JINJA_EXPRESSION_TEST;

typedef enum JINJA_EXPRESSION_CONDITION_KIND {
  JINJA_EXPRESSION_CONDITION_PATH,
  JINJA_EXPRESSION_CONDITION_CAPTURE,
  JINJA_EXPRESSION_CONDITION_NAMESPACE_TARGET,
  JINJA_EXPRESSION_CONDITION_BOOL,
  JINJA_EXPRESSION_CONDITION_INTEGER,
  JINJA_EXPRESSION_CONDITION_FLOAT,
  JINJA_EXPRESSION_CONDITION_STRING,
  JINJA_EXPRESSION_CONDITION_NONE,
  JINJA_EXPRESSION_CONDITION_ITEM_LOOKUP,
  JINJA_EXPRESSION_CONDITION_SLICE_LOOKUP,
  /* Compound subscript value: three collection items in start/stop/step order. */
  JINJA_EXPRESSION_CONDITION_SLICE,
  JINJA_EXPRESSION_CONDITION_CONCAT,
  JINJA_EXPRESSION_CONDITION_FILTER,
  JINJA_EXPRESSION_CONDITION_ATTRIBUTE_LOOKUP,
  JINJA_EXPRESSION_CONDITION_CALL,
  JINJA_EXPRESSION_CONDITION_TEST,
  JINJA_EXPRESSION_CONDITION_STRING_COMPARISON,
  JINJA_EXPRESSION_CONDITION_COMPARISON,
  JINJA_EXPRESSION_CONDITION_NESTED_COMPARISON,
  JINJA_EXPRESSION_CONDITION_COMPARISON_CHAIN,
  JINJA_EXPRESSION_CONDITION_UNARY_ARITHMETIC,
  JINJA_EXPRESSION_CONDITION_BINARY_ARITHMETIC,
  JINJA_EXPRESSION_CONDITION_LOGICAL_AND,
  JINJA_EXPRESSION_CONDITION_LOGICAL_OR,
  JINJA_EXPRESSION_CONDITION_CONDITIONAL,
  JINJA_EXPRESSION_CONDITION_LIST,
  JINJA_EXPRESSION_CONDITION_TUPLE,
  JINJA_EXPRESSION_CONDITION_DICT
} JINJA_EXPRESSION_CONDITION_KIND;

#define JINJA_EXPRESSION_MAX_NODES 64u
#define JINJA_EXPRESSION_MAX_REFERENCES (2u * JINJA_EXPRESSION_MAX_NODES)

typedef struct JINJA_EXPRESSION_BINDING_ACCESSES {
  size_t read_count;
  size_t write_count;
  JINJA_EXPRESSION_SPAN reads[JINJA_EXPRESSION_MAX_REFERENCES];
  JINJA_EXPRESSION_SPAN writes[JINJA_EXPRESSION_MAX_REFERENCES];
} JINJA_EXPRESSION_BINDING_ACCESSES;

#define JINJA_EXPRESSION_MAX_SCOPE_EVENTS 65536u
#define JINJA_EXPRESSION_MAX_SCOPE_DEPTH 64u

typedef enum JINJA_EXPRESSION_SCOPE_EVENT_KIND {
  JINJA_EXPRESSION_SCOPE_READ,
  JINJA_EXPRESSION_SCOPE_STORE,
  JINJA_EXPRESSION_SCOPE_PARAMETER,
  JINJA_EXPRESSION_SCOPE_BRANCH_STORE
} JINJA_EXPRESSION_SCOPE_EVENT_KIND;

typedef struct JINJA_EXPRESSION_SCOPE_EVENT {
  JINJA_EXPRESSION_SCOPE_EVENT_KIND kind;
  JINJA_EXPRESSION_SPAN name;
} JINJA_EXPRESSION_SCOPE_EVENT;

typedef enum JINJA_EXPRESSION_SCOPE_LOAD {
  JINJA_EXPRESSION_SCOPE_ARGUMENT,
  JINJA_EXPRESSION_SCOPE_RESOLVE,
  JINJA_EXPRESSION_SCOPE_ALIAS,
  JINJA_EXPRESSION_SCOPE_UNDEFINED
} JINJA_EXPRESSION_SCOPE_LOAD;

typedef struct JINJA_EXPRESSION_SCOPE_SYMBOL {
  JINJA_EXPRESSION_SPAN name;
  JINJA_EXPRESSION_SCOPE_LOAD load;
  int stored;
  /* ALIAS only: ancestor distance and symbol slot; otherwise 0 and SIZE_MAX. */
  size_t parent_depth;
  size_t parent_symbol;
} JINJA_EXPRESSION_SCOPE_SYMBOL;

typedef struct JINJA_EXPRESSION_SCOPE {
  vstr source;
  const struct JINJA_EXPRESSION_SCOPE *parent;
  size_t count;
  JINJA_EXPRESSION_SCOPE_SYMBOL symbols[JINJA_EXPRESSION_MAX_REFERENCES];
} JINJA_EXPRESSION_SCOPE;

typedef struct JINJA_EXPRESSION_SCOPE_REFERENCE {
  size_t depth;
  size_t symbol;
} JINJA_EXPRESSION_SCOPE_REFERENCE;

typedef struct JINJA_EXPRESSION_PARAMETER {
  JINJA_EXPRESSION_SPAN name;
  JINJA_EXPRESSION_SPAN default_expression;
} JINJA_EXPRESSION_PARAMETER;

typedef struct JINJA_EXPRESSION_MACRO_SIGNATURE {
  JINJA_EXPRESSION_SPAN name;
  size_t parameter_count;
  JINJA_EXPRESSION_PARAMETER parameters[JINJA_EXPRESSION_MAX_NODES];
  size_t default_reference_count;
  JINJA_EXPRESSION_SPAN default_references[JINJA_EXPRESSION_MAX_REFERENCES];
  size_t default_reference_parameters[JINJA_EXPRESSION_MAX_REFERENCES];
} JINJA_EXPRESSION_MACRO_SIGNATURE;

typedef enum JINJA_EXPRESSION_OPERAND_KIND {
  JINJA_EXPRESSION_OPERAND_PATH,
  JINJA_EXPRESSION_OPERAND_BOOL,
  JINJA_EXPRESSION_OPERAND_INTEGER,
  JINJA_EXPRESSION_OPERAND_FLOAT,
  JINJA_EXPRESSION_OPERAND_STRING
} JINJA_EXPRESSION_OPERAND_KIND;

typedef struct JINJA_EXPRESSION_OPERAND {
  JINJA_EXPRESSION_OPERAND_KIND kind;
  JINJA_EXPRESSION_SPAN span;
  int boolean;
  int64_t integer;
  double floating;
} JINJA_EXPRESSION_OPERAND;

typedef struct JINJA_EXPRESSION_CONDITION {
  JINJA_EXPRESSION_CONDITION_KIND kind;
  JINJA_EXPRESSION_SPAN path;
  size_t unary_not_count;
  /* Assignment-only spans borrow the original header, never lexer storage. */
  JINJA_EXPRESSION_SPAN namespace_attribute;
  int boolean;
  int64_t integer;
  double floating;
  JINJA_EXPRESSION_SPAN string;
  JINJA_EXPRESSION_SPAN right_string;
  JINJA_EXPRESSION_OPERAND left_operand;
  JINJA_EXPRESSION_OPERAND right_operand;
  JINJA_EXPRESSION_COMPARISON_KIND comparison;
  JINJA_EXPRESSION_ARITHMETIC_KIND arithmetic;
  JINJA_EXPRESSION_TEST_KIND test;
  JINJA_EXPRESSION_SPAN test_name;
  int test_supported;
  size_t left_condition;
  size_t right_condition;
  size_t test_condition;
  size_t first_comparison_step;
  size_t comparison_step_count;
  size_t first_collection_item;
  size_t collection_item_count;
  int has_else;
  int grouped;
  int test_arguments_supplied;
  char truth_marker;
} JINJA_EXPRESSION_CONDITION;

typedef struct JINJA_EXPRESSION_SLICE {
  JINJA_EXPRESSION_CONDITION start;
  JINJA_EXPRESSION_CONDITION stop;
  JINJA_EXPRESSION_CONDITION step;
} JINJA_EXPRESSION_SLICE;

typedef struct JINJA_EXPRESSION_CONDITIONAL_ELSE {
  JINJA_EXPRESSION_CONDITION condition;
  int has_else;
} JINJA_EXPRESSION_CONDITIONAL_ELSE;

typedef struct JINJA_EXPRESSION_COMPARISON_STEP {
  JINJA_EXPRESSION_COMPARISON_KIND comparison;
  size_t operand_condition;
  size_t next;
} JINJA_EXPRESSION_COMPARISON_STEP;

typedef enum JINJA_EXPRESSION_EXPANSION {
  JINJA_EXPRESSION_EXPANSION_NONE,
  JINJA_EXPRESSION_EXPANSION_POSITIONAL,
  JINJA_EXPRESSION_EXPANSION_KEYWORD
} JINJA_EXPRESSION_EXPANSION;

typedef struct JINJA_EXPRESSION_COLLECTION_ITEM {
  size_t value_condition;
  size_t next;
  JINJA_EXPRESSION_SPAN keyword;
  JINJA_EXPRESSION_EXPANSION expansion;
} JINJA_EXPRESSION_COLLECTION_ITEM;

typedef struct JINJA_EXPRESSION_FILTER_ARGUMENT {
  JINJA_EXPRESSION_CONDITION value;
  JINJA_EXPRESSION_SPAN keyword;
  JINJA_EXPRESSION_EXPANSION expansion;
} JINJA_EXPRESSION_FILTER_ARGUMENT;

typedef struct JINJA_EXPRESSION_DICT_ITEM {
  size_t key_condition;
  size_t value_condition;
  size_t next;
} JINJA_EXPRESSION_DICT_ITEM;

typedef struct JINJA_EXPRESSION_TREE {
  JINJA_EXPRESSION_CONDITION nodes[JINJA_EXPRESSION_MAX_NODES];
  JINJA_EXPRESSION_COMPARISON_STEP comparison_steps[JINJA_EXPRESSION_MAX_NODES];
  JINJA_EXPRESSION_COLLECTION_ITEM collection_items[JINJA_EXPRESSION_MAX_NODES];
  JINJA_EXPRESSION_DICT_ITEM dict_items[JINJA_EXPRESSION_MAX_NODES];
  size_t count;
  size_t root;
  size_t comparison_step_count;
  size_t collection_item_count;
  size_t dict_item_count;
  /* Distinguish a statement expression list from a parenthesized tuple. */
  int unparenthesized_tuple;
} JINJA_EXPRESSION_TREE;

typedef enum JINJA_TEMPLATE_REFERENCE_KIND {
  JINJA_TEMPLATE_REFERENCE_EXTENDS,
  JINJA_TEMPLATE_REFERENCE_INCLUDE,
  JINJA_TEMPLATE_REFERENCE_IMPORT,
  JINJA_TEMPLATE_REFERENCE_FROM
} JINJA_TEMPLATE_REFERENCE_KIND;

typedef struct JINJA_TEMPLATE_BLOCK_HEADER {
  JINJA_EXPRESSION_SPAN name;
  int scoped;
  int required;
} JINJA_TEMPLATE_BLOCK_HEADER;

typedef struct JINJA_TEMPLATE_FOR_HEADER {
  JINJA_EXPRESSION_SPAN target;
  JINJA_EXPRESSION_SPAN iterable;
  JINJA_EXPRESSION_SPAN test;
  JINJA_EXPRESSION_TREE targets;
  JINJA_EXPRESSION_TREE iterable_tree;
  JINJA_EXPRESSION_TREE test_tree;
  int has_test;
  int recursive;
} JINJA_TEMPLATE_FOR_HEADER;

typedef struct JINJA_TEMPLATE_IMPORT_NAME {
  JINJA_EXPRESSION_SPAN name;
  JINJA_EXPRESSION_SPAN alias;
} JINJA_TEMPLATE_IMPORT_NAME;

typedef struct JINJA_TEMPLATE_REFERENCE {
  JINJA_EXPRESSION_SPAN expression;
  JINJA_EXPRESSION_TREE tree;
  JINJA_TEMPLATE_IMPORT_NAME names[JINJA_EXPRESSION_MAX_NODES];
  size_t name_count;
  int with_context;
  int ignore_missing;
} JINJA_TEMPLATE_REFERENCE;

typedef struct JINJA_EXPRESSION_COMPARISON {
  JINJA_EXPRESSION_COMPARISON_KIND kind;
  size_t offset;
} JINJA_EXPRESSION_COMPARISON;

typedef struct JINJA_EXPRESSION_COMPARISON_TAIL {
  size_t count;
  size_t first_offset;
  size_t first_step;
} JINJA_EXPRESSION_COMPARISON_TAIL;

typedef struct JINJA_EXPRESSION_COLLECTION {
  size_t count;
  size_t first_item;
} JINJA_EXPRESSION_COLLECTION;

typedef struct JINJA_EXPRESSION_TEST_ARGUMENTS {
  JINJA_EXPRESSION_COLLECTION values;
  int supplied;
} JINJA_EXPRESSION_TEST_ARGUMENTS;

typedef struct JINJA_EXPRESSION_PARSE_CONTEXT {
  JINJA_EXPRESSION_TREE tree;
  size_t error_offset;
  int accepted;
  int failed;
  int capacity_exceeded;
} JINJA_EXPRESSION_PARSE_CONTEXT;

typedef enum JINJA_EXPRESSION_TEST_LEX_STATE {
  JINJA_EXPRESSION_TEST_LEX_NONE,
  JINJA_EXPRESSION_TEST_LEX_OPTIONAL_NOT,
  JINJA_EXPRESSION_TEST_LEX_NAME,
  JINJA_EXPRESSION_TEST_LEX_ARGUMENT
} JINJA_EXPRESSION_TEST_LEX_STATE;

typedef struct JINJA_EXPRESSION_LEXER {
  const char *input;
  const char *cursor;
  const char *limit;
  int expects_operand;
  int expects_membership_in;
  JINJA_EXPRESSION_TEST_LEX_STATE test_state;
  JINJA_EXPRESSION_TEST_LEX_STATE filter_state;
} JINJA_EXPRESSION_LEXER;

typedef enum JINJA_EXPRESSION_LEX_STATUS {
  JINJA_EXPRESSION_LEX_SYNTAX = -2,
  JINJA_EXPRESSION_LEX_UNSUPPORTED = -1,
  JINJA_EXPRESSION_LEX_EOF = 0,
  JINJA_EXPRESSION_LEX_TOKEN = 1
} JINJA_EXPRESSION_LEX_STATUS;

typedef enum JINJA_EXPRESSION_PARSE_STATUS {
  JINJA_EXPRESSION_PARSE_INVALID = 0,
  JINJA_EXPRESSION_PARSE_OK,
  JINJA_EXPRESSION_PARSE_UNSUPPORTED,
  JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY,
  JINJA_EXPRESSION_PARSE_CAPACITY
} JINJA_EXPRESSION_PARSE_STATUS;

void jinja_expression_lexer_init(JINJA_EXPRESSION_LEXER *lexer, vstr expression);
int jinja_expression_lexer_next(JINJA_EXPRESSION_LEXER *lexer, int *kind,
                                JINJA_EXPRESSION_TOKEN *token);
int jinja_expression_parse_integer_token(JINJA_EXPRESSION_TOKEN token, int64_t *value);
int jinja_expression_parse_float_token(JINJA_EXPRESSION_TOKEN token, double *value);
int jinja_expression_compare_numeric(const JINJA_EXPRESSION_CONDITION *left,
                                     JINJA_EXPRESSION_COMPARISON_KIND comparison,
                                     const JINJA_EXPRESSION_CONDITION *right, int *result);
int jinja_expression_condition_operand(const JINJA_EXPRESSION_CONDITION *condition,
                                       JINJA_EXPRESSION_OPERAND *operand);
int jinja_expression_store_condition(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                     JINJA_EXPRESSION_CONDITION condition, size_t *index);
int jinja_expression_make_unary_arithmetic(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                           JINJA_EXPRESSION_ARITHMETIC_KIND arithmetic,
                                           JINJA_EXPRESSION_CONDITION operand,
                                           JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_binary_arithmetic(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                            JINJA_EXPRESSION_ARITHMETIC_KIND arithmetic,
                                            JINJA_EXPRESSION_CONDITION left,
                                            JINJA_EXPRESSION_CONDITION right,
                                            JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_subscript_lookup(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                       JINJA_EXPRESSION_CONDITION base,
                                       JINJA_EXPRESSION_CONDITION key,
                                       JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_slice_value(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                      JINJA_EXPRESSION_SLICE slice,
                                      JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_integer_item_lookup(JINJA_EXPRESSION_PARSE_CONTEXT *context,
    JINJA_EXPRESSION_CONDITION base, JINJA_EXPRESSION_TOKEN index,
    JINJA_EXPRESSION_CONDITION *condition);

int jinja_expression_make_filter(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                 JINJA_EXPRESSION_CONDITION base, JINJA_EXPRESSION_TOKEN name,
                                 JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_concat(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                 JINJA_EXPRESSION_CONDITION left, JINJA_EXPRESSION_CONDITION right,
                                 JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_item_lookup(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                      JINJA_EXPRESSION_CONDITION base,
                                      JINJA_EXPRESSION_CONDITION key,
                                      JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_attribute_lookup(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                           JINJA_EXPRESSION_CONDITION base,
                                           JINJA_EXPRESSION_TOKEN attribute,
                                           JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_call(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                               JINJA_EXPRESSION_CONDITION callable,
                               JINJA_EXPRESSION_COLLECTION arguments,
                               JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_store_collection_item(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                           JINJA_EXPRESSION_CONDITION value, size_t next,
                                           size_t *index);
int jinja_expression_make_list(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                               JINJA_EXPRESSION_COLLECTION items,
                               JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_tuple(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                JINJA_EXPRESSION_COLLECTION items,
                                JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_store_dict_item(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                     JINJA_EXPRESSION_CONDITION key,
                                     JINJA_EXPRESSION_CONDITION value, size_t next, size_t *index);
int jinja_expression_make_dict(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                               JINJA_EXPRESSION_COLLECTION items,
                               JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_make_conditional(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                      JINJA_EXPRESSION_CONDITION true_value,
                                      JINJA_EXPRESSION_CONDITION test,
                                      const JINJA_EXPRESSION_CONDITIONAL_ELSE *false_value,
                                      JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_test_from_token(JINJA_EXPRESSION_TOKEN token, JINJA_EXPRESSION_TEST *test);
int jinja_expression_make_test(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                               JINJA_EXPRESSION_CONDITION operand, JINJA_EXPRESSION_TEST_KIND test,
                               int negated, JINJA_EXPRESSION_CONDITION *condition);
int jinja_expression_store_comparison_step(JINJA_EXPRESSION_PARSE_CONTEXT *context,
                                           JINJA_EXPRESSION_COMPARISON_KIND comparison,
                                           JINJA_EXPRESSION_CONDITION operand, size_t next,
                                           size_t *index);
JINJA_EXPRESSION_PARSE_STATUS
jinja_expression_parse_tree(vstr expression, JINJA_EXPRESSION_TREE *tree, size_t *error_offset);
/** @internal Parse using a borrowed, malloc-aligned allocator for the source
 * copy and fixed-stack parser workspace. NULL explicitly selects the default
 * allocator. Both callbacks are required otherwise; no fallback is attempted.
 * Every successful allocation is freed before return with its original size.
 * CAPACITY and OUT_OF_MEMORY are distinct. Failure preserves tree; allocation
 * admission failure also preserves error_offset. Source spans borrow input,
 * never the temporary copy. Stack-local AST construction is not heap storage.
 * Statement/header variants below propagate the same allocator recursively. */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_tree_allocated(vstr expression,
    JINJA_EXPRESSION_TREE *tree, size_t *error_offset, const stl_allocator *allocator);
/** @internal Collect lexical root reads from a successfully parsed expression.
 * tree must belong to the unchanged input (use the RHS view for assignment RHS).
 * Results are distinct names in first source-occurrence order, including reads
 * in short-circuit branches. Labels, attributes and filter/test names are excluded.
 * This does not resolve scope, classify free variables, or evaluate anything.
 * Each set is bounded by MAX_REFERENCES. Spans borrow input until modified/freed.
 * Caller exclusively owns result; failure leaves it unchanged. Invalid counts/
 * spans return INVALID, excess counts CAPACITY; error offsets are input-relative.
 * No shared state or retained allocation. Parser-produced trees are a precondition;
 * these entries are not arbitrary AST graph validators.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_collect_reads(vstr input,
    const JINJA_EXPRESSION_TREE *tree, JINJA_EXPRESSION_BINDING_ACCESSES *result,
    size_t *error_offset);
/** @internal Same ownership/error contract; accepts a parsed assignment target
 * tree with spans relative to the whole assignment input. Plain names are writes,
 * namespace owners are reads (attribute mutation is not a binding write), tuples
 * combine their children. Empty tuples produce empty sets. Non-target kinds fail.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_collect_targets(vstr input,
    const JINJA_EXPRESSION_TREE *tree, JINJA_EXPRESSION_BINDING_ACCESSES *result,
    size_t *error_offset);
/** @internal Analyze ordered name events for a single lexical frame.
 * All PARAMETER events precede reads/stores; duplicate parameters are INVALID.
 * Every store under a conditional in this frame must be BRANCH_STORE, including
 * stores in nested conditionals. Conditional tests and all branches contribute
 * reads; this describes initialization, not runtime reachability/definite values.
 * Nested frame events must be analyzed separately after their parent completes.
 * Parent must be an unchanged result of this entry, with a stable, live ancestor
 * chain and source views. Input names are exact lexical names, not paths/labels.
 * Child reads can refer directly to parent symbols; only child writes shadow.
 * Local slots follow first local definition order; ALIAS identifies an ancestor
 * slot, not a borrowed runtime value or a captured snapshot.
 * Caller owns result, all parents and input; single-threaded analysis, no allocation.
 * Parent/source lifetimes extend through consumption of this result. Do not pass
 * a parent or ancestor as result. The result remains unchanged on failure.
 * Limits: MAX_REFERENCES local symbols, MAX_SCOPE_EVENTS events, MAX_SCOPE_DEPTH
 * total frames. Excess returns CAPACITY; invalid events/spans/order return INVALID.
 * Error offsets identify the input event when supplied. This is a private binding
 * primitive: template walking, special macro arguments and runtime cells are not
 * implemented by it. No public macro execution capability is implied.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_analyze_scope(vstr input,
    const JINJA_EXPRESSION_SCOPE *parent, const JINJA_EXPRESSION_SCOPE_EVENT *events,
    size_t event_count, JINJA_EXPRESSION_SCOPE *result, size_t *error_offset);
/** @internal Locate a name in a completed lexical scope chain for lowering.
 * depth=0 selects this scope; larger depths select ancestors. symbol=SIZE_MAX
 * with depth=0 means absent, not a runtime undefined value. ALIAS symbols select
 * their own local slot: their parent reference describes initialization only.
 * Example: an inherited read through one empty frame yields {1, parent_slot}.
 * Scopes must be unchanged analyze_scope results with live source/parent views;
 * the lookup name is borrowed only during this call. Result holds indices, not
 * pointers, and is usable only with this same immutable chain. No allocation or
 * shared mutation. This does not assign runtime cell/function activation identity.
 * Invalid names, counts or spans return INVALID; chains beyond MAX_SCOPE_DEPTH
 * return CAPACITY, including cycles. NULL scope/result is INVALID. Failure leaves
 * result unchanged. Cost O(depth * symbols * name bytes), with bounded frames/slots.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_resolve_scope(
    const JINJA_EXPRESSION_SCOPE *scope, vstr name, JINJA_EXPRESSION_SCOPE_REFERENCE *result);
/* One lexical token, including whitespace, before expression-level folding.
 * Used to recognize configured template endings only at token boundaries.
 * offset is source-relative, retaining dot context for numeric token boundaries.
 * Returns zero at/beyond EOF, SIZE_MAX for malformed quoted or numeric tokens. Other
 * malformed tokens are validated by the expression parser. */
size_t jinja_expression_lexical_token_length(vstr source, size_t offset);
/** @internal Parse a complete name(parameters) header without the macro keyword.
 * All spans refer to input, which must remain valid while they are consumed.
 * Default expressions are validated with Lemon, not evaluated. Zero default
 * length means required parameter. Output unchanged on failure; offset is input-relative.
 * default_references contains distinct root names read by defaults, in first
 * source-occurrence order (including parameters and short-circuit branches).
 * Attributes, keyword labels, filter/test names and literals are not bindings.
 * At most MAX_REFERENCES names are retained; overflow returns CAPACITY. These
 * spans also borrow input. This is a read set, not resolved free-variable analysis.
 * default_reference_parameters[i] is the declared parameter index for read i,
 * or SIZE_MAX when it still requires macro-body/lexical binding analysis. All
 * declarations participate, including later parameters; no defaults are run.
 * This private parser does not expose macro compilation or runtime support.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_macro_signature(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature, size_t *error_offset);
/** @internal Parse a call-block header without the call keyword.
 * Optional caller parameters/defaults use macro signature rules; name is empty.
 * signature spans and call_span borrow input. Tree spans are relative to call_span.
 * The expression root must be CALL. signature/call_span/tree are unchanged on failure.
 * Parse error offsets are relative to input. No runtime support.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_call_header(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature,
    JINJA_EXPRESSION_SPAN *call_span, JINJA_EXPRESSION_TREE *tree, size_t *error_offset);
/** @internal Parse a template-reference header without its leading tag keyword.
 * Result spans borrow input; expression-tree spans are relative to expression.
 * IMPORT stores its module binding in names[0].alias (name is empty); FROM stores
 * each imported name and its alias, defaulting alias to name. At most MAX_NODES names.
 * Result unchanged on failure; parse error offset is input-relative. No loading/evaluation.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_template_reference(
    vstr input, JINJA_TEMPLATE_REFERENCE_KIND kind,
    JINJA_TEMPLATE_REFERENCE *result, size_t *error_offset);
/** @internal Parse block name [scoped] [required], without the block keyword.
 * Name span borrows input. Result unchanged on failure; error offset is input-relative.
 * This header parser does not validate required-block body contents or nesting.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_block_header(
    vstr input, JINJA_TEMPLATE_BLOCK_HEADER *result, size_t *error_offset);
/** @internal Parse the optional endblock name and match it against an already
 * parsed, nonempty opening name. Both views are borrowed for this call only.
 * An omitted name is valid. Error offset is relative to input, not the opening tag.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_endblock(
    vstr input, vstr opening_name, size_t *error_offset);
/** @internal Parse a for header without its keyword. Header spans borrow input;
 * each tree's spans are relative to its corresponding header span. Targets are
 * names/nested tuples only. Result unchanged on failure; error offset is input-relative.
 * Loop body/else/endfor structure and recursive execution are separate concerns.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_for_header(
    vstr input, JINJA_TEMPLATE_FOR_HEADER *result, size_t *error_offset);
/** @internal Parse an inline filter chain on a captured block value.
 * Spans refer to expression; tree remains unchanged on failure. No source rewriting.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_filter_block(
    vstr expression, JINJA_EXPRESSION_TREE *tree, size_t *error_offset);
/** @internal Parse an expression assignment or capture header (without set).
 * On success name/rhs borrow input, tree spans are relative to rhs, and targets
 * spans are relative to input. name is the bare name or full tuple source view.
 * On failure output views/tree are unchanged; error_offset is relative to input.
 * capture is nonzero for block form; its tree consumes the captured value and
 * optional filter chain, with spans relative to input. Namespace targets carry
 * separate owner path and namespace_attribute spans relative to input; only
 * unparenthesized single-level attributes are valid. Runtime support is separate.
 * For namespace and tuple targets, name borrows the complete target header slice.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_assignment(
    vstr input, vstr *name, vstr *rhs, JINJA_EXPRESSION_TREE *tree,
    JINJA_EXPRESSION_TREE *targets, int *capture, size_t *error_offset);
/** @internal Parse one with initializer. consumed excludes the separating comma.
 * rhs borrows input; RHS spans are relative to rhs, target spans to input.
 * Only name/tuple targets and expression RHS are admitted. Outputs unchanged on failure.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_with_binding(
    vstr input, size_t *consumed, vstr *rhs, JINJA_EXPRESSION_TREE *tree,
    JINJA_EXPRESSION_TREE *targets);
JINJA_EXPRESSION_PARSE_STATUS
jinja_expression_parse_condition(vstr expression, JINJA_EXPRESSION_CONDITION *condition,
                                 size_t *error_offset);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_legacy_condition(vstr expression, vstr *path,
                                                                      char *truth_marker,
                                                                      size_t *error_offset);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_legacy_path(vstr expression, vstr *path,
                                                                 size_t *error_offset);

/* Allocation-aware counterparts keep each original entry's parsing, ownership
 * and error-offset contract. NULL selects the default allocator; callbacks are
 * borrowed only through this call, and all temporary allocations are released
 * before return. Nested expression parses never replace the supplied allocator. */
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_filter_block_allocated(
    vstr expression, JINJA_EXPRESSION_TREE *tree, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_condition_allocated(vstr expression, JINJA_EXPRESSION_CONDITION *condition,
                                 size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_assignment_allocated(
    vstr input, vstr *name, vstr *rhs, JINJA_EXPRESSION_TREE *tree,
    JINJA_EXPRESSION_TREE *targets, int *capture, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_macro_signature_allocated(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_call_header_allocated(
    vstr input, JINJA_EXPRESSION_MACRO_SIGNATURE *signature,
    JINJA_EXPRESSION_SPAN *call_span, JINJA_EXPRESSION_TREE *tree, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_block_header_allocated(
    vstr input, JINJA_TEMPLATE_BLOCK_HEADER *result, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_endblock_allocated(
    vstr input, vstr opening_name, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_for_header_allocated(
    vstr input, JINJA_TEMPLATE_FOR_HEADER *result, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_template_reference_allocated(
    vstr input, JINJA_TEMPLATE_REFERENCE_KIND reference_kind,
    JINJA_TEMPLATE_REFERENCE *result, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_with_binding_allocated(
    vstr input, size_t *consumed, vstr *rhs, JINJA_EXPRESSION_TREE *tree,
    JINJA_EXPRESSION_TREE *targets, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_legacy_condition_allocated(vstr expression, vstr *path,
                                                                      char *truth_marker,
                                                                      size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_expression_parse_legacy_path_allocated(vstr expression, vstr *path,
                                                                 size_t *error_offset, const stl_allocator *allocator);

#endif
