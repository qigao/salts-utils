typedef struct JINJA_TEMPLATE_NODE JINJA_TEMPLATE_NODE;
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST
#define CMETA_KNOWN_TYPE_LIST CMETA_BUILTIN_TYPE_LIST, \
  (JinjaTemplateNode, JINJA_TEMPLATE_NODE, jinja_template_node_type, CMETA_T_OBJECT, jinja_template_node_traits)

#include "jinja_template_parser.h"
#include "jinja_parser_memory.h"
#include <cstl/vec_alloc.h>
#include "jinja_template_lexer.h"
#include "jinja_expression_grammar_gen.h"
#include <salts_unicode.h>
#include <stdlib.h>
#include <string.h>
#include <cstl/typed.h>

static bool jinja_template_node_copy(void *destination, const void *source) {
  if (destination == NULL || source == NULL) return false;
  memcpy(destination, source, sizeof(JINJA_TEMPLATE_NODE));
  return true;
}

static void jinja_template_node_move(void *destination, void *source) {
  memcpy(destination, source, sizeof(JINJA_TEMPLATE_NODE));
  memset(source, 0, sizeof(JINJA_TEMPLATE_NODE));
}

static void jinja_template_node_destroy(void *value) {
  memset(value, 0, sizeof(JINJA_TEMPLATE_NODE));
}

static const cmeta_type_traits jinja_template_node_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
    NULL, NULL, NULL, jinja_template_node_copy, jinja_template_node_move, jinja_template_node_destroy};
const cmeta_type_desc jinja_template_node_type = {
    "jinja.TemplateNode", sizeof(JINJA_TEMPLATE_NODE), _Alignof(JINJA_TEMPLATE_NODE),
    CMETA_T_OBJECT, NULL, &jinja_template_node_traits, NULL};

struct JINJA_TEMPLATE_STORAGE {
  vec_alloc_t *nodes;
  stl_allocator allocator;
};

void jinja_template_tree_destroy(JINJA_TEMPLATE_TREE *tree) {
  if (tree == NULL) return;
  if (tree->storage != NULL) {
    const stl_allocator allocator = tree->storage->allocator;
    vec_alloc_destroy(tree->storage->nodes);
    jinja_parser_deallocate(&allocator, tree->storage, sizeof(*tree->storage));
  }
  *tree = (JINJA_TEMPLATE_TREE){0};
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_storage_status(stl_status status) {
  if (status == STL_OK) return JINJA_EXPRESSION_PARSE_OK;
  if (status == STL_OUT_OF_MEMORY) return JINJA_EXPRESSION_PARSE_OUT_OF_MEMORY;
  if (status == STL_CAPACITY_EXCEEDED) return JINJA_EXPRESSION_PARSE_CAPACITY;
  return JINJA_EXPRESSION_PARSE_INVALID;
}

typedef struct JINJA_TEMPLATE_FRAME {
  size_t opening;
  size_t branch;
  int has_else;
} JINJA_TEMPLATE_FRAME;

typedef struct JINJA_TEMPLATE_WORK {
  stl_allocator allocator;
  JINJA_TEMPLATE_TREE tree;
  JINJA_TEMPLATE_FRAME frames[JINJA_TEMPLATE_MAX_DEPTH];
  size_t depth;
  unsigned extensions;
  JINJA_EXPRESSION_SPAN translation_names[JINJA_EXPRESSION_MAX_NODES];
  size_t translation_name_count;
  int translation_has_count;
  union {
    JINJA_TEMPLATE_FOR_HEADER loop;
    JINJA_TEMPLATE_REFERENCE reference;
    struct {
      JINJA_EXPRESSION_MACRO_SIGNATURE signature;
      JINJA_EXPRESSION_TREE expression;
      JINJA_EXPRESSION_TREE targets;
    } expressions;
  } scratch;
} JINJA_TEMPLATE_WORK;

typedef struct JINJA_TEMPLATE_BINDING_WORK {
  stl_allocator allocator;
  JINJA_EXPRESSION_SCOPE_EVENT events[JINJA_EXPRESSION_MAX_SCOPE_EVENTS];
  size_t count;
  JINJA_EXPRESSION_BINDING_ACCESSES accesses;
  union {
    JINJA_TEMPLATE_FOR_HEADER loop;
    JINJA_TEMPLATE_REFERENCE reference;
    struct {
      JINJA_EXPRESSION_MACRO_SIGNATURE signature;
      JINJA_EXPRESSION_TREE expression;
      JINJA_EXPRESSION_TREE targets;
    } expressions;
  } scratch;
} JINJA_TEMPLATE_BINDING_WORK;

static int jinja_template_space(salts_unicode_scalar scalar) {
  enum { SEPARATOR_FIRST = 0x1c, SEPARATOR_LAST = 0x1f };
  return (scalar.properties & SALTS_UNICODE_PROPERTY_WHITE_SPACE) != 0u ||
      (scalar.value >= SEPARATOR_FIRST && scalar.value <= SEPARATOR_LAST);
}

static JINJA_EXPRESSION_SPAN jinja_template_trim(vstr source, JINJA_EXPRESSION_SPAN span) {
  size_t cursor = span.offset, end = span.offset + span.length, first = end, last = end;
  while (cursor < end) {
    salts_unicode_scalar scalar;
    size_t before = cursor;
    if (salts_unicode_utf8_next(source, &cursor, &scalar) != SALTS_UNICODE_OK) break;
    if (!jinja_template_space(scalar)) {
      if (first == end) first = before;
      last = cursor;
    }
  }
  return (JINJA_EXPRESSION_SPAN){first, last - first};
}

static size_t jinja_template_skip_space(vstr source, size_t cursor) {
  while (cursor < source.len) {
    salts_unicode_scalar scalar;
    size_t next = cursor;
    if (salts_unicode_utf8_next(source, &next, &scalar) != SALTS_UNICODE_OK || !jinja_template_space(scalar)) break;
    cursor = next;
  }
  return cursor;
}

static vstr jinja_template_view(vstr source, JINJA_EXPRESSION_SPAN span) {
  return vstr_from_buf(source.data + span.offset, span.length);
}

static int jinja_template_word(vstr word, const char *expected) {
  return word.len == strlen(expected) && memcmp(word.data, expected, word.len) == 0;
}

static int jinja_template_open(JINJA_TEMPLATE_TOKEN_KIND kind) {
  return kind == JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN || kind == JINJA_TEMPLATE_TOKEN_STATEMENT_OPEN ||
      kind == JINJA_TEMPLATE_TOKEN_COMMENT_OPEN || kind == JINJA_TEMPLATE_TOKEN_LINE_STATEMENT ||
      kind == JINJA_TEMPLATE_TOKEN_LINE_COMMENT;
}

/* The line-ending rule is greedy whitespace followed by a newline or EOF.
 * Retain original CR/LF byte ranges; normalization is a lowering policy. */
static size_t jinja_template_line_end(vstr source, size_t cursor) {
  size_t end = SIZE_MAX;
  while (cursor < source.len) {
    salts_unicode_scalar scalar;
    size_t next = cursor;
    if (salts_unicode_utf8_next(source, &next, &scalar) != SALTS_UNICODE_OK || !jinja_template_space(scalar)) break;
    if (scalar.value == '\r' || scalar.value == '\n') end = next;
    cursor = next;
  }
  return cursor == source.len ? cursor : end;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_line_tag(vstr source, JINJA_TEMPLATE_LEXER *lexer,
    JINJA_TEMPLATE_TOKEN open, JINJA_TEMPLATE_NODE *node, size_t *failure) {
  const int comment = open.kind == JINJA_TEMPLATE_TOKEN_LINE_COMMENT;
  size_t begin = open.offset + open.length, cursor, end = SIZE_MAX, depth = 0u;
  char brackets[JINJA_EXPRESSION_MAX_NODES];
  if (begin < source.len && (source.data[begin] == '-' || source.data[begin] == '+'))
    node->left_control = source.data[begin++] == '-' ? -1 : 1;
  cursor = begin;
  while (cursor < source.len) {
    if (comment) {
      if (source.data[cursor] == '\r' || source.data[cursor] == '\n') { end = cursor; break; }
      ++cursor;
      continue;
    }
    if (depth == 0u && (end = jinja_template_line_end(source, cursor)) != SIZE_MAX) break;
    size_t length = jinja_expression_lexical_token_length(source, cursor);
    if (length == SIZE_MAX) { *failure = cursor; return JINJA_EXPRESSION_PARSE_INVALID; }
    char c = source.data[cursor];
    if (length == 1u) {
      if (c == '(' || c == '[' || c == '{') {
        if (depth == JINJA_EXPRESSION_MAX_NODES) { *failure = cursor; return JINJA_EXPRESSION_PARSE_CAPACITY; }
        brackets[depth++] = c == '(' ? ')' : c == '[' ? ']' : '}';
      } else if (c == ')' || c == ']' || c == '}') {
        if (depth == 0u || brackets[--depth] != c) { *failure = cursor; return JINJA_EXPRESSION_PARSE_INVALID; }
      }
    }
    cursor += length;
  }
  if (depth != 0u) { *failure = open.offset; return JINJA_EXPRESSION_PARSE_INVALID; }
  if (end == SIZE_MAX) end = source.len;
  node->kind = comment ? JINJA_TEMPLATE_COMMENT : JINJA_TEMPLATE_END;
  node->line_statement = !comment;
  node->line_comment = comment;
  node->source = (JINJA_EXPRESSION_SPAN){open.offset, end - open.offset};
  node->header = jinja_template_trim(source, (JINJA_EXPRESSION_SPAN){begin, cursor - begin});
  jinja_template_lexer_set_offset(lexer, end);
  return JINJA_EXPRESSION_PARSE_OK;
}

static int jinja_translation_name(int kind) {
  return kind == JINJA_EXPRESSION_TOKEN_IDENTIFIER || kind == JINJA_EXPRESSION_TOKEN_TRUE ||
      kind == JINJA_EXPRESSION_TOKEN_FALSE || kind == JINJA_EXPRESSION_TOKEN_NONE ||
      kind == JINJA_EXPRESSION_TOKEN_AND || kind == JINJA_EXPRESSION_TOKEN_OR ||
      kind == JINJA_EXPRESSION_TOKEN_NOT || kind == JINJA_EXPRESSION_TOKEN_IN ||
      kind == JINJA_EXPRESSION_TOKEN_IS || kind == JINJA_EXPRESSION_TOKEN_IF ||
      kind == JINJA_EXPRESSION_TOKEN_ELSE;
}

static int jinja_translation_has_name(vstr source, const JINJA_TEMPLATE_WORK *work, JINJA_EXPRESSION_SPAN name) {
  for (size_t i = 0u; i < work->translation_name_count; ++i) {
    JINJA_EXPRESSION_SPAN previous = work->translation_names[i];
    if (previous.length == name.length && memcmp(source.data + previous.offset, source.data + name.offset, name.length) == 0)
      return 1;
  }
  return 0;
}

/* A trans context is one string token, unlike expression-level adjacent strings.
 * Binding expressions use Lemon after a depth-aware comma scan. At most 64
 * names are retained; duplicate detection is quadratic in that bounded count. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_translation_header(vstr source, JINJA_TEMPLATE_WORK *work,
    JINJA_TEMPLATE_NODE *node, size_t *failure) {
  vstr input = jinja_template_view(source, node->header);
  size_t start = 0u, relative = 0u;
  JINJA_EXPRESSION_TREE *tree = &work->scratch.expressions.expression;
  work->translation_name_count = 0u;
  work->translation_has_count = 0;
  if (input.len != 0u && (input.data[0] == '\'' || input.data[0] == '"')) {
    const char quote = input.data[0];
    start = 1u;
    while (start < input.len) {
      char c = input.data[start++];
      if (c == quote) break;
      if (c == '\\' && start < input.len) ++start;
    }
    JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_tree_allocated(vstr_from_buf(input.data, start), tree, &relative, &work->allocator);
    if (status != JINJA_EXPRESSION_PARSE_OK) { *failure = node->header.offset + relative; return status; }
    node->name = (JINJA_EXPRESSION_SPAN){node->header.offset, start};
  }
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token = {0};
  int kind = 0, lex_status;
  jinja_expression_lexer_init(&lexer, vstr_from_buf(input.data + start, input.len - start));
  const size_t base = node->header.offset + start;
  lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
  while (lex_status > 0) {
    *failure = base + token.offset;
    if (work->translation_name_count != 0u) {
      if (kind != JINJA_EXPRESSION_TOKEN_COMMA) return JINJA_EXPRESSION_PARSE_INVALID;
      lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
      if (lex_status <= 0) return JINJA_EXPRESSION_PARSE_INVALID;
    }
    if (kind == JINJA_EXPRESSION_TOKEN_COLON) {
      lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
      return lex_status == 0 ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_INVALID;
    }
    if (!jinja_translation_name(kind)) return JINJA_EXPRESSION_PARSE_INVALID;
    JINJA_EXPRESSION_SPAN name = {base + token.offset,
        kind == JINJA_EXPRESSION_TOKEN_NOT ? sizeof("not") - 1u : token.length};
    if (jinja_translation_has_name(source, work, name)) return JINJA_EXPRESSION_PARSE_INVALID;
    lex_status = jinja_expression_lexer_next(&lexer, &kind, &token);
    if (lex_status > 0 && kind == JINJA_EXPRESSION_TOKEN_ASSIGN) {
      const size_t expression_start = (size_t)(lexer.cursor - lexer.input);
      size_t depth = 0u;
      while ((lex_status = jinja_expression_lexer_next(&lexer, &kind, &token)) > 0) {
        if (depth == 0u && kind == JINJA_EXPRESSION_TOKEN_COMMA) break;
        if (kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN || kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACKET ||
            kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACE) ++depth;
        else if (kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN || kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET ||
                 kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACE) {
          if (depth == 0u) return JINJA_EXPRESSION_PARSE_INVALID;
          --depth;
        }
      }
      const size_t end = lex_status == 0 ? (size_t)(lexer.limit - lexer.input) : token.offset;
      if (lex_status < 0) return JINJA_EXPRESSION_PARSE_INVALID;
      JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_tree_allocated(
          vstr_from_buf(lexer.input + expression_start, end - expression_start), tree, &relative, &work->allocator);
      if (status != JINJA_EXPRESSION_PARSE_OK) { *failure = base + expression_start + relative; return status; }
      if (tree->unparenthesized_tuple) return JINJA_EXPRESSION_PARSE_INVALID;
    } else if (node->translation_trim == 0 &&
        (jinja_template_word(jinja_template_view(source, name), "trimmed") ||
         jinja_template_word(jinja_template_view(source, name), "notrimmed"))) {
      node->translation_trim = jinja_template_word(jinja_template_view(source, name), "trimmed") ? 1 : -1;
      continue;
    }
    if (work->translation_name_count == JINJA_EXPRESSION_MAX_NODES) return JINJA_EXPRESSION_PARSE_CAPACITY;
    work->translation_names[work->translation_name_count++] = name;
    work->translation_has_count = 1;
  }
  return lex_status == 0 ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_INVALID;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_translation_placeholder(vstr source, JINJA_TEMPLATE_NODE *node) {
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token;
  int kind;
  jinja_expression_lexer_init(&lexer, jinja_template_view(source, node->header));
  if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0 || !jinja_translation_name(kind))
    return JINJA_EXPRESSION_PARSE_INVALID;
  node->name = (JINJA_EXPRESSION_SPAN){node->header.offset + token.offset,
      kind == JINJA_EXPRESSION_TOKEN_NOT ? sizeof("not") - 1u : token.length};
  return jinja_expression_lexer_next(&lexer, &kind, &token) == 0
      ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_INVALID;
}

/* A sign is a whitespace control only when the complete signed ending matches. */
static size_t jinja_template_close_at(vstr source, size_t offset, vstr delimiter, int allow_plus) {
  if (offset < source.len && (source.data[offset] == '-' || (allow_plus && source.data[offset] == '+')) &&
      jinja_template_delimiter_end(source, offset + 1u, delimiter) != SIZE_MAX) return offset + 1u;
  return jinja_template_delimiter_end(source, offset, delimiter) != SIZE_MAX ? offset : SIZE_MAX;
}

/* Raw's greedy whitespace must still leave a complete ending, which itself
 * may begin with whitespace. Keep the last match at a Unicode boundary. */
static size_t jinja_template_raw_close(vstr source, size_t offset, vstr delimiter,
    int allow_plus, size_t *content_end) {
  size_t result = SIZE_MAX;
  for (;;) {
    size_t close = jinja_template_close_at(source, offset, delimiter, allow_plus);
    if (close != SIZE_MAX) { result = close; *content_end = offset; }
    if (offset == source.len) break;
    salts_unicode_scalar scalar;
    size_t next = offset;
    if (salts_unicode_utf8_next(source, &next, &scalar) != SALTS_UNICODE_OK || !jinja_template_space(scalar)) break;
    offset = next;
  }
  return result;
}

/* Complete raw openings precede ordinary longest-opening matching in Jinja. */
static int jinja_template_prefer_raw(vstr source, JINJA_TEMPLATE_LEXER *lexer,
    JINJA_TEMPLATE_TOKEN *token, JINJA_TEMPLATE_NODE *node) {
  vstr opening = lexer->delimiters->tokens[JINJA_TEMPLATE_TOKEN_STATEMENT_OPEN - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN];
  vstr ending = lexer->delimiters->tokens[JINJA_TEMPLATE_TOKEN_STATEMENT_CLOSE - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN];
  const size_t opening_end = jinja_template_delimiter_end(source, token->offset, opening);
  if (opening_end == SIZE_MAX) return 0;
  size_t begin = opening_end;
  if (begin < source.len && (source.data[begin] == '-' || source.data[begin] == '+'))
    node->left_control = source.data[begin++] == '-' ? -1 : 1;
  begin = jinja_template_skip_space(source, begin);
  const size_t name_length = sizeof("raw") - 1u;
  if (name_length > source.len - begin || memcmp(source.data + begin, "raw", name_length) != 0) return 0;
  node->header = (JINJA_EXPRESSION_SPAN){begin, name_length};
  const size_t close = jinja_template_raw_close(source, begin + name_length, ending, 0, &begin);
  if (close == SIZE_MAX) return 0;
  const size_t end = jinja_template_delimiter_end(source, close, ending);
  node->source = (JINJA_EXPRESSION_SPAN){token->offset, end - token->offset};
  node->kind = JINJA_TEMPLATE_END;
  node->right_control = close != begin ? -1 : 0;
  token->kind = JINJA_TEMPLATE_TOKEN_STATEMENT_OPEN;
  token->length = opening_end - token->offset;
  jinja_template_lexer_set_offset(lexer, end);
  return 1;
}

/* Endings are recognized at lexical boundaries, not inside names, numbers,
 * strings or multi-byte operators. Lemon remains responsible for expressions. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_template_tag(vstr source, JINJA_TEMPLATE_LEXER *lexer,
    JINJA_TEMPLATE_TOKEN open, JINJA_TEMPLATE_NODE *node, size_t *failure) {
  char brackets[JINJA_EXPRESSION_MAX_NODES];
  size_t depth = 0u, close = SIZE_MAX;
  const vstr delimiter = lexer->delimiters->tokens[open.kind - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN + 1u];
  const int comment = open.kind == JINJA_TEMPLATE_TOKEN_COMMENT_OPEN;
  node->source.offset = open.offset;
  node->kind = open.kind == JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN ? JINJA_TEMPLATE_OUTPUT :
      comment ? JINJA_TEMPLATE_COMMENT : JINJA_TEMPLATE_END;
  size_t begin = open.offset + open.length;
  if (begin < source.len && (source.data[begin] == '-' || source.data[begin] == '+'))
    node->left_control = source.data[begin++] == '-' ? -1 : 1;
  size_t cursor = begin;
  while (cursor < source.len) {
    if (depth == 0u) {
      close = jinja_template_close_at(source, cursor, delimiter, open.kind != JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN);
      if (close != SIZE_MAX) break;
    }
    if (comment) { ++cursor; continue; }
    const size_t length = jinja_expression_lexical_token_length(source, cursor);
    if (length == SIZE_MAX) { *failure = cursor; return JINJA_EXPRESSION_PARSE_INVALID; }
    char c = source.data[cursor];
    if (length == 1u) {
      if (c == '(' || c == '[' || c == '{') {
        if (depth == JINJA_EXPRESSION_MAX_NODES) { *failure = cursor; return JINJA_EXPRESSION_PARSE_CAPACITY; }
        brackets[depth++] = c == '(' ? ')' : c == '[' ? ']' : '}';
      } else if (c == ')' || c == ']' || c == '}') {
        if (depth == 0u || brackets[--depth] != c) { *failure = cursor; return JINJA_EXPRESSION_PARSE_INVALID; }
      }
    }
    cursor += length;
  }
  if (close == SIZE_MAX) { *failure = open.offset; return JINJA_EXPRESSION_PARSE_INVALID; }
  const size_t end = jinja_template_delimiter_end(source, close, delimiter);
  node->source.length = end - open.offset;
  if (close != cursor) node->right_control = source.data[cursor] == '-' ? -1 : 1;
  node->header = jinja_template_trim(source, (JINJA_EXPRESSION_SPAN){begin, cursor - begin});
  jinja_template_lexer_set_offset(lexer, end);
  return JINJA_EXPRESSION_PARSE_OK;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_raw(vstr source, JINJA_TEMPLATE_LEXER *lexer,
    JINJA_TEMPLATE_NODE *node, size_t *failure) {
  JINJA_TEMPLATE_TOKEN token;
  node->raw_left_control = node->right_control;
  size_t body = node->source.offset + node->source.length;
  if (node->raw_left_control == -1)
    jinja_template_lexer_set_offset(lexer, jinja_template_skip_space(source, body));
  const vstr delimiter = lexer->delimiters->tokens[JINJA_TEMPLATE_TOKEN_STATEMENT_CLOSE - JINJA_TEMPLATE_TOKEN_VARIABLE_OPEN];
  while (jinja_template_lexer_raw_next(lexer, &token) > 0) {
    if (token.kind != JINJA_TEMPLATE_TOKEN_STATEMENT_OPEN) continue;
    jinja_template_lexer_set_offset(lexer, token.offset + 1u);
    /* Invalid statements inside raw are data, including unterminated quotes. */
    size_t begin = token.offset + token.length;
    int inner_control = 0;
    if (begin < source.len && (source.data[begin] == '-' || source.data[begin] == '+'))
      inner_control = source.data[begin++] == '-' ? -1 : 1;
    begin = jinja_template_skip_space(source, begin);
    const size_t name_length = sizeof("endraw") - 1u;
    if (name_length > source.len - begin || memcmp(source.data + begin, "endraw", name_length) != 0) continue;
    size_t content_end = begin + name_length;
    size_t end = jinja_template_raw_close(source, content_end, delimiter, 1, &content_end);
    if (end == SIZE_MAX) continue;
    node->kind = JINJA_TEMPLATE_RAW;
    node->raw_right_control = inner_control;
    node->header = (JINJA_EXPRESSION_SPAN){body, token.offset - body};
    const size_t after = jinja_template_delimiter_end(source, end, delimiter);
    node->source.length = after - node->source.offset;
    node->right_control = content_end < end ? (source.data[content_end] == '-' ? -1 : 1) : 0;
    jinja_template_lexer_set_offset(lexer, after);
    return JINJA_EXPRESSION_PARSE_OK;
  }
  *failure = node->source.offset;
  return JINJA_EXPRESSION_PARSE_INVALID;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_scan_tag(vstr source, JINJA_TEMPLATE_LEXER *lexer,
    JINJA_TEMPLATE_TOKEN opening, JINJA_TEMPLATE_NODE *result, size_t *error_offset) {
  JINJA_TEMPLATE_NODE node = {.parent = SIZE_MAX, .match = SIZE_MAX, .branch = SIZE_MAX};
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_OK;
  size_t failure = opening.offset;
  if (lexer == NULL || result == NULL || !jinja_template_open(opening.kind))
    return JINJA_EXPRESSION_PARSE_INVALID;
  if (!jinja_template_prefer_raw(source, lexer, &opening, &node)) {
    if (opening.kind == JINJA_TEMPLATE_TOKEN_LINE_STATEMENT || opening.kind == JINJA_TEMPLATE_TOKEN_LINE_COMMENT)
      status = jinja_template_line_tag(source, lexer, opening, &node, &failure);
    else status = jinja_template_tag(source, lexer, opening, &node, &failure);
  }
  if (status == JINJA_EXPRESSION_PARSE_OK && node.kind == JINJA_TEMPLATE_END &&
      !node.line_statement && node.right_control != 1 &&
      jinja_template_word(jinja_template_view(source, node.header), "raw"))
    status = jinja_template_raw(source, lexer, &node, &failure);
  if (status == JINJA_EXPRESSION_PARSE_OK) *result = node;
  else if (error_offset != NULL) *error_offset = failure;
  return status;
}

/* if/elif use parse_tuple(with_condexpr=False); containers and parentheses
 * re-enter normal expression syntax. The lexer carries shorthand-test state. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_template_if_test(vstr input,
    JINJA_EXPRESSION_TREE *tree, size_t *failure, const stl_allocator *allocator) {
  void *source_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS status =
      jinja_parser_allocate(allocator, input.len + 1u, &source_storage);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  char *terminated = (char *)source_storage;
  if (input.len != 0u) memcpy(terminated, input.data, input.len);
  terminated[input.len] = 0;
  JINJA_EXPRESSION_LEXER lexer;
  JINJA_EXPRESSION_TOKEN token;
  int kind = 0, previous = 0, invalid = 0;
  size_t nesting = 0u;
  jinja_expression_lexer_init(&lexer, vstr_from_buf(terminated, input.len));
  for (;;) {
    int expects_operand = lexer.expects_operand;
    int test_argument = lexer.test_state == JINJA_EXPRESSION_TEST_LEX_ARGUMENT;
    if (jinja_expression_lexer_next(&lexer, &kind, &token) <= 0) break;
    if (nesting == 0u && kind == JINJA_EXPRESSION_TOKEN_IF && !expects_operand &&
        !test_argument && previous != JINJA_EXPRESSION_TOKEN_DOT) {
      *failure = token.offset;
      invalid = 1;
      break;
    }
    if (kind == JINJA_EXPRESSION_TOKEN_LEFT_PAREN || kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACKET ||
        kind == JINJA_EXPRESSION_TOKEN_LEFT_BRACE) ++nesting;
    else if (kind == JINJA_EXPRESSION_TOKEN_RIGHT_PAREN || kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET ||
        kind == JINJA_EXPRESSION_TOKEN_RIGHT_BRACE) {
      if (nesting != 0u) --nesting;
    }
    previous = kind;
  }
  jinja_parser_deallocate(allocator, terminated, input.len + 1u);
  if (invalid) return JINJA_EXPRESSION_PARSE_INVALID;
  return jinja_expression_parse_tree_allocated(input, tree, failure, allocator);
}

static const char *jinja_template_ending(JINJA_TEMPLATE_NODE_KIND kind) {
  switch (kind) {
    case JINJA_TEMPLATE_IF: return "endif";
    case JINJA_TEMPLATE_FOR: return "endfor";
    case JINJA_TEMPLATE_BLOCK: return "endblock";
    case JINJA_TEMPLATE_MACRO: return "endmacro";
    case JINJA_TEMPLATE_CALL: return "endcall";
    case JINJA_TEMPLATE_CAPTURE: return "endset";
    case JINJA_TEMPLATE_WITH: return "endwith";
    case JINJA_TEMPLATE_FILTER: return "endfilter";
    case JINJA_TEMPLATE_AUTOESCAPE: return "endautoescape";
    case JINJA_TEMPLATE_TRANS: return "endtrans";
    default: return NULL;
  }
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_statement(vstr source, JINJA_TEMPLATE_WORK *work,
    JINJA_TEMPLATE_NODE *node, size_t *failure) {
  static const struct { const char *word; JINJA_TEMPLATE_NODE_KIND kind; } tags[] = {
    {"if", JINJA_TEMPLATE_IF}, {"elif", JINJA_TEMPLATE_ELIF}, {"else", JINJA_TEMPLATE_ELSE},
    {"for", JINJA_TEMPLATE_FOR}, {"block", JINJA_TEMPLATE_BLOCK}, {"macro", JINJA_TEMPLATE_MACRO},
    {"call", JINJA_TEMPLATE_CALL}, {"set", JINJA_TEMPLATE_SET}, {"with", JINJA_TEMPLATE_WITH},
    {"filter", JINJA_TEMPLATE_FILTER}, {"autoescape", JINJA_TEMPLATE_AUTOESCAPE},
    {"extends", JINJA_TEMPLATE_EXTENDS}, {"include", JINJA_TEMPLATE_INCLUDE},
    {"import", JINJA_TEMPLATE_IMPORT}, {"from", JINJA_TEMPLATE_FROM}, {"print", JINJA_TEMPLATE_PRINT},
    {"do", JINJA_TEMPLATE_DO}, {"break", JINJA_TEMPLATE_BREAK},
    {"continue", JINJA_TEMPLATE_CONTINUE}, {"debug", JINJA_TEMPLATE_DEBUG},
    {"trans", JINJA_TEMPLATE_TRANS}, {"pluralize", JINJA_TEMPLATE_PLURALIZE}
  };
  vstr header = jinja_template_view(source, node->header);
  size_t length = 0u;
  while (length < header.len) {
    size_t next = length;
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(header, &next, &scalar) != SALTS_UNICODE_OK || jinja_template_space(scalar)) break;
    uint32_t c = scalar.value;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c >= 0x80u)) break;
    length = next;
  }
  vstr word = vstr_from_buf(header.data, length);
  node->header.offset += length;
  node->header.length -= length;
  node->header = jinja_template_trim(source, node->header);
  vstr payload = jinja_template_view(source, node->header);
  size_t relative = 0u;
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_INVALID;
  JINJA_TEMPLATE_FRAME *translation = work->depth != 0u &&
      work->tree.nodes[work->frames[work->depth - 1u].opening].kind == JINJA_TEMPLATE_TRANS
          ? &work->frames[work->depth - 1u] : NULL;
  if (translation != NULL && !jinja_template_word(word, "endtrans") &&
      !jinja_template_word(word, "pluralize")) return JINJA_EXPRESSION_PARSE_INVALID;
  if (word.len >= 3u && memcmp(word.data, "end", 3u) == 0) {
    if (work->depth == 0u) return status;
    JINJA_TEMPLATE_FRAME *frame = &work->frames[work->depth - 1u];
    JINJA_TEMPLATE_NODE *opening = &work->tree.nodes[frame->opening];
    const char *ending = jinja_template_ending(opening->kind);
    if (ending == NULL || !jinja_template_word(word, ending)) return status;
    if (opening->kind == JINJA_TEMPLATE_BLOCK)
      status = jinja_expression_parse_endblock_allocated(payload, jinja_template_view(source, opening->name), &relative, &work->allocator);
    else status = payload.len == 0u ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_INVALID;
    if (status != JINJA_EXPRESSION_PARSE_OK) { *failure = node->header.offset + relative; return status; }
    node->kind = JINJA_TEMPLATE_END;
    node->match = frame->opening;
    node->parent = opening->parent;
    opening->match = work->tree.count;
    --work->depth;
    return status;
  }
  size_t tag_index;
  for (tag_index = 0u; tag_index < sizeof(tags) / sizeof(tags[0]); ++tag_index)
    if (jinja_template_word(word, tags[tag_index].word)) break;
  if (tag_index == sizeof(tags) / sizeof(tags[0])) {
    if ((work->extensions & JINJA_TEMPLATE_EXTENSION_CUSTOM) == 0u)
      return JINJA_EXPRESSION_PARSE_UNSUPPORTED;
    node->kind = JINJA_TEMPLATE_EXTENSION_TAG;
    node->name = (JINJA_EXPRESSION_SPAN){(size_t)(word.data - source.data), length};
  } else node->kind = tags[tag_index].kind;
  unsigned extension = node->kind == JINJA_TEMPLATE_DO ? JINJA_TEMPLATE_EXTENSION_DO :
      node->kind == JINJA_TEMPLATE_DEBUG ? JINJA_TEMPLATE_EXTENSION_DEBUG :
      (node->kind == JINJA_TEMPLATE_TRANS || node->kind == JINJA_TEMPLATE_PLURALIZE)
          ? JINJA_TEMPLATE_EXTENSION_I18N :
      (node->kind == JINJA_TEMPLATE_BREAK || node->kind == JINJA_TEMPLATE_CONTINUE)
          ? JINJA_TEMPLATE_EXTENSION_LOOP_CONTROLS : 0u;
  if (extension != 0u && (work->extensions & extension) == 0u)
    return JINJA_EXPRESSION_PARSE_UNSUPPORTED;
  int colon = payload.len != 0u && payload.data[payload.len - 1u] == ':';
  if (colon && node->kind != JINJA_TEMPLATE_WITH && node->kind != JINJA_TEMPLATE_TRANS &&
      (jinja_template_ending(node->kind) != NULL || node->kind == JINJA_TEMPLATE_ELIF ||
       node->kind == JINJA_TEMPLATE_ELSE || node->kind == JINJA_TEMPLATE_SET)) {
    --node->header.length;
    node->header = jinja_template_trim(source, node->header);
    payload = jinja_template_view(source, node->header);
  } else colon = 0;
  JINJA_EXPRESSION_TREE *expression = &work->scratch.expressions.expression;
  JINJA_EXPRESSION_TREE *targets = &work->scratch.expressions.targets;
  switch (node->kind) {
    case JINJA_TEMPLATE_TRANS:
      status = jinja_translation_header(source, work, node, failure);
      if (status != JINJA_EXPRESSION_PARSE_OK) return status;
      break;
    case JINJA_TEMPLATE_PLURALIZE:
      if (translation == NULL || translation->has_else) return JINJA_EXPRESSION_PARSE_INVALID;
      if (payload.len != 0u) {
        status = jinja_translation_placeholder(source, node);
        if (status != JINJA_EXPRESSION_PARSE_OK || !jinja_translation_has_name(source, work, node->name))
          return JINJA_EXPRESSION_PARSE_INVALID;
      } else if (!work->translation_has_count) return JINJA_EXPRESSION_PARSE_INVALID;
      work->tree.nodes[translation->opening].branch = work->tree.count;
      node->parent = translation->opening;
      translation->branch = work->tree.count;
      translation->has_else = 1;
      return JINJA_EXPRESSION_PARSE_OK;
    case JINJA_TEMPLATE_BLOCK: {
      JINJA_TEMPLATE_BLOCK_HEADER block;
      status = jinja_expression_parse_block_header_allocated(payload, &block, &relative, &work->allocator);
      if (status == JINJA_EXPRESSION_PARSE_OK) {
        node->name = block.name;
        node->name.offset += node->header.offset;
        node->required = block.required;
        node->scoped = block.scoped;
      }
      break;
    }
    case JINJA_TEMPLATE_FOR:
      status = jinja_expression_parse_for_header_allocated(payload, &work->scratch.loop, &relative, &work->allocator); break;
    case JINJA_TEMPLATE_MACRO:
      status = jinja_expression_parse_macro_signature_allocated(payload, &work->scratch.expressions.signature, &relative, &work->allocator); break;
    case JINJA_TEMPLATE_CALL: {
      JINJA_EXPRESSION_SPAN call;
      status = jinja_expression_parse_call_header_allocated(payload, &work->scratch.expressions.signature, &call, expression, &relative, &work->allocator);
      break;
    }
    case JINJA_TEMPLATE_SET: {
      vstr name, rhs;
      int capture = 0;
      status = jinja_expression_parse_assignment_allocated(payload, &name, &rhs, expression, targets, &capture, &relative, &work->allocator);
      if (status == JINJA_EXPRESSION_PARSE_OK) {
        if (capture) node->kind = JINJA_TEMPLATE_CAPTURE;
        else if (colon) status = JINJA_EXPRESSION_PARSE_INVALID;
      }
      break;
    }
    case JINJA_TEMPLATE_WITH: {
      status = JINJA_EXPRESSION_PARSE_OK;
      size_t consumed = 0u, binding_count = 0u;
      vstr rhs;
      while (payload.len != 0u) {
        if (binding_count++ == JINJA_EXPRESSION_MAX_NODES) { status = JINJA_EXPRESSION_PARSE_CAPACITY; break; }
        status = jinja_expression_parse_with_binding_allocated(payload, &consumed, &rhs, expression, targets, &work->allocator);
        if (status != JINJA_EXPRESSION_PARSE_OK) break;
        if (consumed == payload.len) break;
        payload.data += consumed + 1u;
        payload.len -= consumed + 1u;
        if (payload.len == 0u) { status = JINJA_EXPRESSION_PARSE_INVALID; break; }
      }
      break;
    }
    case JINJA_TEMPLATE_FILTER:
      status = jinja_expression_parse_filter_block_allocated(payload, expression, &relative, &work->allocator); break;
    case JINJA_TEMPLATE_EXTENDS: case JINJA_TEMPLATE_INCLUDE:
    case JINJA_TEMPLATE_IMPORT: case JINJA_TEMPLATE_FROM: {
      JINJA_TEMPLATE_REFERENCE_KIND kind = node->kind == JINJA_TEMPLATE_EXTENDS ? JINJA_TEMPLATE_REFERENCE_EXTENDS :
          node->kind == JINJA_TEMPLATE_INCLUDE ? JINJA_TEMPLATE_REFERENCE_INCLUDE :
          node->kind == JINJA_TEMPLATE_IMPORT ? JINJA_TEMPLATE_REFERENCE_IMPORT : JINJA_TEMPLATE_REFERENCE_FROM;
      status = jinja_expression_parse_template_reference_allocated(payload, kind, &work->scratch.reference, &relative, &work->allocator);
      break;
    }
    case JINJA_TEMPLATE_BREAK: case JINJA_TEMPLATE_CONTINUE: case JINJA_TEMPLATE_DEBUG:
    case JINJA_TEMPLATE_ELSE:
      status = payload.len == 0u ? JINJA_EXPRESSION_PARSE_OK : JINJA_EXPRESSION_PARSE_INVALID; break;
    case JINJA_TEMPLATE_EXTENSION_TAG:
      status = payload.len == 0u ? JINJA_EXPRESSION_PARSE_OK :
          jinja_expression_parse_tree_allocated(payload, expression, &relative, &work->allocator);
      break;
    case JINJA_TEMPLATE_IF: case JINJA_TEMPLATE_ELIF:
      status = jinja_template_if_test(payload, expression, &relative, &work->allocator); break;
    default:
      if (node->kind == JINJA_TEMPLATE_PRINT && payload.len == 0u) status = JINJA_EXPRESSION_PARSE_OK;
      else status = jinja_expression_parse_tree_allocated(payload, expression, &relative, &work->allocator);
      if (status == JINJA_EXPRESSION_PARSE_OK &&
          ((node->kind == JINJA_TEMPLATE_AUTOESCAPE && expression->unparenthesized_tuple) ||
           (node->kind == JINJA_TEMPLATE_PRINT && payload.len != 0u && payload.data[payload.len - 1u] == ',')))
        status = JINJA_EXPRESSION_PARSE_INVALID;
      break;
  }
  if (status != JINJA_EXPRESSION_PARSE_OK) { *failure = node->header.offset + relative; return status; }
  if (node->kind == JINJA_TEMPLATE_ELSE || node->kind == JINJA_TEMPLATE_ELIF) {
    if (work->depth == 0u) return JINJA_EXPRESSION_PARSE_INVALID;
    JINJA_TEMPLATE_FRAME *frame = &work->frames[work->depth - 1u];
    JINJA_TEMPLATE_NODE *opening = &work->tree.nodes[frame->opening];
    if (frame->has_else || (opening->kind != JINJA_TEMPLATE_IF &&
        (opening->kind != JINJA_TEMPLATE_FOR || node->kind != JINJA_TEMPLATE_ELSE)))
      return JINJA_EXPRESSION_PARSE_INVALID;
    work->tree.nodes[frame->branch].branch = work->tree.count;
    frame->branch = work->tree.count;
    frame->has_else = node->kind == JINJA_TEMPLATE_ELSE;
    node->parent = frame->opening;
  } else if (jinja_template_ending(node->kind) != NULL) {
    if (work->depth == JINJA_TEMPLATE_MAX_DEPTH) return JINJA_EXPRESSION_PARSE_CAPACITY;
    work->frames[work->depth++] = (JINJA_TEMPLATE_FRAME){work->tree.count, work->tree.count, 0};
  }
  return status;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_store(JINJA_TEMPLATE_WORK *work,
    vstr source, JINJA_TEMPLATE_NODE node) {
  if (work->tree.count == JINJA_TEMPLATE_MAX_NODES) return JINJA_EXPRESSION_PARSE_CAPACITY;
  /* Required blocks cannot hide statements or interpolation behind conditions. */
  if (node.parent != SIZE_MAX && work->tree.nodes[node.parent].required) {
    if (node.kind != JINJA_TEMPLATE_COMMENT &&
        ((node.kind != JINJA_TEMPLATE_TEXT && node.kind != JINJA_TEMPLATE_RAW) ||
         jinja_template_trim(source, node.kind == JINJA_TEMPLATE_RAW ? node.header : node.source).length != 0u))
      return JINJA_EXPRESSION_PARSE_INVALID;
  }
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_template_storage_status(
      vec_alloc_push(work->tree.storage->nodes, &node));
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  work->tree.nodes = (JINJA_TEMPLATE_NODE *)vec_alloc_at(work->tree.storage->nodes, 0u);
  work->tree.count = vec_size(vec_alloc_view(work->tree.storage->nodes));
  return status;
}

static size_t jinja_template_newline_length(vstr source, size_t offset, size_t end) {
  if (offset == end) return 0u;
  if (source.data[offset] == '\n') return 1u;
  if (source.data[offset] != '\r') return 0u;
  return offset + 1u < end && source.data[offset + 1u] == '\n' ? 2u : 1u;
}

JINJA_EXPRESSION_SPAN jinja_template_content_left(vstr source,
    JINJA_EXPRESSION_SPAN span, int control, int trim_block) {
  const size_t end = span.offset + span.length;
  if (control == -1) {
    size_t next = jinja_template_skip_space(vstr_from_buf(source.data, end), span.offset);
    span.offset = next;
  } else if (control == 0 && trim_block)
    span.offset += jinja_template_newline_length(source, span.offset, end);
  span.length = end - span.offset;
  return span;
}

/* O(span bytes), using Unicode properties rather than byte-wise whitespace.
 * Only the current lexical data segment participates: never strip across tags. */
JINJA_EXPRESSION_SPAN jinja_template_content_right(vstr source,
    JINJA_EXPRESSION_SPAN span, int control, int lstrip_block) {
  if (control == 1 || (control == 0 && !lstrip_block)) return span;
  size_t cursor = span.offset, end = span.offset + span.length;
  size_t last_nonspace = span.offset, line = span.offset;
  int line_start = span.offset == 0u || source.data[span.offset - 1u] == '\n' || source.data[span.offset - 1u] == '\r';
  while (cursor < end) {
    salts_unicode_scalar scalar;
    if (salts_unicode_utf8_next(source, &cursor, &scalar) != SALTS_UNICODE_OK) break;
    if (!jinja_template_space(scalar)) last_nonspace = cursor;
    if (scalar.value == '\n' || scalar.value == '\r') { line = cursor; line_start = 1; }
  }
  if (control == -1) span.length = last_nonspace - span.offset;
  else if (line_start && last_nonspace <= line) span.length = line - span.offset;
  return span;
}

/* Content is a disposable projection of source/header, never a second source
 * of truth. Two bounded scans per literal; no allocation or source mutation. */
static void jinja_template_project_content(vstr source, const JINJA_TEMPLATE_DELIMITERS *config,
    JINJA_TEMPLATE_TREE *tree) {
  for (size_t i = 0u; i < tree->count; ++i) {
    JINJA_TEMPLATE_NODE *node = &tree->nodes[i];
    if (node->kind == JINJA_TEMPLATE_RAW) {
      node->content = jinja_template_content_left(source, node->header, node->raw_left_control, 0);
      node->content = jinja_template_content_right(source, node->content,
          node->raw_right_control, config->lstrip_blocks);
    } else if (node->kind == JINJA_TEMPLATE_TEXT) {
      node->content = node->source;
      if (i != 0u) {
        const JINJA_TEMPLATE_NODE *previous = &tree->nodes[i - 1u];
        const int block = previous->kind != JINJA_TEMPLATE_OUTPUT &&
            !previous->line_statement && !previous->line_comment;
        node->content = jinja_template_content_left(source, node->content,
            previous->right_control, config->trim_blocks && block);
      }
      if (i + 1u < tree->count) {
        const JINJA_TEMPLATE_NODE *next = &tree->nodes[i + 1u];
        node->content = jinja_template_content_right(source, node->content,
            next->left_control, config->lstrip_blocks && next->kind != JINJA_TEMPLATE_OUTPUT);
      } else if (!config->keep_trailing_newline && node->content.length != 0u) {
        size_t end = node->content.offset + node->content.length;
        if (source.data[end - 1u] == '\n') {
          --node->content.length;
          if (node->content.length != 0u && source.data[end - 2u] == '\r') --node->content.length;
        } else if (source.data[end - 1u] == '\r') --node->content.length;
      }
    }
  }
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_validate_delimiters(const JINJA_TEMPLATE_DELIMITERS *delimiters) {
  if (delimiters == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  if ((delimiters->trim_blocks != 0 && delimiters->trim_blocks != 1) ||
      (delimiters->lstrip_blocks != 0 && delimiters->lstrip_blocks != 1) ||
      (delimiters->keep_trailing_newline != 0 && delimiters->keep_trailing_newline != 1))
    return JINJA_EXPRESSION_PARSE_INVALID;
  for (size_t i = 0u; i < JINJA_TEMPLATE_DELIMITER_COUNT; ++i) {
    vstr value = delimiters->tokens[i];
    if (!vstr_is_valid(value) || value.len == 0u) return JINJA_EXPRESSION_PARSE_INVALID;
    if (value.len > JINJA_TEMPLATE_MAX_DELIMITER_BYTES) return JINJA_EXPRESSION_PARSE_CAPACITY;
    if (vstr_utf8_invalid_offset(value) != VSTR_NPOS) return JINJA_EXPRESSION_PARSE_INVALID;
    if (i % 2u == 0u) for (size_t j = 0u; j < i; j += 2u) {
      vstr previous = delimiters->tokens[j];
      if (value.len == previous.len && memcmp(value.data, previous.data, value.len) == 0)
        return JINJA_EXPRESSION_PARSE_INVALID;
    }
  }
  const vstr prefixes[] = {delimiters->line_statement_prefix, delimiters->line_comment_prefix};
  for (size_t i = 0u; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
    if (!vstr_is_valid(prefixes[i])) return JINJA_EXPRESSION_PARSE_INVALID;
    if (prefixes[i].len > JINJA_TEMPLATE_MAX_DELIMITER_BYTES) return JINJA_EXPRESSION_PARSE_CAPACITY;
    if (prefixes[i].data != NULL && vstr_utf8_invalid_offset(prefixes[i]) != VSTR_NPOS)
      return JINJA_EXPRESSION_PARSE_INVALID;
  }
  return JINJA_EXPRESSION_PARSE_OK;
}

vstr jinja_template_lexical_source(vstr source, const JINJA_TEMPLATE_DELIMITERS *delimiters) {
  if (!delimiters->keep_trailing_newline && source.len != 0u) {
    if (source.data[source.len - 1u] == '\n') {
      --source.len;
      if (source.len != 0u && source.data[source.len - 1u] == '\r') --source.len;
    } else if (source.data[source.len - 1u] == '\r') --source.len;
  }
  return source;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_parse(const JINJA_TEMPLATE_DELIMITERS *delimiters,
    unsigned extensions, vstr source,
    JINJA_TEMPLATE_TREE *result, size_t *error_offset) {
  return jinja_template_parse_allocated(delimiters, extensions, source, result, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_parse_allocated(const JINJA_TEMPLATE_DELIMITERS *delimiters,
    unsigned extensions, vstr source,
    JINJA_TEMPLATE_TREE *result, size_t *error_offset, const stl_allocator *allocator) {
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_OK;
  size_t failure = 0u, cursor = 0u;
  if (!vstr_is_valid(source) || result == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  if (delimiters == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  status = jinja_template_validate_delimiters(delimiters);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  const unsigned supported = JINJA_TEMPLATE_EXTENSION_DO | JINJA_TEMPLATE_EXTENSION_LOOP_CONTROLS |
      JINJA_TEMPLATE_EXTENSION_DEBUG | JINJA_TEMPLATE_EXTENSION_I18N |
      JINJA_TEMPLATE_EXTENSION_CUSTOM;
  if ((extensions & ~supported) != 0u) return JINJA_EXPRESSION_PARSE_INVALID;
  if (source.len > JINJA_TEMPLATE_MAX_BYTES) return JINJA_EXPRESSION_PARSE_CAPACITY;
  failure = vstr_utf8_invalid_offset(source);
  if (failure != VSTR_NPOS) {
    if (error_offset != NULL) *error_offset = failure;
    return JINJA_EXPRESSION_PARSE_INVALID;
  }
  const vstr original_source = source;
  /* Jinja removes one trailing physical newline before lexing. Keep the full
   * borrowed view separately so diagnostic and literal spans retain its bytes. */
  source = jinja_template_lexical_source(source, delimiters);
  void *work_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_zero(allocator, sizeof(JINJA_TEMPLATE_WORK), &work_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  JINJA_TEMPLATE_WORK *work = (JINJA_TEMPLATE_WORK *)work_storage;
  work->allocator = jinja_parser_allocator_copy(allocator);
  void *tree_storage = NULL;
  status = jinja_parser_zero(allocator, sizeof(*work->tree.storage), &tree_storage);
  if (status != JINJA_EXPRESSION_PARSE_OK) {
    jinja_parser_deallocate(allocator, work, sizeof(*work));
    return status;
  }
  work->tree.storage = (struct JINJA_TEMPLATE_STORAGE *)tree_storage;
  work->tree.storage->allocator = work->allocator;
  status = jinja_template_storage_status(vec_alloc_new(&jinja_template_node_type,
      original_source.len != 0u ? original_source.len : 1u,
      &work->allocator, &work->tree.storage->nodes));
  if (status != JINJA_EXPRESSION_PARSE_OK) {
    jinja_template_tree_destroy(&work->tree);
    jinja_parser_deallocate(allocator, work, sizeof(*work));
    return status;
  }
  work->extensions = extensions;
  JINJA_TEMPLATE_LEXER lexer;
  JINJA_TEMPLATE_TOKEN token;
  jinja_template_lexer_init(&lexer, source.data, source.len, delimiters);
  for (;;) {
    int lex_status = jinja_template_lexer_open_next(&lexer, &token);
    if (lex_status == 0) token.offset = original_source.len;
    failure = token.offset;
    if (lex_status < 0) { status = JINJA_EXPRESSION_PARSE_INVALID; break; }
    if (lex_status != 0 && !jinja_template_open(token.kind)) continue;
    size_t parent = work->depth != 0u ? work->frames[work->depth - 1u].branch : SIZE_MAX;
    if (token.offset != cursor) {
      JINJA_TEMPLATE_NODE text = {.kind = JINJA_TEMPLATE_TEXT,
          .source = {cursor, token.offset - cursor}, .parent = parent, .match = SIZE_MAX, .branch = SIZE_MAX};
      status = jinja_template_store(work, original_source, text);
      if (status != JINJA_EXPRESSION_PARSE_OK) { failure = cursor; break; }
    }
    if (lex_status == 0) break;
    if (work->tree.count == JINJA_TEMPLATE_MAX_NODES) { status = JINJA_EXPRESSION_PARSE_CAPACITY; break; }
    JINJA_TEMPLATE_NODE node = {.parent = parent, .match = SIZE_MAX, .branch = SIZE_MAX};
    status = jinja_template_scan_tag(source, &lexer, token, &node, &failure);
    if (status != JINJA_EXPRESSION_PARSE_OK) break;
    node.parent = parent;
    if (node.kind == JINJA_TEMPLATE_END) {
      status = jinja_template_statement(source, work, &node, &failure);
    } else if (node.kind == JINJA_TEMPLATE_OUTPUT) {
      size_t relative = 0u;
      if (work->depth != 0u && work->tree.nodes[work->frames[work->depth - 1u].opening].kind == JINJA_TEMPLATE_TRANS) {
        status = jinja_translation_placeholder(source, &node);
        if (!work->frames[work->depth - 1u].has_else) work->translation_has_count = 1;
      } else status = jinja_expression_parse_tree_allocated(jinja_template_view(source, node.header),
          &work->scratch.expressions.expression, &relative, allocator);
      failure = node.header.offset + relative;
    }
    if (status != JINJA_EXPRESSION_PARSE_OK) break;
    status = jinja_template_store(work, source, node);
    if (status != JINJA_EXPRESSION_PARSE_OK) { failure = node.source.offset; break; }
    cursor = node.source.offset + node.source.length;
    /* Whitespace consumption affects the next lexical match, especially line
     * prefixes and delimiters beginning with spaces. Retain cursor as the
     * original tag end so the following TEXT source still covers those bytes. */
    JINJA_EXPRESSION_SPAN rest = jinja_template_content_left(source,
        (JINJA_EXPRESSION_SPAN){cursor, source.len - cursor}, node.right_control,
        delimiters->trim_blocks && node.kind != JINJA_TEMPLATE_OUTPUT &&
        !node.line_statement && !node.line_comment);
    jinja_template_lexer_set_offset(&lexer, rest.offset);
  }
  if (status == JINJA_EXPRESSION_PARSE_OK && work->depth != 0u) {
    failure = work->tree.nodes[work->frames[work->depth - 1u].opening].source.offset;
    status = JINJA_EXPRESSION_PARSE_INVALID;
  }
  if (status == JINJA_EXPRESSION_PARSE_OK) {
    jinja_template_project_content(original_source, delimiters, &work->tree);
    jinja_template_tree_destroy(result);
    *result = work->tree;
    work->tree = (JINJA_TEMPLATE_TREE){0};
  }
  else if (error_offset != NULL) *error_offset = failure;
  jinja_template_tree_destroy(&work->tree);
  jinja_parser_deallocate(allocator, work, sizeof(*work));
  return status;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_event(
    JINJA_TEMPLATE_BINDING_WORK *work, JINJA_EXPRESSION_SCOPE_EVENT_KIND kind,
    JINJA_EXPRESSION_SPAN name, size_t base) {
  if (work->count == JINJA_EXPRESSION_MAX_SCOPE_EVENTS) return JINJA_EXPRESSION_PARSE_CAPACITY;
  name.offset += base;
  work->events[work->count++] = (JINJA_EXPRESSION_SCOPE_EVENT){kind, name};
  return JINJA_EXPRESSION_PARSE_OK;
}

/* AST spans are relative to input, while every published event uses full source.
 * The parser validates spans before collection; base is a validated source offset. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_accesses(
    JINJA_TEMPLATE_BINDING_WORK *work, vstr input, const JINJA_EXPRESSION_TREE *tree,
    size_t base, JINJA_EXPRESSION_SCOPE_EVENT_KIND kind, size_t *failure) {
  size_t relative = 0u;
  JINJA_EXPRESSION_PARSE_STATUS status = kind == JINJA_EXPRESSION_SCOPE_READ ?
      jinja_expression_collect_reads(input, tree, &work->accesses, &relative) :
      jinja_expression_collect_targets(input, tree, &work->accesses, &relative);
  *failure = base + relative;
  size_t read = 0u, write = 0u;
  /* A tuple can bind ns before reading ns.attr as another target. Preserve that
   * order by merging the collector's sorted first-access spans, not by access kind. */
  while (status == JINJA_EXPRESSION_PARSE_OK &&
      (read < work->accesses.read_count || write < work->accesses.write_count)) {
    if (write == work->accesses.write_count || (read < work->accesses.read_count &&
        work->accesses.reads[read].offset < work->accesses.writes[write].offset))
      status = jinja_template_binding_event(work, JINJA_EXPRESSION_SCOPE_READ, work->accesses.reads[read++], base);
    else status = jinja_template_binding_event(work, kind, work->accesses.writes[write++], base);
  }
  return status;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_signature(
    JINJA_TEMPLATE_BINDING_WORK *work, vstr input, JINJA_TEMPLATE_NODE_KIND kind,
    size_t *relative) {
  if (kind == JINJA_TEMPLATE_MACRO)
    return jinja_expression_parse_macro_signature_allocated(input, &work->scratch.expressions.signature, relative, &work->allocator);
  JINJA_EXPRESSION_SPAN call;
  return jinja_expression_parse_call_header_allocated(input, &work->scratch.expressions.signature,
      &call, &work->scratch.expressions.expression, relative, &work->allocator);
}

/* WITH/FOR permit repeated targets, unlike a macro signature. Declare each distinct
 * incoming slot once; runtime assignment order is not represented by these facts. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_parameters(vstr source,
    vstr input, size_t base, const JINJA_EXPRESSION_TREE *targets,
    JINJA_TEMPLATE_BINDING_WORK *work, size_t *failure) {
  size_t relative = 0u;
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_collect_targets(input,
      targets, &work->accesses, &relative);
  *failure = base + relative;
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  for (size_t i = 0u; i < work->accesses.write_count; ++i) {
    JINJA_EXPRESSION_SPAN name = work->accesses.writes[i];
    name.offset += base;
    size_t slot = 0u;
    for (; slot < work->count; ++slot) {
      JINJA_EXPRESSION_SPAN prior = work->events[slot].name;
      if (prior.length == name.length &&
          memcmp(source.data + prior.offset, source.data + name.offset, name.length) == 0) break;
    }
    if (slot != work->count) continue;
    status = jinja_template_binding_event(work, JINJA_EXPRESSION_SCOPE_PARAMETER, name, 0u);
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  }
  return status;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_with(vstr source,
    const JINJA_TEMPLATE_NODE *node, JINJA_TEMPLATE_BINDING_WORK *work,
    JINJA_EXPRESSION_SCOPE_EVENT_KIND kind, size_t *failure) {
  vstr input = jinja_template_view(source, node->header), rhs;
  size_t base = node->header.offset, consumed = 0u, bindings = 0u;
  JINJA_EXPRESSION_TREE *expression = &work->scratch.expressions.expression;
  JINJA_EXPRESSION_TREE *targets = &work->scratch.expressions.targets;
  while (input.len != 0u) {
    *failure = base;
    if (bindings++ == JINJA_EXPRESSION_MAX_NODES) return JINJA_EXPRESSION_PARSE_CAPACITY;
    JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_with_binding_allocated(input, &consumed, &rhs, expression, targets, &work->allocator);
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    if (kind == JINJA_EXPRESSION_SCOPE_READ)
      status = jinja_template_binding_accesses(work, rhs, expression,
          base + (size_t)(rhs.data - input.data), kind, failure);
    else status = jinja_template_binding_parameters(source, input, base, targets, work, failure);
    if (status != JINJA_EXPRESSION_PARSE_OK || consumed == input.len) return status;
    if (consumed >= input.len) return JINJA_EXPRESSION_PARSE_INVALID;
    base += consumed + 1u;
    input.data += consumed + 1u;
    input.len -= consumed + 1u;
  }
  return JINJA_EXPRESSION_PARSE_OK;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_trans(vstr source,
    const JINJA_TEMPLATE_NODE *node, JINJA_TEMPLATE_BINDING_WORK *work, size_t *failure) {
  JINJA_TEMPLATE_NODE binding = *node;
  vstr header = jinja_template_view(source, node->header);
  size_t cursor = 0u;
  if (header.len != 0u && (header.data[0] == '\'' || header.data[0] == '"')) {
    const char quote = header.data[cursor++];
    while (cursor < header.len) {
      if (header.data[cursor] == '\\') {
        cursor += cursor + 1u < header.len ? 2u : 1u;
        continue;
      }
      if (header.data[cursor++] == quote) break;
    }
    if (cursor == header.len && header.data[cursor - 1u] != quote)
      return JINJA_EXPRESSION_PARSE_INVALID;
  }
  binding.header = jinja_template_trim(source, (JINJA_EXPRESSION_SPAN){
      node->header.offset + cursor, node->header.length - cursor});
  header = jinja_template_view(source, binding.header);
  for (size_t i = 0u; i < 2u; ++i) {
    static const char *const modifiers[] = {"trimmed", "notrimmed"};
    const size_t length = strlen(modifiers[i]);
    size_t next = length;
    salts_unicode_scalar scalar;
    if (header.len < length || memcmp(header.data, modifiers[i], length) != 0 ||
        (header.len != length && header.data[length] != ',' &&
         (salts_unicode_utf8_next(header, &next, &scalar) != SALTS_UNICODE_OK ||
          !jinja_template_space(scalar)))) continue;
    binding.header = jinja_template_trim(source, (JINJA_EXPRESSION_SPAN){
        binding.header.offset + length, binding.header.length - length});
    header = jinja_template_view(source, binding.header);
    if (header.len != 0u && header.data[0] == ',') {
      binding.header = jinja_template_trim(source, (JINJA_EXPRESSION_SPAN){
          binding.header.offset + 1u, binding.header.length - 1u});
      header = jinja_template_view(source, binding.header);
    }
    break;
  }
  if (header.len == 0u)
    return JINJA_EXPRESSION_PARSE_OK;
  return jinja_template_binding_with(source, &binding, work, JINJA_EXPRESSION_SCOPE_READ, failure);
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_header(vstr source,
    const JINJA_TEMPLATE_NODE *node, JINJA_TEMPLATE_BINDING_WORK *work,
    JINJA_EXPRESSION_SCOPE_EVENT_KIND store, size_t *failure) {
  vstr input = jinja_template_view(source, node->header), name, rhs;
  size_t base = node->header.offset, relative = 0u;
  JINJA_EXPRESSION_TREE *expression = &work->scratch.expressions.expression;
  JINJA_EXPRESSION_TREE *targets = &work->scratch.expressions.targets;
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_OK;
  *failure = node->source.offset;
  switch (node->kind) {
    case JINJA_TEMPLATE_TEXT: case JINJA_TEMPLATE_RAW: case JINJA_TEMPLATE_COMMENT:
    case JINJA_TEMPLATE_ELSE: case JINJA_TEMPLATE_END:
    case JINJA_TEMPLATE_BREAK: case JINJA_TEMPLATE_CONTINUE:
    case JINJA_TEMPLATE_BLOCK: case JINJA_TEMPLATE_AUTOESCAPE:
      return status;
    case JINJA_TEMPLATE_DEBUG:
      return JINJA_EXPRESSION_PARSE_UNSUPPORTED;
    case JINJA_TEMPLATE_TRANS:
      return jinja_template_binding_trans(source, node, work, failure);
    case JINJA_TEMPLATE_PLURALIZE:
      return status;
    case JINJA_TEMPLATE_MACRO:
      status = jinja_template_binding_signature(work, input, node->kind, &relative);
      if (status == JINJA_EXPRESSION_PARSE_OK)
        status = jinja_template_binding_event(work, store, work->scratch.expressions.signature.name, base);
      break;
    case JINJA_TEMPLATE_SET: case JINJA_TEMPLATE_CAPTURE: {
      int capture = 0;
      status = jinja_expression_parse_assignment_allocated(input, &name, &rhs, expression, targets, &capture, &relative, &work->allocator);
      if (status != JINJA_EXPRESSION_PARSE_OK) break;
      if (!capture) {
        status = jinja_template_binding_accesses(work, rhs, expression,
            base + (size_t)(rhs.data - input.data), JINJA_EXPRESSION_SCOPE_READ, failure);
        if (status != JINJA_EXPRESSION_PARSE_OK) return status;
      }
      return jinja_template_binding_accesses(work, input, targets, base, store, failure);
    }
    case JINJA_TEMPLATE_FOR:
      status = jinja_expression_parse_for_header_allocated(input, &work->scratch.loop, &relative, &work->allocator);
      if (status == JINJA_EXPRESSION_PARSE_OK) {
        JINJA_EXPRESSION_SPAN iterable = work->scratch.loop.iterable;
        return jinja_template_binding_accesses(work, jinja_template_view(input, iterable),
            &work->scratch.loop.iterable_tree, base + iterable.offset, JINJA_EXPRESSION_SCOPE_READ, failure);
      }
      break;
    case JINJA_TEMPLATE_WITH:
      return jinja_template_binding_with(source, node, work, JINJA_EXPRESSION_SCOPE_READ, failure);
    case JINJA_TEMPLATE_CALL: {
      JINJA_EXPRESSION_SPAN call;
      status = jinja_expression_parse_call_header_allocated(input, &work->scratch.expressions.signature,
          &call, expression, &relative, &work->allocator);
      if (status == JINJA_EXPRESSION_PARSE_OK)
        return jinja_template_binding_accesses(work, jinja_template_view(input, call), expression,
            base + call.offset, JINJA_EXPRESSION_SCOPE_READ, failure);
      break;
    }
    case JINJA_TEMPLATE_EXTENDS: case JINJA_TEMPLATE_INCLUDE:
    case JINJA_TEMPLATE_IMPORT: case JINJA_TEMPLATE_FROM: {
      JINJA_TEMPLATE_REFERENCE_KIND kind = node->kind == JINJA_TEMPLATE_EXTENDS ? JINJA_TEMPLATE_REFERENCE_EXTENDS :
          node->kind == JINJA_TEMPLATE_INCLUDE ? JINJA_TEMPLATE_REFERENCE_INCLUDE :
          node->kind == JINJA_TEMPLATE_IMPORT ? JINJA_TEMPLATE_REFERENCE_IMPORT : JINJA_TEMPLATE_REFERENCE_FROM;
      JINJA_TEMPLATE_REFERENCE *reference = &work->scratch.reference;
      status = jinja_expression_parse_template_reference_allocated(input, kind, reference, &relative, &work->allocator);
      if (status != JINJA_EXPRESSION_PARSE_OK) break;
      status = jinja_template_binding_accesses(work, jinja_template_view(input, reference->expression),
          &reference->tree, base + reference->expression.offset, JINJA_EXPRESSION_SCOPE_READ, failure);
      for (size_t i = 0u; status == JINJA_EXPRESSION_PARSE_OK && i < reference->name_count; ++i)
        status = jinja_template_binding_event(work, store, reference->names[i].alias, base);
      return status;
    }
    case JINJA_TEMPLATE_FILTER:
      status = jinja_expression_parse_filter_block_allocated(input, expression, &relative, &work->allocator);
      if (status == JINJA_EXPRESSION_PARSE_OK)
        return jinja_template_binding_accesses(work, input, expression, base, JINJA_EXPRESSION_SCOPE_READ, failure);
      break;
    case JINJA_TEMPLATE_PRINT:
      if (input.len == 0u) return status;
      /* Nonempty print uses the expression-list AST, just like output. */
      /* fall through */
    case JINJA_TEMPLATE_OUTPUT: case JINJA_TEMPLATE_DO: case JINJA_TEMPLATE_EXTENSION_TAG:
    case JINJA_TEMPLATE_IF: case JINJA_TEMPLATE_ELIF:
      status = jinja_expression_parse_tree_allocated(input, expression, &relative, &work->allocator);
      if (status == JINJA_EXPRESSION_PARSE_OK)
        return jinja_template_binding_accesses(work, input, expression, base, JINJA_EXPRESSION_SCOPE_READ, failure);
      break;
    default: return JINJA_EXPRESSION_PARSE_INVALID;
  }
  *failure = base + relative;
  return status;
}

static int jinja_template_binding_span(vstr source, JINJA_EXPRESSION_SPAN span) {
  return span.offset <= source.len && span.length <= source.len - span.offset;
}

static int jinja_template_binding_frame_kind(JINJA_TEMPLATE_NODE_KIND kind) {
  return kind == JINJA_TEMPLATE_MACRO || kind == JINJA_TEMPLATE_CALL ||
      kind == JINJA_TEMPLATE_WITH || kind == JINJA_TEMPLATE_FILTER ||
      kind == JINJA_TEMPLATE_CAPTURE || kind == JINJA_TEMPLATE_AUTOESCAPE ||
      kind == JINJA_TEMPLATE_FOR || kind == JINJA_TEMPLATE_BLOCK;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_for(vstr source,
    const JINJA_TEMPLATE_NODE *node, JINJA_TEMPLATE_FOR_BRANCH branch,
    JINJA_TEMPLATE_BINDING_WORK *work, size_t *failure) {
  if (branch == JINJA_TEMPLATE_FOR_ELSE) return JINJA_EXPRESSION_PARSE_OK;
  vstr input = jinja_template_view(source, node->header);
  JINJA_TEMPLATE_FOR_HEADER *loop = &work->scratch.loop;
  size_t relative = 0u;
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_for_header_allocated(input, loop, &relative, &work->allocator);
  *failure = node->header.offset + relative;
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  status = jinja_template_binding_parameters(source, jinja_template_view(input, loop->target),
      node->header.offset + loop->target.offset, &loop->targets, work, failure);
  if (status != JINJA_EXPRESSION_PARSE_OK || branch != JINJA_TEMPLATE_FOR_TEST || !loop->has_test)
    return status;
  return jinja_template_binding_accesses(work, jinja_template_view(input, loop->test),
      &loop->test_tree, node->header.offset + loop->test.offset, JINJA_EXPRESSION_SCOPE_READ, failure);
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_macro_entry(
    const JINJA_EXPRESSION_MACRO_SIGNATURE *signature, size_t base,
    const JINJA_TEMPLATE_MACRO_BINDINGS *bindings, JINJA_TEMPLATE_BINDING_WORK *work) {
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_OK;
  for (size_t i = 0u; status == JINJA_EXPRESSION_PARSE_OK && i < signature->parameter_count; ++i)
    status = jinja_template_binding_event(work, JINJA_EXPRESSION_SCOPE_PARAMETER, signature->parameters[i].name, base);
  if (bindings != NULL) {
    for (size_t i = 0u; status == JINJA_EXPRESSION_PARSE_OK && i < JINJA_TEMPLATE_MACRO_SPECIAL_COUNT; ++i)
      if (bindings->implicit[i].length != 0u)
        status = jinja_template_binding_event(work, JINJA_EXPRESSION_SCOPE_PARAMETER, bindings->implicit[i], 0u);
  }
  for (size_t i = 0u; status == JINJA_EXPRESSION_PARSE_OK && i < signature->default_reference_count; ++i)
    status = jinja_template_binding_event(work, JINJA_EXPRESSION_SCOPE_READ, signature->default_references[i], base);
  return status;
}

static JINJA_EXPRESSION_PARSE_STATUS jinja_template_binding_entry(vstr source,
    const JINJA_TEMPLATE_NODE *node, JINJA_TEMPLATE_BINDING_WORK *work, size_t *failure) {
  vstr input = jinja_template_view(source, node->header);
  size_t relative = 0u;
  if (node->kind == JINJA_TEMPLATE_WITH)
    return jinja_template_binding_with(source, node, work, JINJA_EXPRESSION_SCOPE_PARAMETER, failure);
  /* Filter arguments follow the body. AssignBlock's RootVisitor visits body only. */
  if (node->kind == JINJA_TEMPLATE_FILTER || node->kind == JINJA_TEMPLATE_CAPTURE ||
      node->kind == JINJA_TEMPLATE_BLOCK)
    return JINJA_EXPRESSION_PARSE_OK;
  if (node->kind == JINJA_TEMPLATE_AUTOESCAPE) {
    JINJA_EXPRESSION_TREE *expression = &work->scratch.expressions.expression;
    JINJA_EXPRESSION_PARSE_STATUS status = jinja_expression_parse_tree_allocated(input, expression, &relative, &work->allocator);
    *failure = node->header.offset + relative;
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    return jinja_template_binding_accesses(work, input, expression, node->header.offset,
        JINJA_EXPRESSION_SCOPE_READ, failure);
  }
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_template_binding_signature(work, input, node->kind, &relative);
  *failure = node->header.offset + relative;
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  return jinja_template_binding_macro_entry(&work->scratch.expressions.signature, node->header.offset, NULL, work);
}

/* One forward traversal; nested frames are skipped by parser-provided match links.
 * Cost is O(nodes + reparsed headers + analyze_scope); all storage is bounded and
 * call-local. No runtime binding or partially analyzed scope is ever published. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_selected_frame(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, JINJA_TEMPLATE_FOR_BRANCH branch,
    const JINJA_EXPRESSION_SCOPE *parent,
    JINJA_EXPRESSION_SCOPE *result, size_t *error_offset, const stl_allocator *allocator) {
  size_t begin = 0u, end, failure = 0u, depth = 0u;
  size_t branches[JINJA_TEMPLATE_MAX_DEPTH];
  JINJA_EXPRESSION_SPAN self = {0}, super = {0};
  JINJA_TEMPLATE_MACRO_DESCRIPTOR macro = {0};
  if (error_offset != NULL) *error_offset = 0u;
  if (!vstr_is_valid(source) || tree == NULL || result == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  if (source.len > JINJA_TEMPLATE_MAX_BYTES || tree->count > JINJA_TEMPLATE_MAX_NODES)
    return JINJA_EXPRESSION_PARSE_CAPACITY;
  end = tree->count;
  if (opener != SIZE_MAX) {
    if (opener >= tree->count) return JINJA_EXPRESSION_PARSE_INVALID;
    if (!jinja_template_binding_frame_kind(tree->nodes[opener].kind))
      return JINJA_EXPRESSION_PARSE_UNSUPPORTED;
    /* Scoped blocks receive a runtime context, not an enclosing lexical frame. */
    if (tree->nodes[opener].kind == JINJA_TEMPLATE_BLOCK && parent != NULL)
      return JINJA_EXPRESSION_PARSE_INVALID;
    begin = opener + 1u;
    end = tree->nodes[opener].match;
    if (end < begin || end >= tree->count) return JINJA_EXPRESSION_PARSE_INVALID;
  }
  for (size_t i = 0u; i < tree->count; ++i) {
    const JINJA_TEMPLATE_NODE *node = &tree->nodes[i];
    if (!jinja_template_binding_span(source, node->header) || !jinja_template_binding_span(source, node->source))
      return JINJA_EXPRESSION_PARSE_INVALID;
    if (jinja_template_ending(node->kind) != NULL &&
        (node->match <= i || node->match >= tree->count ||
         tree->nodes[node->match].kind != JINJA_TEMPLATE_END || tree->nodes[node->match].match != i))
      return JINJA_EXPRESSION_PARSE_INVALID;
  }
  if (opener != SIZE_MAX && tree->nodes[opener].kind == JINJA_TEMPLATE_FOR) {
    size_t alternate = tree->nodes[opener].branch;
    if (alternate != SIZE_MAX && (alternate < begin || alternate >= end ||
        tree->nodes[alternate].kind != JINJA_TEMPLATE_ELSE || tree->nodes[alternate].parent != opener))
      return JINJA_EXPRESSION_PARSE_INVALID;
    if (branch == JINJA_TEMPLATE_FOR_TEST) begin = end;
    else if (branch == JINJA_TEMPLATE_FOR_ELSE) begin = alternate == SIZE_MAX ? end : alternate + 1u;
    else if (alternate != SIZE_MAX) end = alternate;
  }
  /* A block is independent; root/block self and block super must be local
   * parameters before ordinary loads or nested closures resolve their names. */
  if (opener == SIZE_MAX || tree->nodes[opener].kind == JINJA_TEMPLATE_BLOCK) {
    JINJA_EXPRESSION_PARSE_STATUS discovery = jinja_template_find_undeclared_allocated(source, tree,
        opener, vstr_from_cstr("self"), &self, error_offset, allocator);
    if (discovery != JINJA_EXPRESSION_PARSE_OK) return discovery;
    if (opener != SIZE_MAX) {
      discovery = jinja_template_find_undeclared_allocated(source, tree, opener,
          vstr_from_cstr("super"), &super, error_offset, allocator);
      if (discovery != JINJA_EXPRESSION_PARSE_OK) return discovery;
    }
  }
  if (opener != SIZE_MAX && (tree->nodes[opener].kind == JINJA_TEMPLATE_MACRO ||
      tree->nodes[opener].kind == JINJA_TEMPLATE_CALL)) {
    JINJA_EXPRESSION_PARSE_STATUS discovery = jinja_template_describe_macro_allocated(source,
        tree, opener, &macro, error_offset, allocator);
    if (discovery != JINJA_EXPRESSION_PARSE_OK) return discovery;
  }
  void *work_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_zero(allocator, sizeof(JINJA_TEMPLATE_BINDING_WORK), &work_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  JINJA_TEMPLATE_BINDING_WORK *work = (JINJA_TEMPLATE_BINDING_WORK *)work_storage;
  work->allocator = jinja_parser_allocator_copy(allocator);
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_OK;
  if (self.length != 0u) status = jinja_template_binding_event(work, JINJA_EXPRESSION_SCOPE_PARAMETER, self, 0u);
  if (status == JINJA_EXPRESSION_PARSE_OK && super.length != 0u)
    status = jinja_template_binding_event(work, JINJA_EXPRESSION_SCOPE_PARAMETER, super, 0u);
  if (status == JINJA_EXPRESSION_PARSE_OK && opener != SIZE_MAX) {
    if (tree->nodes[opener].kind == JINJA_TEMPLATE_FOR)
      status = jinja_template_binding_for(source, &tree->nodes[opener], branch, work, &failure);
    else if (tree->nodes[opener].kind == JINJA_TEMPLATE_MACRO || tree->nodes[opener].kind == JINJA_TEMPLATE_CALL)
      status = jinja_template_binding_macro_entry(&macro.signature, 0u, &macro.bindings, work);
    else status = jinja_template_binding_entry(source, &tree->nodes[opener], work, &failure);
  }
  for (size_t i = begin; status == JINJA_EXPRESSION_PARSE_OK && i < end; ++i) {
    const JINJA_TEMPLATE_NODE *node = &tree->nodes[i];
    while (depth != 0u && i >= branches[depth - 1u]) --depth;
    status = jinja_template_binding_header(source, node, work,
        depth == 0u ? JINJA_EXPRESSION_SCOPE_STORE : JINJA_EXPRESSION_SCOPE_BRANCH_STORE, &failure);
    if (status != JINJA_EXPRESSION_PARSE_OK) break;
    if (node->kind == JINJA_TEMPLATE_IF) {
      if (depth == JINJA_TEMPLATE_MAX_DEPTH) { status = JINJA_EXPRESSION_PARSE_CAPACITY; break; }
      branches[depth++] = node->match;
    } else if (jinja_template_ending(node->kind) != NULL) {
      if (node->match >= end) { status = JINJA_EXPRESSION_PARSE_INVALID; break; }
      i = node->match;
    }
  }
  if (status == JINJA_EXPRESSION_PARSE_OK && opener != SIZE_MAX && tree->nodes[opener].kind == JINJA_TEMPLATE_FILTER)
    status = jinja_template_binding_header(source, &tree->nodes[opener], work, JINJA_EXPRESSION_SCOPE_STORE, &failure);
  if (status == JINJA_EXPRESSION_PARSE_OK)
    status = jinja_expression_analyze_scope(source, parent, work->events, work->count, result, &failure);
  if (error_offset != NULL) *error_offset = status == JINJA_EXPRESSION_PARSE_OK ? 0u : failure;
  jinja_parser_deallocate(allocator, work, sizeof(*work));
  return status;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_frame(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, const JINJA_EXPRESSION_SCOPE *parent,
    JINJA_EXPRESSION_SCOPE *result, size_t *error_offset) {
  return jinja_template_analyze_frame_allocated(source, tree, opener, parent, result, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_frame_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, const JINJA_EXPRESSION_SCOPE *parent,
    JINJA_EXPRESSION_SCOPE *result, size_t *error_offset, const stl_allocator *allocator) {
  return jinja_template_analyze_selected_frame(source, tree, opener, JINJA_TEMPLATE_FOR_BODY,
      parent, result, error_offset, allocator);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_for_frame(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, JINJA_TEMPLATE_FOR_BRANCH branch,
    const JINJA_EXPRESSION_SCOPE *parent, JINJA_EXPRESSION_SCOPE *result, size_t *error_offset) {
  return jinja_template_analyze_for_frame_allocated(source, tree, opener, branch, parent, result, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_for_frame_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, JINJA_TEMPLATE_FOR_BRANCH branch,
    const JINJA_EXPRESSION_SCOPE *parent, JINJA_EXPRESSION_SCOPE *result, size_t *error_offset, const stl_allocator *allocator) {
  if (error_offset != NULL) *error_offset = 0u;
  if (tree == NULL || branch < JINJA_TEMPLATE_FOR_BODY || branch > JINJA_TEMPLATE_FOR_ELSE)
    return JINJA_EXPRESSION_PARSE_INVALID;
  if (tree->count > JINJA_TEMPLATE_MAX_NODES) return JINJA_EXPRESSION_PARSE_CAPACITY;
  if (opener >= tree->count || tree->nodes[opener].kind != JINJA_TEMPLATE_FOR)
    return JINJA_EXPRESSION_PARSE_INVALID;
  return jinja_template_analyze_selected_frame(source, tree, opener, branch, parent, result, error_offset, allocator);
}

/* Discovery follows generic AST visitation, not evaluation or scope initialization.
 * Reuse header parsers/collectors but keep the differing traversal policy explicit. */
static JINJA_EXPRESSION_PARSE_STATUS jinja_template_discovery_header(vstr source,
    const JINJA_TEMPLATE_NODE *node, int closing, JINJA_TEMPLATE_BINDING_WORK *work,
    size_t *failure) {
  vstr input = jinja_template_view(source, node->header), name, rhs;
  size_t relative = 0u, base = node->header.offset;
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_OK;
  *failure = base;
  if (node->kind == JINJA_TEMPLATE_FOR) {
    JINJA_TEMPLATE_FOR_HEADER *loop = &work->scratch.loop;
    status = jinja_expression_parse_for_header_allocated(input, loop, &relative, &work->allocator);
    *failure = base + relative;
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    if (closing) {
      if (!loop->has_test) return status;
      return jinja_template_binding_accesses(work, jinja_template_view(input, loop->test),
          &loop->test_tree, base + loop->test.offset, JINJA_EXPRESSION_SCOPE_READ, failure);
    }
    status = jinja_template_binding_parameters(source, jinja_template_view(input, loop->target),
        base + loop->target.offset, &loop->targets, work, failure);
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    return jinja_template_binding_accesses(work, jinja_template_view(input, loop->iterable),
        &loop->iterable_tree, base + loop->iterable.offset, JINJA_EXPRESSION_SCOPE_READ, failure);
  }
  if (node->kind == JINJA_TEMPLATE_FILTER)
    return closing ? jinja_template_binding_header(source, node, work,
        JINJA_EXPRESSION_SCOPE_STORE, failure) : status;
  if (closing) return status;
  if (node->kind == JINJA_TEMPLATE_SET || node->kind == JINJA_TEMPLATE_CAPTURE) {
    int capture = 0;
    JINJA_EXPRESSION_TREE *expression = &work->scratch.expressions.expression;
    JINJA_EXPRESSION_TREE *targets = &work->scratch.expressions.targets;
    status = jinja_expression_parse_assignment_allocated(input, &name, &rhs, expression, targets, &capture, &relative, &work->allocator);
    *failure = base + relative;
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    /* NSRef owners are reads for binding, but are not Name nodes for discovery. */
    status = jinja_template_binding_parameters(source, input, base, targets, work, failure);
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    return jinja_template_binding_accesses(work, capture ? input : rhs, expression,
        capture ? base : base + (size_t)(rhs.data - input.data), JINJA_EXPRESSION_SCOPE_READ, failure);
  }
  if (node->kind == JINJA_TEMPLATE_WITH) {
    status = jinja_template_binding_with(source, node, work, JINJA_EXPRESSION_SCOPE_PARAMETER, failure);
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    return jinja_template_binding_with(source, node, work, JINJA_EXPRESSION_SCOPE_READ, failure);
  }
  if (node->kind == JINJA_TEMPLATE_CALL) {
    status = jinja_template_binding_header(source, node, work, JINJA_EXPRESSION_SCOPE_STORE, failure);
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  }
  if (node->kind == JINJA_TEMPLATE_MACRO || node->kind == JINJA_TEMPLATE_CALL ||
      node->kind == JINJA_TEMPLATE_AUTOESCAPE)
    return jinja_template_binding_entry(source, node, work, failure);
  status = jinja_template_binding_header(source, node, work, JINJA_EXPRESSION_SCOPE_STORE, failure);
  if (node->kind == JINJA_TEMPLATE_IMPORT || node->kind == JINJA_TEMPLATE_FROM) {
    /* Import aliases are strings in the AST, not Name stores. */
    size_t count = 0u;
    for (size_t i = 0u; i < work->count; ++i)
      if (work->events[i].kind == JINJA_EXPRESSION_SCOPE_READ)
        work->events[count++] = work->events[i];
    work->count = count;
  }
  return status;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_find_undeclared(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, vstr name,
    JINJA_EXPRESSION_SPAN *reference, size_t *error_offset) {
  return jinja_template_find_undeclared_allocated(source, tree, opener, name, reference, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_find_undeclared_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener, vstr name,
    JINJA_EXPRESSION_SPAN *reference, size_t *error_offset, const stl_allocator *allocator) {
  size_t begin = 0u, end, failure = 0u;
  if (error_offset != NULL) *error_offset = 0u;
  if (!vstr_is_valid(source) || !vstr_is_valid(name) || name.len == 0u ||
      tree == NULL || reference == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  if (source.len > JINJA_TEMPLATE_MAX_BYTES || tree->count > JINJA_TEMPLATE_MAX_NODES)
    return JINJA_EXPRESSION_PARSE_CAPACITY;
  end = tree->count;
  if (opener != SIZE_MAX) {
    if (opener >= end) return JINJA_EXPRESSION_PARSE_INVALID;
    if (!jinja_template_binding_frame_kind(tree->nodes[opener].kind)) return JINJA_EXPRESSION_PARSE_UNSUPPORTED;
    begin = opener + 1u;
    end = tree->nodes[opener].match;
    if (end < begin || end >= tree->count) return JINJA_EXPRESSION_PARSE_INVALID;
    if (tree->nodes[opener].kind == JINJA_TEMPLATE_FOR && tree->nodes[opener].branch != SIZE_MAX) {
      size_t alternate = tree->nodes[opener].branch;
      if (alternate < begin || alternate >= end || tree->nodes[alternate].kind != JINJA_TEMPLATE_ELSE ||
          tree->nodes[alternate].parent != opener) return JINJA_EXPRESSION_PARSE_INVALID;
      end = alternate;
    }
  }
  for (size_t i = 0u; i < tree->count; ++i) {
    const JINJA_TEMPLATE_NODE *node = &tree->nodes[i];
    if (!jinja_template_binding_span(source, node->header) || !jinja_template_binding_span(source, node->source))
      return JINJA_EXPRESSION_PARSE_INVALID;
    if (jinja_template_ending(node->kind) != NULL &&
        (node->match <= i || node->match >= tree->count ||
         tree->nodes[node->match].kind != JINJA_TEMPLATE_END || tree->nodes[node->match].match != i))
      return JINJA_EXPRESSION_PARSE_INVALID;
    if (node->kind == JINJA_TEMPLATE_END && (node->match >= i ||
        jinja_template_ending(tree->nodes[node->match].kind) == NULL || tree->nodes[node->match].match != i))
      return JINJA_EXPRESSION_PARSE_INVALID;
  }
  void *work_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_zero(allocator, sizeof(JINJA_TEMPLATE_BINDING_WORK), &work_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  JINJA_TEMPLATE_BINDING_WORK *work = (JINJA_TEMPLATE_BINDING_WORK *)work_storage;
  work->allocator = jinja_parser_allocator_copy(allocator);
  JINJA_EXPRESSION_PARSE_STATUS status = JINJA_EXPRESSION_PARSE_OK;
  JINJA_EXPRESSION_SPAN found = {0};
  int decided = 0;
  /* Linear source traversal with close-time header visits reproduces structural
   * order without recursion. Cost O(nodes + header parsing), fixed scratch space. */
  for (size_t i = begin; status == JINJA_EXPRESSION_PARSE_OK && i < end; ++i) {
    const JINJA_TEMPLATE_NODE *node = &tree->nodes[i];
    if (node->kind == JINJA_TEMPLATE_BLOCK) {
      if (node->match >= end) { status = JINJA_EXPRESSION_PARSE_INVALID; break; }
      i = node->match;
      continue;
    }
    int closing = node->kind == JINJA_TEMPLATE_END;
    if (closing) node = &tree->nodes[node->match];
    work->count = 0u;
    status = jinja_template_discovery_header(source, node, closing, work, &failure);
    for (size_t j = 0u; !decided && status == JINJA_EXPRESSION_PARSE_OK && j < work->count; ++j) {
      JINJA_EXPRESSION_SCOPE_EVENT event = work->events[j];
      if (event.name.length != name.len || memcmp(source.data + event.name.offset, name.data, name.len) != 0) continue;
      decided = 1;
      if (event.kind == JINJA_EXPRESSION_SCOPE_READ) found = event.name;
    }
  }
  if (status == JINJA_EXPRESSION_PARSE_OK) *reference = found;
  else if (error_offset != NULL) *error_offset = failure;
  jinja_parser_deallocate(allocator, work, sizeof(*work));
  return status;
}

/* Header parsers publish local spans; lowering consumes only full-template spans. */
static void jinja_template_rebase_span(JINJA_EXPRESSION_SPAN *span, size_t base) {
  if (span->length != 0u) span->offset += base;
  else span->offset = 0u;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_describe_macro(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener,
    JINJA_TEMPLATE_MACRO_DESCRIPTOR *result, size_t *error_offset) {
  return jinja_template_describe_macro_allocated(source, tree, opener, result, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_describe_macro_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener,
    JINJA_TEMPLATE_MACRO_DESCRIPTOR *result, size_t *error_offset, const stl_allocator *allocator) {
  JINJA_TEMPLATE_MACRO_DESCRIPTOR parsed = {0};
  if (error_offset != NULL) *error_offset = 0u;
  if (result == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  /* Capability analysis validates source/tree spans before any header view is formed. */
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_template_analyze_macro_bindings_allocated(
      source, tree, opener, &parsed.bindings, error_offset, allocator);
  if (status != JINJA_EXPRESSION_PARSE_OK) return status;
  const JINJA_TEMPLATE_NODE *node = &tree->nodes[opener];
  vstr input = jinja_template_view(source, node->header);
  size_t relative = 0u;
  if (node->kind == JINJA_TEMPLATE_MACRO) {
    status = jinja_expression_parse_macro_signature_allocated(input, &parsed.signature, &relative, allocator);
  } else {
    void *expression_storage = NULL;
    status = jinja_parser_allocate(allocator, sizeof(JINJA_EXPRESSION_TREE), &expression_storage);
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    JINJA_EXPRESSION_TREE *expression = (JINJA_EXPRESSION_TREE *)expression_storage;
    status = jinja_expression_parse_call_header_allocated(input, &parsed.signature, &parsed.call, expression, &relative, allocator);
    jinja_parser_deallocate(allocator, expression, sizeof(*expression));
  }
  if (status != JINJA_EXPRESSION_PARSE_OK) {
    if (error_offset != NULL) *error_offset = node->header.offset + relative;
    return status;
  }
  jinja_template_rebase_span(&parsed.signature.name, node->header.offset);
  jinja_template_rebase_span(&parsed.call, node->header.offset);
  for (size_t i = 0u; i < parsed.signature.parameter_count; ++i) {
    jinja_template_rebase_span(&parsed.signature.parameters[i].name, node->header.offset);
    jinja_template_rebase_span(&parsed.signature.parameters[i].default_expression, node->header.offset);
  }
  for (size_t i = 0u; i < parsed.signature.default_reference_count; ++i)
    jinja_template_rebase_span(&parsed.signature.default_references[i], node->header.offset);
  parsed.body_begin = opener + 1u;
  parsed.body_end = node->match;
  *result = parsed;
  return JINJA_EXPRESSION_PARSE_OK;
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_macro_bindings(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener,
    JINJA_TEMPLATE_MACRO_BINDINGS *result, size_t *error_offset) {
  return jinja_template_analyze_macro_bindings_allocated(source, tree, opener, result, error_offset, NULL);
}

JINJA_EXPRESSION_PARSE_STATUS jinja_template_analyze_macro_bindings_allocated(vstr source,
    const JINJA_TEMPLATE_TREE *tree, size_t opener,
    JINJA_TEMPLATE_MACRO_BINDINGS *result, size_t *error_offset, const stl_allocator *allocator) {
  static const char *const names[JINJA_TEMPLATE_MACRO_SPECIAL_COUNT] = {"caller", "kwargs", "varargs"};
  if (error_offset != NULL) *error_offset = 0u;
  if (!vstr_is_valid(source) || tree == NULL || result == NULL) return JINJA_EXPRESSION_PARSE_INVALID;
  if (source.len > JINJA_TEMPLATE_MAX_BYTES || tree->count > JINJA_TEMPLATE_MAX_NODES)
    return JINJA_EXPRESSION_PARSE_CAPACITY;
  if (opener >= tree->count || (tree->nodes[opener].kind != JINJA_TEMPLATE_MACRO &&
      tree->nodes[opener].kind != JINJA_TEMPLATE_CALL)) return JINJA_EXPRESSION_PARSE_INVALID;
  JINJA_TEMPLATE_MACRO_BINDINGS bindings = {0};
  for (size_t i = 0u; i < JINJA_TEMPLATE_MACRO_SPECIAL_COUNT; ++i) {
    JINJA_EXPRESSION_PARSE_STATUS status = jinja_template_find_undeclared_allocated(source, tree,
        opener, vstr_from_cstr(names[i]), &bindings.implicit[i], error_offset, allocator);
    if (status != JINJA_EXPRESSION_PARSE_OK) return status;
    if (bindings.implicit[i].length != 0u) bindings.accesses |= 1u << i;
  }
  /* Discovery validates tree spans before signature parsing; its workspaces are
   * already released, so signature analysis never doubles the peak scratch budget. */
  const JINJA_TEMPLATE_NODE *node = &tree->nodes[opener];
  vstr input = jinja_template_view(source, node->header);
  size_t relative = 0u;
  void *work_storage = NULL;
  const JINJA_EXPRESSION_PARSE_STATUS allocation_status =
      jinja_parser_zero(allocator, sizeof(JINJA_TEMPLATE_BINDING_WORK), &work_storage);
  if (allocation_status != JINJA_EXPRESSION_PARSE_OK) return allocation_status;
  JINJA_TEMPLATE_BINDING_WORK *work = (JINJA_TEMPLATE_BINDING_WORK *)work_storage;
  work->allocator = jinja_parser_allocator_copy(allocator);
  JINJA_EXPRESSION_PARSE_STATUS status = jinja_template_binding_signature(work, input, node->kind, &relative);
  const JINJA_EXPRESSION_MACRO_SIGNATURE *signature = &work->scratch.expressions.signature;
  for (size_t i = 0u; status == JINJA_EXPRESSION_PARSE_OK && i < signature->parameter_count; ++i) {
    JINJA_EXPRESSION_PARAMETER parameter = signature->parameters[i];
    for (size_t j = 0u; j < JINJA_TEMPLATE_MACRO_SPECIAL_COUNT; ++j) {
      if (parameter.name.length != strlen(names[j]) ||
          memcmp(input.data + parameter.name.offset, names[j], parameter.name.length) != 0) continue;
      if (j == JINJA_TEMPLATE_MACRO_CALLER && (bindings.accesses & (1u << j)) != 0u &&
          parameter.default_expression.length == 0u) {
        status = JINJA_EXPRESSION_PARSE_INVALID;
        relative = parameter.name.offset;
        break;
      }
      bindings.implicit[j] = (JINJA_EXPRESSION_SPAN){0};
      if (j != JINJA_TEMPLATE_MACRO_CALLER) bindings.accesses &= ~(1u << j);
    }
  }
  if (status == JINJA_EXPRESSION_PARSE_OK) *result = bindings;
  else if (error_offset != NULL) *error_offset = node->header.offset + relative;
  jinja_parser_deallocate(allocator, work, sizeof(*work));
  return status;
}
