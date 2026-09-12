// re2c --lang c
#include "jinja_expression_grammar_gen.h"
#include "jinja_expression_parser.h"
#include "salts_unicode.h"

/*!include:re2c "unicode_properties.re" */
/*!include:re2c "unicode_categories.re" */

static const char *jinja_expression_decimal_end(const char *cursor, const char *limit);

/* Called only on complete quoted tokens recognized by re2c. Escaped slashes
 * consume pairs, so literal \\N text is never treated as a character name. */
static int jinja_expression_valid_named_escapes(const char *cursor, const char *end) {
  while (cursor < end) {
    if (*cursor++ != '\\') continue;
    if (cursor == end) return 0;
    if (*cursor++ != 'N') continue;
    if (cursor == end || *cursor++ != '{') return 0;
    const char *name = cursor;
    uint32_t scalar;
    while (cursor < end && *cursor != '}') ++cursor;
    if (cursor == end || salts_unicode_name_lookup(
        vstr_from_buf(name, (size_t)(cursor - name)), &scalar) != SALTS_UNICODE_OK) return 0;
    ++cursor;
  }
  return 1;
}

static int jinja_expression_ascii_number(const char *begin, const char *end) {
  for (; begin != end; ++begin)
    if ((unsigned char)*begin >= 0x80u) return 0;
  return 1;
}

void jinja_expression_lexer_init(JINJA_EXPRESSION_LEXER *lexer, vstr expression) {
  if (lexer == NULL) return;
  lexer->input = expression.data;
  lexer->cursor = expression.data;
  lexer->limit = expression.data + expression.len;
  lexer->expects_operand = 1;
  lexer->expects_membership_in = 0;
  lexer->test_state = JINJA_EXPRESSION_TEST_LEX_NONE;
  lexer->filter_state = JINJA_EXPRESSION_TEST_LEX_NONE;
}

static int jinja_expression_token_is_name(int kind) {
  switch (kind) {
  case JINJA_EXPRESSION_TOKEN_IDENTIFIER:
  case JINJA_EXPRESSION_TOKEN_NOT:
  case JINJA_EXPRESSION_TOKEN_AND:
  case JINJA_EXPRESSION_TOKEN_OR:
  case JINJA_EXPRESSION_TOKEN_IN:
  case JINJA_EXPRESSION_TOKEN_IF:
  case JINJA_EXPRESSION_TOKEN_ELSE:
  case JINJA_EXPRESSION_TOKEN_IS:
  case JINJA_EXPRESSION_TOKEN_TRUE:
  case JINJA_EXPRESSION_TOKEN_FALSE:
  case JINJA_EXPRESSION_TOKEN_NONE:
    return 1;
  default:
    return 0;
  }
}

int jinja_expression_lexer_next(JINJA_EXPRESSION_LEXER *lexer, int *kind,
                                JINJA_EXPRESSION_TOKEN *token) {
  const char *YYCURSOR;
  const char *YYMARKER;
  const char *YYLIMIT;
  const char *token_start;
  int not_name = 0;

  if (lexer == NULL || kind == NULL || token == NULL) return JINJA_EXPRESSION_LEX_UNSUPPORTED;
  YYCURSOR = lexer->cursor;
  YYLIMIT = lexer->limit;

scan:
  token_start = YYCURSOR;
  /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:encoding:utf8 = 1;
    re2c:encoding-policy = fail;
    re2c:yyfill:enable = 0;
    re2c:eof = 0;
    re2c:api = custom;
    re2c:api:style = free-form;
    re2c:define:YYLESSTHAN = "YYCURSOR >= YYLIMIT";
    re2c:define:YYPEEK = "YYCURSOR < YYLIMIT ? (unsigned char)*YYCURSOR : 0";
    re2c:define:YYSKIP = "++YYCURSOR;";
    re2c:define:YYBACKUP = "YYMARKER = YYCURSOR;";
    re2c:define:YYRESTORE = "YYCURSOR = YYMARKER;";

    whitespace = (White_Space | [\x1c-\x1f])+;
    not_prefix = "not" whitespace;
    digits = Nd+ ("_" Nd+)*;
    decimal_integer = [1-9] ("_"? Nd)* | "0" ("_"? "0")*;
    binary_digits = [0-1]+ ("_" [0-1]+)*;
    octal_digits = [0-7]+ ("_" [0-7]+)*;
    hexadecimal_digits = (Nd | [a-fA-F])+ ("_" (Nd | [a-fA-F])+)*;
    float = [+-]? digits (("." digits)? [eE] [+-]? digits | "." digits);
    based_integer = "0" ([bB] "_"? binary_digits | [oO] "_"? octal_digits | [xX] "_"? hexadecimal_digits);
    integer = [+-]? (based_integer | decimal_integer);
    escape_hex = [0-9a-fA-F];
    // Python's unicode-escape permits surrogates but rejects values above U+10FFFF.
    escape_codepoint = "00" ("0" escape_hex{5} | "10" escape_hex{4});
    escape = "\\" ([^\x00xuUN] | "x" escape_hex{2} | "u" escape_hex{4} | "U" escape_codepoint | "N{" [A-Za-z0-9 -]+ "}");
    quoted_single_string = "'" ([^'\\] | escape)* "'";
    quoted_double_string = "\"" ([^"\\] | escape)* "\"";
    quoted_string = quoted_single_string | quoted_double_string;
    // Jinja's Python identifier contract excludes join controls.
    identifier = (XID_Start | "_") (XID_Continue \ Join_Control)*;

    $ {
      *kind = 0;
      token->text = YYCURSOR;
      token->offset = (size_t)(YYCURSOR - lexer->input);
      token->length = 0u;
      lexer->cursor = YYCURSOR;
      return JINJA_EXPRESSION_LEX_EOF;
    }

    whitespace { goto scan; }
    not_prefix { *kind = JINJA_EXPRESSION_TOKEN_NOT; goto accept; }
    "not" { *kind = JINJA_EXPRESSION_TOKEN_NOT; goto accept; }
    "and" { *kind = JINJA_EXPRESSION_TOKEN_AND; goto accept; }
    "or" { *kind = JINJA_EXPRESSION_TOKEN_OR; goto accept; }
    "in" { *kind = JINJA_EXPRESSION_TOKEN_IN; goto accept; }
    "if" { *kind = JINJA_EXPRESSION_TOKEN_IF; goto accept; }
    "else" { *kind = JINJA_EXPRESSION_TOKEN_ELSE; goto accept; }
    "is" { *kind = JINJA_EXPRESSION_TOKEN_IS; goto accept; }
    "none" | "None" { *kind = JINJA_EXPRESSION_TOKEN_NONE; goto accept; }
    "true" | "True" { *kind = JINJA_EXPRESSION_TOKEN_TRUE; goto accept; }
    "false" | "False" { *kind = JINJA_EXPRESSION_TOKEN_FALSE; goto accept; }
    float {
      if (token_start > lexer->input && token_start[-1] == '.') {
        /* Jinja excludes a float immediately after a dot: x.0.1 is two
         * item lookups, whereas x. 0.1 still contains a float token. */
        if (*token_start == '-' || *token_start == '+') {
          YYCURSOR = token_start + 1u;
          *kind = *token_start == '-' ? JINJA_EXPRESSION_TOKEN_MINUS : JINJA_EXPRESSION_TOKEN_PLUS;
        } else {
          if (token_start[0] < '0' || token_start[0] > '9') goto syntax_error;
          YYCURSOR = jinja_expression_decimal_end(token_start, YYLIMIT);
          *kind = JINJA_EXPRESSION_TOKEN_INTEGER;
        }
      } else if (!lexer->expects_operand && (token_start[0] == '+' || token_start[0] == '-')) {
        *kind = token_start[0] == '+' ? JINJA_EXPRESSION_TOKEN_PLUS
                                     : JINJA_EXPRESSION_TOKEN_MINUS;
        YYCURSOR = token_start + 1;
      } else {
        // Upstream lexes Unicode float digits but ast.literal_eval rejects them.
        if (!jinja_expression_ascii_number(token_start, YYCURSOR)) goto syntax_error;
        *kind = JINJA_EXPRESSION_TOKEN_FLOAT;
      }
      goto accept;
    }
    integer {
      if (!lexer->expects_operand && (token_start[0] == '+' || token_start[0] == '-')) {
        *kind = token_start[0] == '+' ? JINJA_EXPRESSION_TOKEN_PLUS
                                     : JINJA_EXPRESSION_TOKEN_MINUS;
        YYCURSOR = token_start + 1;
      } else {
        *kind = JINJA_EXPRESSION_TOKEN_INTEGER;
      }
      goto accept;
    }
    quoted_string (whitespace? quoted_string)* {
      if (!jinja_expression_valid_named_escapes(token_start, YYCURSOR)) goto syntax_error;
      *kind = JINJA_EXPRESSION_TOKEN_STRING;
      goto accept;
    }
    Nd { goto syntax_error; }
    "==" { *kind = JINJA_EXPRESSION_TOKEN_EQUAL; goto accept; }
    "!=" { *kind = JINJA_EXPRESSION_TOKEN_NOT_EQUAL; goto accept; }
    "<=" { *kind = JINJA_EXPRESSION_TOKEN_LESS_EQUAL; goto accept; }
    ">=" { *kind = JINJA_EXPRESSION_TOKEN_GREATER_EQUAL; goto accept; }
    "<" { *kind = JINJA_EXPRESSION_TOKEN_LESS; goto accept; }
    ">" { *kind = JINJA_EXPRESSION_TOKEN_GREATER; goto accept; }
    "**" { *kind = JINJA_EXPRESSION_TOKEN_POWER; goto accept; }
    "//" { *kind = JINJA_EXPRESSION_TOKEN_FLOOR_DIVIDE; goto accept; }
    "*" { *kind = JINJA_EXPRESSION_TOKEN_MULTIPLY; goto accept; }
    "/" { *kind = JINJA_EXPRESSION_TOKEN_TRUE_DIVIDE; goto accept; }
    "%" { *kind = JINJA_EXPRESSION_TOKEN_MODULO; goto accept; }
    "+" { *kind = JINJA_EXPRESSION_TOKEN_PLUS; goto accept; }
    "~" { *kind = JINJA_EXPRESSION_TOKEN_TILDE; goto accept; }
    "|" { *kind = JINJA_EXPRESSION_TOKEN_PIPE; goto accept; }
    "-" { *kind = JINJA_EXPRESSION_TOKEN_MINUS; goto accept; }
    "." { *kind = JINJA_EXPRESSION_TOKEN_DOT; goto accept; }
    "(" { *kind = JINJA_EXPRESSION_TOKEN_LEFT_PAREN; goto accept; }
    ")" { *kind = JINJA_EXPRESSION_TOKEN_RIGHT_PAREN; goto accept; }
    "[" { *kind = JINJA_EXPRESSION_TOKEN_LEFT_BRACKET; goto accept; }
    "]" { *kind = JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET; goto accept; }
    "{" { *kind = JINJA_EXPRESSION_TOKEN_LEFT_BRACE; goto accept; }
    "}" { *kind = JINJA_EXPRESSION_TOKEN_RIGHT_BRACE; goto accept; }
    "," { *kind = JINJA_EXPRESSION_TOKEN_COMMA; goto accept; }
    ":" { *kind = JINJA_EXPRESSION_TOKEN_COLON; goto accept; }
    identifier { *kind = JINJA_EXPRESSION_TOKEN_IDENTIFIER; goto accept; }

    "=" { *kind = JINJA_EXPRESSION_TOKEN_ASSIGN; goto accept; }

    * {
      *kind = 0;
      token->text = token_start;
      token->offset = (size_t)(token_start - lexer->input);
      token->length = 1u;
      lexer->cursor = YYCURSOR;
      return token_start[0] == '\'' || token_start[0] == '"'
                 ? JINJA_EXPRESSION_LEX_SYNTAX
                 : JINJA_EXPRESSION_LEX_UNSUPPORTED;
    }
  */

accept:
  if (lexer->filter_state == JINJA_EXPRESSION_TEST_LEX_NAME && jinja_expression_token_is_name(*kind)) {
    not_name = *kind == JINJA_EXPRESSION_TOKEN_NOT;
    *kind = JINJA_EXPRESSION_TOKEN_IDENTIFIER;
    lexer->filter_state = JINJA_EXPRESSION_TEST_LEX_ARGUMENT;
  } else if (lexer->filter_state == JINJA_EXPRESSION_TEST_LEX_ARGUMENT && *kind == JINJA_EXPRESSION_TOKEN_DOT) {
    lexer->filter_state = JINJA_EXPRESSION_TEST_LEX_NAME;
  } else lexer->filter_state = JINJA_EXPRESSION_TEST_LEX_NONE;
  if (*kind == JINJA_EXPRESSION_TOKEN_PIPE) lexer->filter_state = JINJA_EXPRESSION_TEST_LEX_NAME;
  /* Jinja parses a test's name before deciding whether a shorthand argument
   * follows. Only and/or/else terminate that argument position; is is rejected
   * by the grammar, while in/if/not are ordinary argument names here. */
  if (lexer->test_state == JINJA_EXPRESSION_TEST_LEX_ARGUMENT) {
    lexer->test_state = *kind == JINJA_EXPRESSION_TOKEN_DOT
                            ? JINJA_EXPRESSION_TEST_LEX_NAME
                            : JINJA_EXPRESSION_TEST_LEX_NONE;
    if (*kind == JINJA_EXPRESSION_TOKEN_IN || *kind == JINJA_EXPRESSION_TOKEN_IF ||
        *kind == JINJA_EXPRESSION_TOKEN_NOT) {
      not_name = *kind == JINJA_EXPRESSION_TOKEN_NOT;
      *kind = JINJA_EXPRESSION_TOKEN_IDENTIFIER;
    }
  } else if (lexer->test_state == JINJA_EXPRESSION_TEST_LEX_OPTIONAL_NOT ||
             lexer->test_state == JINJA_EXPRESSION_TEST_LEX_NAME) {
    if (lexer->test_state == JINJA_EXPRESSION_TEST_LEX_OPTIONAL_NOT &&
        *kind == JINJA_EXPRESSION_TOKEN_NOT) {
      lexer->test_state = JINJA_EXPRESSION_TEST_LEX_NAME;
    } else if (jinja_expression_token_is_name(*kind)) {
      not_name = *kind == JINJA_EXPRESSION_TOKEN_NOT;
      *kind = JINJA_EXPRESSION_TOKEN_IDENTIFIER;
      lexer->test_state = JINJA_EXPRESSION_TEST_LEX_ARGUMENT;
    } else {
      lexer->test_state = JINJA_EXPRESSION_TEST_LEX_NONE;
    }
  }
  /* Contextual names must be classified before Lemon chooses a reduction.
   * In particular, AND after a zero-argument test must remain an operator. */
  if (lexer->expects_operand && !lexer->expects_membership_in &&
      (*kind == JINJA_EXPRESSION_TOKEN_AND || *kind == JINJA_EXPRESSION_TOKEN_OR ||
       *kind == JINJA_EXPRESSION_TOKEN_IN || *kind == JINJA_EXPRESSION_TOKEN_IF ||
       *kind == JINJA_EXPRESSION_TOKEN_ELSE || *kind == JINJA_EXPRESSION_TOKEN_IS))
    *kind = JINJA_EXPRESSION_TOKEN_IDENTIFIER;
  if (*kind == JINJA_EXPRESSION_TOKEN_IS)
    lexer->test_state = JINJA_EXPRESSION_TEST_LEX_OPTIONAL_NOT;
  lexer->expects_membership_in =
      *kind == JINJA_EXPRESSION_TOKEN_NOT && !lexer->expects_operand;
  token->text = token_start;
  token->offset = (size_t)(token_start - lexer->input);
  token->length = (size_t)(YYCURSOR - token_start);
  if (not_name) token->length = sizeof("not") - 1u;
  lexer->cursor = YYCURSOR;
  switch (*kind) {
  case JINJA_EXPRESSION_TOKEN_TRUE:
  case JINJA_EXPRESSION_TOKEN_FALSE:
  case JINJA_EXPRESSION_TOKEN_NONE:
  case JINJA_EXPRESSION_TOKEN_INTEGER:
  case JINJA_EXPRESSION_TOKEN_FLOAT:
  case JINJA_EXPRESSION_TOKEN_STRING:
  case JINJA_EXPRESSION_TOKEN_IDENTIFIER:
  case JINJA_EXPRESSION_TOKEN_RIGHT_PAREN:
  case JINJA_EXPRESSION_TOKEN_RIGHT_BRACKET:
  case JINJA_EXPRESSION_TOKEN_RIGHT_BRACE:
    lexer->expects_operand = 0;
    break;
  default:
    lexer->expects_operand = 1;
    break;
  }
  return JINJA_EXPRESSION_LEX_TOKEN;

syntax_error:
  *kind = 0;
  token->text = token_start;
  token->offset = (size_t)(token_start - lexer->input);
  token->length = (size_t)(YYCURSOR - token_start);
  lexer->cursor = YYCURSOR;
  return JINJA_EXPRESSION_LEX_SYNTAX;
}

size_t jinja_expression_lexical_token_length(vstr source, size_t offset) {
  if (offset >= source.len) return 0u;
  vstr input = vstr_from_buf(source.data + offset, source.len - offset);
  const char *YYCURSOR = input.data;
  const char *YYLIMIT = input.data + input.len;
  const char *YYMARKER;
  /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:encoding:utf8 = 1;
    re2c:encoding-policy = fail;
    re2c:yyfill:enable = 0;
    re2c:eof = 0;
    re2c:api = custom;
    re2c:api:style = free-form;
    re2c:define:YYLESSTHAN = "YYCURSOR >= YYLIMIT";
    re2c:define:YYPEEK = "YYCURSOR < YYLIMIT ? (unsigned char)*YYCURSOR : 0";
    re2c:define:YYSKIP = "++YYCURSOR;";
    re2c:define:YYBACKUP = "YYMARKER = YYCURSOR;";
    re2c:define:YYRESTORE = "YYCURSOR = YYMARKER;";
    tag_float = digits (("." digits)? [eE] [+-]? digits | "." digits);
    $ { return 0u; }
    tag_float {
      if (offset != 0u && source.data[offset - 1u] == '.') {
        if (input.data[0] < '0' || input.data[0] > '9') return SIZE_MAX;
        YYCURSOR = jinja_expression_decimal_end(input.data, YYLIMIT);
      } else if (!jinja_expression_ascii_number(input.data, YYCURSOR)) return SIZE_MAX;
      return (size_t)(YYCURSOR - input.data);
    }
    quoted_string {
      return jinja_expression_valid_named_escapes(input.data, YYCURSOR)
                 ? (size_t)(YYCURSOR - input.data) : SIZE_MAX;
    }
    whitespace | based_integer | decimal_integer | identifier |
        "==" | "!=" | "<=" | ">=" | "**" | "//" {
      return (size_t)(YYCURSOR - input.data);
    }
    * { return input.data[0] == '\'' || input.data[0] == '"' ? SIZE_MAX : (size_t)(YYCURSOR - input.data); }
  */
}

/* Reuse the integer DFA when a float candidate is excluded by dot context. */
static const char *jinja_expression_decimal_end(const char *cursor, const char *limit) {
  const char *YYCURSOR = cursor, *YYLIMIT = limit, *YYMARKER;
  /*!re2c
    re2c:define:YYCTYPE = "unsigned char";
    re2c:yyfill:enable = 0;
    re2c:eof = 0;
    re2c:api = custom;
    re2c:api:style = free-form;
    re2c:define:YYLESSTHAN = "YYCURSOR >= YYLIMIT";
    re2c:define:YYPEEK = "YYCURSOR < YYLIMIT ? (unsigned char)*YYCURSOR : 0";
    re2c:define:YYSKIP = "++YYCURSOR;";
    re2c:define:YYBACKUP = "YYMARKER = YYCURSOR;";
    re2c:define:YYRESTORE = "YYCURSOR = YYMARKER;";
    decimal_integer { return YYCURSOR; }
    $ { return YYCURSOR; }
    * { return YYCURSOR; }
  */
}
