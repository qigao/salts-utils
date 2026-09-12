#ifndef JINJA_TEMPLATE_PARSER_H
#define JINJA_TEMPLATE_PARSER_H

#include "jinja_expression_parser.h"
#include "jinja_template_lexer.h"

/* Shared private policy helpers: validated source spans only, no allocation.
 * Returned spans borrow source and remain within the input span. */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_validate_delimiters(const JINJA_TEMPLATE_DELIMITERS *delimiters);
JINJA_EXPRESSION_SPAN jinja_template_content_left(vstr source,
    JINJA_EXPRESSION_SPAN span, int control, int trim_block);
JINJA_EXPRESSION_SPAN jinja_template_content_right(vstr source,
    JINJA_EXPRESSION_SPAN span, int control, int lstrip_block);

/* Private lexical input normalization; the returned view borrows source and
 * removes at most one physical trailing newline according to configuration. */
vstr jinja_template_lexical_source(vstr source, const JINJA_TEMPLATE_DELIMITERS *delimiters);

#ifndef JINJA_TEMPLATE_MAX_DEPTH
#define JINJA_TEMPLATE_MAX_DEPTH 64u
#endif
#ifndef JINJA_TEMPLATE_MAX_BYTES
#define JINJA_TEMPLATE_MAX_BYTES (16u * 1024u * 1024u)
#endif
/* Every published node consumes at least one source byte. */
#define JINJA_TEMPLATE_MAX_NODES JINJA_TEMPLATE_MAX_BYTES

typedef enum JINJA_TEMPLATE_NODE_KIND {
  JINJA_TEMPLATE_TEXT, JINJA_TEMPLATE_COMMENT, JINJA_TEMPLATE_OUTPUT,
  JINJA_TEMPLATE_IF, JINJA_TEMPLATE_ELIF, JINJA_TEMPLATE_ELSE,
  JINJA_TEMPLATE_FOR, JINJA_TEMPLATE_BLOCK, JINJA_TEMPLATE_MACRO,
  JINJA_TEMPLATE_CALL, JINJA_TEMPLATE_SET, JINJA_TEMPLATE_CAPTURE,
  JINJA_TEMPLATE_WITH, JINJA_TEMPLATE_FILTER, JINJA_TEMPLATE_AUTOESCAPE,
  JINJA_TEMPLATE_EXTENDS, JINJA_TEMPLATE_INCLUDE, JINJA_TEMPLATE_IMPORT,
  JINJA_TEMPLATE_FROM, JINJA_TEMPLATE_PRINT, JINJA_TEMPLATE_END,
  JINJA_TEMPLATE_RAW, JINJA_TEMPLATE_DO, JINJA_TEMPLATE_BREAK,
  JINJA_TEMPLATE_CONTINUE, JINJA_TEMPLATE_DEBUG, JINJA_TEMPLATE_TRANS,
  JINJA_TEMPLATE_PLURALIZE, JINJA_TEMPLATE_EXTENSION_TAG
} JINJA_TEMPLATE_NODE_KIND;

typedef enum JINJA_TEMPLATE_EXTENSION {
  JINJA_TEMPLATE_EXTENSION_DO = 1u << 0,
  JINJA_TEMPLATE_EXTENSION_LOOP_CONTROLS = 1u << 1,
  JINJA_TEMPLATE_EXTENSION_DEBUG = 1u << 2,
  JINJA_TEMPLATE_EXTENSION_I18N = 1u << 3,
  JINJA_TEMPLATE_EXTENSION_CUSTOM = 1u << 4
} JINJA_TEMPLATE_EXTENSION;

typedef struct JINJA_TEMPLATE_NODE {
  JINJA_TEMPLATE_NODE_KIND kind;
  JINJA_EXPRESSION_SPAN source;
  JINJA_EXPRESSION_SPAN header;
  JINJA_EXPRESSION_SPAN name;
  /* TEXT/RAW only: effective literal bytes, derived without changing source/header. */
  JINJA_EXPRESSION_SPAN content;
  size_t parent;
  size_t match;
  size_t branch;
  /* -1 trims, +1 preserves, 0 uses the environment's whitespace policy. */
  int left_control;
  int right_control;
  int required;
  int scoped;
  /* TRANS only: 0 uses environment policy, +1 trimmed, -1 notrimmed. */
  int translation_trim;
  /* Statement source includes its physical line ending instead of a closing delimiter. */
  int line_statement;
  int line_comment;
  /* RAW body edges: opening tag's right control and closing tag's left control. */
  int raw_left_control;
  int raw_right_control;
} JINJA_TEMPLATE_NODE;

typedef struct JINJA_TEMPLATE_TREE {
  struct JINJA_TEMPLATE_STORAGE *storage;
  JINJA_TEMPLATE_NODE *nodes;
  size_t count;
} JINJA_TEMPLATE_TREE;

/** @internal Release owned nodes and zero the tree; NULL is accepted.
 * Zero-initialize before first parse. Do not shallow-copy owning trees.
 * Node and source-span views expire at successful reparse or destruction. */
void jinja_template_tree_destroy(JINJA_TEMPLATE_TREE *tree);

/** @internal Parse a complete configured-delimiter template without evaluation.
 * delimiters and its six nonempty UTF-8 token views are borrowed for this call.
 * Each delimiter is at most MAX_DELIMITER_BYTES; opening strings must differ.
 * Invalid configuration returns INVALID, excessive lengths return CAPACITY.
 * Default syntax is selected explicitly with JINJA_TEMPLATE_DEFAULT_DELIMITERS.
 * All spans borrow source, which must stay immutable and alive while consumed.
 * Nodes are source-ordered. parent identifies the enclosing block/branch;
 * match pairs block open/end, branch links successive elif/else branches.
 * Absent links are SIZE_MAX. Headers are validated with the expression parser;
 * their original source is retained for subsequent lowering, not rewritten.
 * Statement headers exclude the keyword and optional block colon; OUTPUT and
 * COMMENT headers exclude delimiters/controls and surrounding whitespace.
 * RAW header spans its untrimmed body; source retains both raw delimiters and
 * their internal whitespace controls. TEXT uses source only. Controls on RAW
 * describe the outside edges, not its body's trim policy. No whitespace is
 * removed from source/header. TEXT/RAW content projects the effective literal
 * span after explicit controls and configured whitespace policies. Other node
 * kinds have empty content. Lowering consumes content, not source/header text.
 * trim_blocks, lstrip_blocks and keep_trailing_newline accept only 0 or 1;
 * defaults are 0. CRLF is one physical newline for trimming. Retained line
 * endings remain original bytes; output newline normalization belongs to lowering.
 * Delimiters and line prefixes match CRLF/CR source as LF; configuration bytes
 * are not normalized. All published spans and errors use original byte offsets.
 * Caller exclusively owns a zero-initialized or previously parsed result.
 * Successful reparse replaces its storage; no state is shared between calls. Failure
 * leaves result unchanged and reports a source-relative error_offset when supplied.
 * Input, node and depth limits return CAPACITY; allocation failure returns OOM.
 * Each with header admits at most JINJA_EXPRESSION_MAX_NODES bindings.
 * extensions explicitly enables the named JINJA_TEMPLATE_EXTENSION bits.
 * Unknown bits return INVALID; disabled extension tags return UNSUPPORTED.
 * Loop-control scope legality belongs to lowering, not syntax parsing.
 * I18N enables trans/pluralize/endtrans. TRANS name retains the optional quoted
 * context token; translation_trim records policy without modifying text.
 * TRANS header retains bindings; at most MAX_NODES distinct names are allowed.
 * PLURALIZE name is its optional explicit count; branch links it from TRANS.
 * Within translations OUTPUT name retains its sole lexical placeholder.
 * TEXT/RAW/COMMENT remain source-preserving; statements other than pluralize
 * and endtrans are rejected. This entry does not translate or evaluate messages.
 * Optional line prefixes borrow UTF-8 views with the same byte budget. NULL/0
 * disables a prefix; a non-NULL empty view enables it. Prefix priority uses
 * Unicode scalar counts, while source spans remain byte-based.
 * Line statements permit bracket/string continuation and consume their ending
 * newline; line comments retain the newline as TEXT. Raw bodies ignore prefixes.
 * Empty comments never publish zero-progress tokens at a newline or EOF.
 * Raw inner controls are recorded separately from its outside edges. Content
 * projection is O(source bytes + nodes), allocation-free and call-local.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_parse(
    const JINJA_TEMPLATE_DELIMITERS *delimiters, unsigned extensions,
    vstr source, JINJA_TEMPLATE_TREE *result, size_t *error_offset);

/** @internal Analyze one lexical frame without evaluating or compiling it.
 * tree must be parser-produced from this exact, immutable source. SIZE_MAX selects
 * the template root; otherwise opener selects MACRO/CALL/WITH/FILTER/CAPTURE/
 * AUTOESCAPE, BLOCK or FOR body. Other frame kinds return UNSUPPORTED.
 * BLOCK requires a NULL parent (including scoped blocks), otherwise INVALID.
 * Root/BLOCK inject self and BLOCK injects super when find_undeclared discovers
 * them in the body, before other symbols. Discovery crosses nested closures and
 * rejects unsupported DEBUG/I18N there, but skips nested BLOCK bodies.
 * MACRO/CALL use analyze_macro_bindings to validate caller and declare implicit
 * caller/kwargs/varargs after explicit parameters, before default reads.
 * WITH declares distinct targets as parameters;
 * its initializers belong to the parent. FILTER reads arguments after its body;
 * CAPTURE visits body only (not outer target or tail filter), matching RootVisitor.
 * AUTOESCAPE reads its policy in the selected child frame before body stores.
 * Parent is a completed, frozen scope; source, tree and
 * parent chain borrow caller storage, with the lifetime rules of analyze_scope.
 * Parameters precede defaults/body; assignments read RHS before storing targets.
 * IF branches share this frame. Nested scopes contribute only parent-frame header
 * accesses, not their bodies. AUTOESCAPE/BLOCK contribute none; I18N/DEBUG in the
 * selected frame return UNSUPPORTED. This does not implement closures, automatic
 * child-frame discovery or control-flow reachability, nor enable macro execution.
 * Work is call-local and bounded by MAX_SCOPE_EVENTS; no shared mutable state.
 * Input/node/event/symbol/depth limits return CAPACITY, allocation failure OOM,
 * invalid spans/selection INVALID. Failure preserves result; optional error_offset
 * is relative to the full source. Output symbol spans borrow the full source.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_frame(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, const JINJA_EXPRESSION_SCOPE *parent,
    JINJA_EXPRESSION_SCOPE *result, size_t *error_offset);

typedef enum JINJA_TEMPLATE_FOR_BRANCH {
  JINJA_TEMPLATE_FOR_BODY, JINJA_TEMPLATE_FOR_TEST, JINJA_TEMPLATE_FOR_ELSE
} JINJA_TEMPLATE_FOR_BRANCH;

/** @internal Analyze a selected FOR branch using analyze_frame's ownership,
 * limits and atomic failure contract. opener must select FOR. BODY/TEST declare
 * distinct target parameters, then visit only body/test respectively. ELSE has
 * no target parameters. Absent test retains parameters; absent else is empty.
 * All three borrow the enclosing parent, not each other. Iterable reads belong
 * to the enclosing frame. No synthetic LoopContext symbols are inserted.
 * Invalid branch or non-FOR selection returns INVALID. analyze_frame selects
 * BODY when given a FOR opener. This is symbol analysis, not loop execution.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_for_frame(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, JINJA_TEMPLATE_FOR_BRANCH branch,
    const JINJA_EXPRESSION_SCOPE *parent, JINJA_EXPRESSION_SCOPE *result, size_t *error_offset);

/** @internal Discover one undeclared Name in a selected body, not a lexical scope.
 * SIZE_MAX selects root; other selections use analyze_frame's opener kinds.
 * The selected header is excluded; FOR selects body only, without test/else.
 * Nested closures are traversed, BLOCK is skipped. Structural AST field order
 * applies: targets precede values; FOR test and FILTER arguments follow bodies.
 * Only Name loads/stores/parameters count, not macro/import labels or NSRef targets.
 * The first Name context decides: a read publishes a source-relative occurrence;
 * a store/parameter suppresses discovery. No occurrence publishes {0,0}.
 * name is a nonempty borrowed byte string matched exactly (including Unicode).
 * Parser-produced tree and exact immutable source obey analyze_frame's limits
 * and borrowing contract. The entire selected body is checked even after discovery;
 * unsupported DEBUG/I18N returns UNSUPPORTED, malformed metadata INVALID.
 * Work is single-call owned, bounded and freed before return. Failure preserves
 * reference; error_offset is source-relative. No synthetic parameter is injected.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_find_undeclared(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, vstr name,
    JINJA_EXPRESSION_SPAN *reference, size_t *error_offset);

typedef enum JINJA_TEMPLATE_MACRO_SPECIAL {
  JINJA_TEMPLATE_MACRO_CALLER, JINJA_TEMPLATE_MACRO_KWARGS,
  JINJA_TEMPLATE_MACRO_VARARGS, JINJA_TEMPLATE_MACRO_SPECIAL_COUNT
} JINJA_TEMPLATE_MACRO_SPECIAL;

typedef struct JINJA_TEMPLATE_MACRO_BINDINGS {
  /* Bit (1u << SPECIAL): caller use, or acceptance of extra keywords/positions.
   * Explicit kwargs/varargs never set their bit. Explicit caller may set its bit. */
  unsigned accesses;
  /* Nonempty only for an implicit parameter, in caller/kwargs/varargs order. */
  JINJA_EXPRESSION_SPAN implicit[JINJA_TEMPLATE_MACRO_SPECIAL_COUNT];
} JINJA_TEMPLATE_MACRO_BINDINGS;

typedef struct JINJA_TEMPLATE_MACRO_DESCRIPTOR {
  JINJA_EXPRESSION_MACRO_SIGNATURE signature;
  JINJA_TEMPLATE_MACRO_BINDINGS bindings;
  /* Half-open tree node interval; excludes the opener and matching END. */
  size_t body_begin;
  size_t body_end;
  /* CALL only: expression that receives the anonymous caller. */
  JINJA_EXPRESSION_SPAN call;
} JINJA_TEMPLATE_MACRO_DESCRIPTOR;

/** @internal Build immutable lowering metadata for one MACRO or CALL node.
 * Requires the exact parser-produced tree and immutable source. Every nonempty
 * signature/default/reference/call span is relative to the full source, not a
 * header substring; absent spans are {0,0}. Parameter reference indices retain
 * signature semantics. No expression is evaluated and no runtime is enabled.
 * Source/tree remain caller-owned; descriptor contains no allocated ownership.
 * Body indices refer to tree, while spans borrow source through lowering.
 * Work is single-call owned, bounded by the existing tree/signature limits.
 * INVALID/UNSUPPORTED/CAPACITY/OOM propagate from syntax/capability analysis;
 * errors are source-relative and failure preserves the caller's descriptor.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_describe_macro(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener,
    JINJA_TEMPLATE_MACRO_DESCRIPTOR *result, size_t *error_offset);

/** @internal Analyze MACRO/CALL capabilities for subsequent lowering.
 * Only body discovery triggers special arguments; explicit kwargs/varargs suppress
 * their implicit counterpart. A used explicit caller must have a default, otherwise
 * INVALID with the parameter's source offset. Unused explicit caller is ordinary.
 * Exact parser-produced tree/source, limits and borrowed spans obey find_undeclared.
 * Non-MACRO/CALL selection returns INVALID. Failure preserves result; all scratch
 * is call-owned and released before return. No evaluation, retained state or callbacks.
 * analyze_frame consumes this result to declare implicit parameters after explicit
 * ones but before defaults. This metadata does not implement macro invocation.
 */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_macro_bindings(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener,
    JINJA_TEMPLATE_MACRO_BINDINGS *result, size_t *error_offset);

/* Private streaming boundary shared with compilation. Source/configuration and
 * opening token must already be validated by the caller. Does not parse statement
 * semantics or impose a whole-tree node budget. Header excludes controls; RAW
 * header is its untrimmed body. Success advances lexer past this tag/raw region
 * and publishes result; failure leaves result unchanged, lexer must be abandoned.
 * Spans borrow source; caller keeps it immutable while consuming the result. */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_scan_tag(vstr source, JINJA_TEMPLATE_LEXER *lexer,
    JINJA_TEMPLATE_TOKEN opening, JINJA_TEMPLATE_NODE *result, size_t *error_offset);

/* Allocation-aware counterparts share the original semantics. NULL explicitly
 * selects default allocation. The parse result copies allocator callbacks and
 * borrows their context through tree destruction; analysis borrows them only
 * for the call. Failed reparse preserves the original tree and its allocator.
 * Node capacity, owner metadata, growth overlap and nested parser work all use
 * this allocator. Each owner releases its exact requested sizes once. */
JINJA_EXPRESSION_PARSE_STATUS jinja_template_parse_allocated(const JINJA_TEMPLATE_DELIMITERS *delimiters,
    unsigned extensions, vstr source,
    JINJA_TEMPLATE_TREE *result, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_frame_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, const JINJA_EXPRESSION_SCOPE *parent,
    JINJA_EXPRESSION_SCOPE *result, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_for_frame_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, JINJA_TEMPLATE_FOR_BRANCH branch,
    const JINJA_EXPRESSION_SCOPE *parent, JINJA_EXPRESSION_SCOPE *result, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_template_find_undeclared_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, vstr name,
    JINJA_EXPRESSION_SPAN *reference, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_template_describe_macro_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener,
    JINJA_TEMPLATE_MACRO_DESCRIPTOR *result, size_t *error_offset, const stl_allocator *allocator);
JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_macro_bindings_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener,
    JINJA_TEMPLATE_MACRO_BINDINGS *result, size_t *error_offset, const stl_allocator *allocator);

#endif
