/**
 * @file schema_grammar.y
 * @brief Schema Grammar for tbe_compiler (Lemon)
 *
 * Supports the TBE-like declarations:
 *   schema Market [id(7), version(2), byte_order(little)];
 *   composite Header { uint32 seq; }
 *   group Level { uint64 price; uint32 qty; }
 *   [id(100)] message BookSnapshot { group<Level> bids; string symbol; }
 */

%name SchemaParse
%token_prefix SCHEMA_TOKEN_
%token_type {schema_token_t}
%default_type {schema_token_t}

%extra_argument {schema_parse_ctx_t *ctx}

%include {
#include "schema_lexer.h"
#include "schema_builtin_type.h"
#include "schema_types.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *tok_strdup(schema_token_t t) {
    if (!t.value || t.length == 0) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    
    // Prevent integer overflow
    if (t.length > SIZE_MAX - 1) {
        return NULL;
    }
    
    char *s = (char *)malloc(t.length + 1);
    if (!s) return NULL;
    
    memcpy(s, t.value, t.length);
    s[t.length] = '\0';
    return s;
}

static int tok_to_ull(schema_token_t t, unsigned long long *out) {
    char *text = tok_strdup(t);
    char *end = NULL;
    unsigned long long value;
    int ok;

    if (text == NULL) return 0;
    value = strtoull(text, &end, 0);
    ok = (text[0] != '\0' && end && *end == '\0');

    free(text);
    if (!ok) {
        return 0;
    }

    *out = value;
    return 1;
}

static int is_numeric_literal(const char *text) {
    if (!text || !text[0]) {
        return 0;
    }

    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

static void grammar_oom(schema_parse_ctx_t *ctx) {
    if (ctx == NULL || ctx->error) return;
    ctx->error = 1;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
             "Out of memory building schema tree");
}

static void add_string(schema_parse_ctx_t *ctx, Node *map, const char *name,
                       const char *value) {
    Node *node;
    if (ctx->error) return;
    node = create_node_string(name, value);
    if (node == NULL || map_add(map, node) != 0) {
        node_free(node);
        grammar_oom(ctx);
    }
}

static void add_true(schema_parse_ctx_t *ctx, Node *map, const char *name) {
    add_string(ctx, map, name, "1");
}

static void add_name_nodes(schema_parse_ctx_t *ctx, Node *map,
                           const char *specific_key, const char *name) {
    add_string(ctx, map, specific_key, name);
    add_string(ctx, map, "name", name);
}

static const char *map_get_string_value(Node *map, const char *name) {
    if (!map || map->type != NODE_MAP) {
        return NULL;
    }

    for (size_t i = 0; i < map->data.map.count; ++i) {
        Node *child = map->data.map.items[i];
        if (!child || child->type != NODE_STRING || !child->name) {
            continue;
        }
        if (strcmp(child->name, name) == 0) {
            return child->data.string_val;
        }
    }

    return NULL;
}

static void mark_record_kind(schema_parse_ctx_t *ctx, Node *record,
                             schema_record_kind_t kind) {
    switch (kind) {
    case SCHEMA_RECORD_COMPOSITE:
        add_string(ctx, record, "decl_kind", "composite");
        add_true(ctx, record, "is_composite_decl");
        break;
    case SCHEMA_RECORD_GROUP:
        add_string(ctx, record, "decl_kind", "group");
        add_true(ctx, record, "is_group_decl");
        break;
    case SCHEMA_RECORD_MESSAGE:
        add_string(ctx, record, "decl_kind", "message");
        add_true(ctx, record, "is_message_decl");
        break;
    case SCHEMA_RECORD_UNION:
        add_string(ctx, record, "decl_kind", "union");
        add_true(ctx, record, "is_union_decl");
        break;
    default:
        fprintf(stderr, "schema_grammar: internal error unknown record kind\n");
        abort();
    }
}

static void begin_record(schema_parse_ctx_t *ctx, Node *list,
                         schema_record_kind_t kind,
                         const char *name_key, const char *name) {
    Node *new_record = create_node_map(NULL);
    Node *new_fields = create_node_list("fields");
    ctx->cur_record = NULL;
    ctx->cur_fields = NULL;
    if (name == NULL || new_record == NULL || new_fields == NULL) {
        node_free(new_record);
        node_free(new_fields);
        grammar_oom(ctx);
        return;
    }

    add_name_nodes(ctx, new_record, name_key, name);
    mark_record_kind(ctx, new_record, kind);
    if (ctx->error) {
        node_free(new_record);
        node_free(new_fields);
        return;
    }
    if (map_add(new_record, new_fields) != 0) {
        node_free(new_record);
        node_free(new_fields);
        grammar_oom(ctx);
        return;
    }
    if (list_add(list, new_record) != 0) {
        node_free(new_record); /* owns the attached fields list */
        grammar_oom(ctx);
        return;
    }
    ctx->cur_record = new_record;
    ctx->cur_record_kind = kind;
    ctx->cur_field_section = SCHEMA_FIELD_SECTION_FIXED;
    ctx->cur_fields = new_fields;
}

static void begin_enum_like(schema_parse_ctx_t *ctx, const char *name,
                            const char *underlying_type, int is_flags) {
    Node *node = create_node_map(NULL);
    Node *items = create_node_list("items");
    ctx->cur_enum = NULL;
    ctx->cur_enum_items = NULL;
    ctx->next_enum_value = is_flags ? 1 : 0;
    if (name == NULL || node == NULL || items == NULL) {
        node_free(node);
        node_free(items);
        grammar_oom(ctx);
        return;
    }
    add_name_nodes(ctx, node, "enum_name", name);
    if (underlying_type != NULL) {
        add_string(ctx, node, "underlying_type", underlying_type);
    }
    if (is_flags) {
        add_string(ctx, node, "is_flags", "1");
    }
    if (ctx->error) {
        node_free(node);
        node_free(items);
        return;
    }
    if (map_add(node, items) != 0) {
        node_free(node);
        node_free(items);
        grammar_oom(ctx);
        return;
    }
    if (list_add(ctx->enums_list, node) != 0) {
        node_free(node); /* owns the attached items list */
        grammar_oom(ctx);
        return;
    }
    ctx->cur_enum = node;
    ctx->cur_enum_items = items;
}

static schema_field_section_t classify_field_section(const char *field_type,
                                                     int is_collection,
                                                     int is_group_field,
                                                     const char *length_field) {
    if (is_group_field) {
        return SCHEMA_FIELD_SECTION_GROUP;
    }

    if (strcmp(field_type, "string") == 0) {
        return SCHEMA_FIELD_SECTION_VAR_DATA;
    }

    if (strcmp(field_type, "bytes") == 0 && !is_numeric_literal(length_field)) {
        return SCHEMA_FIELD_SECTION_VAR_DATA;
    }

    if (strcmp(field_type, "array") == 0 && is_numeric_literal(length_field)) {
        return SCHEMA_FIELD_SECTION_FIXED;
    }

    if (is_collection) {
        return SCHEMA_FIELD_SECTION_VAR_DATA;
    }

    return SCHEMA_FIELD_SECTION_FIXED;
}

static int field_supported_in_tbe(const char *field_type,
                                  int is_collection,
                                  int is_group_field,
                                  const char *length_field) {
    if (is_group_field) {
        return 1;
    }

    if (!is_collection) {
        return 1;
    }

    if (strcmp(field_type, "array") == 0) {
        return is_numeric_literal(length_field);
    }

    return strcmp(field_type, "list") == 0 ||
           strcmp(field_type, "set") == 0 ||
           strcmp(field_type, "map") == 0;
}

static int validate_field_layout(schema_parse_ctx_t *ctx,
                                 const char *field_type,
                                 int is_collection,
                                 int is_group_field,
                                 const char *length_field) {
    schema_field_section_t section;

    if (field_type == NULL) {
        grammar_oom(ctx);
        return 0;
    }

    /* Union variants are user-defined type references — no layout constraints */
    if (ctx->cur_record_kind == SCHEMA_RECORD_UNION) {
        return 1;
    }

    if (!field_supported_in_tbe(field_type, is_collection, is_group_field, length_field)) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Unsupported dynamic collection in tbe declaration");
        ctx->error = 1;
        return 0;
    }

    section = classify_field_section(field_type, is_collection, is_group_field, length_field);
    if (ctx->cur_record_kind == SCHEMA_RECORD_COMPOSITE &&
        section != SCHEMA_FIELD_SECTION_FIXED) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Composite fields must be fixed-size");
        ctx->error = 1;
        return 0;
    }

    if (ctx->cur_record_kind == SCHEMA_RECORD_MESSAGE ||
        ctx->cur_record_kind == SCHEMA_RECORD_GROUP) {
        if (section > ctx->cur_field_section) {
            ctx->cur_field_section = section;
        }
    }

    return 1;
}

static void annotate_field(schema_parse_ctx_t *ctx, Node *field_map, const char *field_type,
                           int is_collection, const char *collection_inner,
                           const char *length_field, int is_group_field) {
    const schema_builtin_type_info_t *builtin_type = schema_builtin_type_find(field_type);
    int size = 0;
    int is_numeric = 0;
    int is_unsigned = 0;
    int is_uuid = 0;
    const char *host_type = NULL;
    const char *wire_reader = NULL;
    const char *map_value_type = NULL;

    if (is_group_field) {
        add_string(ctx, field_map, "ctype", "GROUP");
        add_true(ctx, field_map, "is_group_field");
        add_true(ctx, field_map, "is_variable_size");
        if (collection_inner && collection_inner[0]) {
            add_string(ctx, field_map, "group_type", collection_inner);
            add_string(ctx, field_map, "inner_type", collection_inner);
        }
    } else if (strcmp(field_type, "varint") == 0) {
        add_string(ctx, field_map, "ctype", "VARINT");
        add_true(ctx, field_map, "is_varint");
        add_true(ctx, field_map, "is_variable_size");
    } else if (builtin_type != NULL &&
               (builtin_type->is_integer || builtin_type->is_float)) {
        size = (int)builtin_type->size;
        is_numeric = 1;
        is_unsigned = builtin_type->is_unsigned;
        host_type = builtin_type->host_type;
        wire_reader = builtin_type->wire_reader;
    } else if (strcmp(field_type, "uuid") == 0) {
        size = 16; is_uuid = 1;
    }

    if (is_numeric) {
        char size_text[16];
        snprintf(size_text, sizeof(size_text), "%d", size);
        add_string(ctx, field_map, "size_bytes", size_text);
        if (host_type) {
            add_string(ctx, field_map, "host_type", host_type);
        }
        if (wire_reader) {
            add_string(ctx, field_map, "wire_reader", wire_reader);
        }
        add_true(ctx, field_map, "is_numeric");
        if (is_unsigned) {
            add_true(ctx, field_map, "is_unsigned");
        }
        add_true(ctx, field_map, "is_primitive");
        add_true(ctx, field_map, "is_fixed_size");
    }

    if (is_uuid) {
        char size_text[16];
        snprintf(size_text, sizeof(size_text), "%d", size);
        add_string(ctx, field_map, "ctype", "UUID");
        add_string(ctx, field_map, "size_bytes", size_text);
        add_string(ctx, field_map, "host_type", "turbo_uuid_t");
        add_true(ctx, field_map, "is_uuid");
        add_true(ctx, field_map, "is_primitive");
        add_true(ctx, field_map, "is_fixed_size");
    }

    if (!is_group_field && builtin_type != NULL && builtin_type->is_integer) {
        add_true(ctx, field_map, "is_integer");
    } else if (!is_group_field && builtin_type != NULL && builtin_type->is_float) {
        add_true(ctx, field_map, "is_float");
    } else if (!is_group_field && strcmp(field_type, "bytes") == 0) {
        add_string(ctx, field_map, "ctype", "BYTES");
        add_true(ctx, field_map, "is_bytes");
        if (is_numeric_literal(length_field)) {
            add_string(ctx, field_map, "size_bytes", length_field);
            add_true(ctx, field_map, "is_fixed_size");
        } else {
            add_true(ctx, field_map, "is_variable_size");
        }
    } else if (!is_group_field && strcmp(field_type, "string") == 0) {
        add_string(ctx, field_map, "ctype", "STRING");
        add_true(ctx, field_map, "is_string");
        add_true(ctx, field_map, "is_variable_size");
    } else if (!is_group_field && (is_collection || strcmp(field_type, "array") == 0 ||
               strcmp(field_type, "list") == 0 || strcmp(field_type, "set") == 0 ||
               strcmp(field_type, "map") == 0)) {
        add_string(ctx, field_map, "ctype", "COLLECTION");
        add_true(ctx, field_map, "is_collection");
        add_string(ctx, field_map, "collection_kind", field_type);
        if (strcmp(field_type, "list") == 0) {
            add_true(ctx, field_map, "is_list");
        } else if (strcmp(field_type, "set") == 0) {
            add_true(ctx, field_map, "is_set");
        } else if (strcmp(field_type, "map") == 0) {
            add_true(ctx, field_map, "is_map");
        }
        if (strcmp(field_type, "map") == 0 && collection_inner && collection_inner[0]) {
            map_value_type = strchr(collection_inner, ',');
            if (map_value_type != NULL) {
                size_t key_len = (size_t)(map_value_type - collection_inner);
                char key_type[128];
                char value_type[128];

                if (key_len < sizeof(key_type)) {
                    memcpy(key_type, collection_inner, key_len);
                    key_type[key_len] = '\0';
                    add_string(ctx, field_map, "key_type", key_type);
                }
                snprintf(value_type, sizeof(value_type), "%s", map_value_type + 1);
                add_string(ctx, field_map, "value_type", value_type);
                add_string(ctx, field_map, "inner_type", value_type);
            }
        } else if (collection_inner && collection_inner[0]) {
            add_string(ctx, field_map, "inner_type", collection_inner);
        }
        if (is_numeric_literal(length_field)) {
            add_true(ctx, field_map, "is_fixed_size");
        } else {
            add_true(ctx, field_map, "is_variable_size");
        }
    } else if (!is_group_field && !is_numeric) {
        add_string(ctx, field_map, "ctype", "USER_DEFINED");
        add_true(ctx, field_map, "is_user_defined");
    }

    if (length_field && length_field[0]) {
        add_string(ctx, field_map, "length_field", length_field);
        add_true(ctx, field_map, "has_length_field");
    }

    if (is_group_field) {
        /* group fields are their own section */
    } else if (strcmp(field_type, "string") == 0 ||
               (strcmp(field_type, "bytes") == 0 && !is_numeric_literal(length_field))) {
        add_true(ctx, field_map, "is_var_data");
    } else if (!is_collection ||
               (strcmp(field_type, "array") == 0 && is_numeric_literal(length_field)) ||
               (strcmp(field_type, "bytes") == 0 && is_numeric_literal(length_field))) {
        add_true(ctx, field_map, "is_fixed_block");
    }
}

static void add_field(schema_parse_ctx_t *ctx,
                      const char *type_str, const char *name_str,
                      int is_collection, const char *inner, const char *len_field,
                      Node *attrs, int is_group_field, int is_optional, const char *default_value) {
    Node *field_map;

    if (ctx->error) {
        node_free(attrs);
        return;
    }
    if (!validate_field_layout(ctx, type_str, is_collection, is_group_field, len_field)) {
        node_free(attrs);
        return;
    }

    field_map = create_node_map(NULL);
    if (field_map == NULL) {
        grammar_oom(ctx);
        return;
    }
    add_string(ctx, field_map, "type", type_str);
    add_string(ctx, field_map, "name", name_str);
    add_string(ctx, field_map, "owner_name",
               map_get_string_value(ctx->cur_record, "name"));
    
    if (is_optional) {
        add_true(ctx, field_map, "is_optional");
    }
    
    if (default_value && default_value[0] != '\0') {
        add_string(ctx, field_map, "default_value", default_value);
        add_true(ctx, field_map, "has_default");
    }

    if (attrs != NULL) {
        if (map_add(field_map, attrs) != 0) {
            grammar_oom(ctx);
        }
    }
    
    annotate_field(ctx, field_map, type_str, is_collection, inner, len_field, is_group_field);
    if (list_add(ctx->cur_fields, field_map) != 0) {
        node_free(field_map);
        grammar_oom(ctx);
    }
}

static Node *create_attribute_node(schema_parse_ctx_t *ctx, schema_token_t key_tok,
                                   schema_token_t value_tok) {
    char *key = tok_strdup(key_tok);
    char *value = tok_strdup(value_tok);
    Node *attr;
    if (ctx->error) {
        free(key);
        free(value);
        return NULL;
    }
    attr = create_node_map(key);

    if (attr == NULL) {
        grammar_oom(ctx);
    } else {
        add_string(ctx, attr, "name", key);
        add_string(ctx, attr, "value", value);
    }

    free(key);
    free(value);
    return attr;
}

static void add_enum_item(schema_parse_ctx_t *ctx, const char *key, const char *value) {
    Node *item;
    if (ctx->error) return;
    item = create_node_map(NULL);
    if (item == NULL) {
        grammar_oom(ctx);
        return;
    }
    add_string(ctx, item, "name", key);
    add_string(ctx, item, "value", value);
    if (list_add(ctx->cur_enum_items, item) != 0) {
        grammar_oom(ctx);
    }
}
}

%type attribute_list {Node *}
%type attr_items {Node *}
%type attr_item {Node *}
%type field_default {char *}
%type field_qualifier {int}
%destructor attribute_list { (void)ctx; node_free($$); }
%destructor attr_items { (void)ctx; node_free($$); }
%destructor field_default { (void)ctx; free($$); }
%destructor attr_item { (void)ctx; node_free($$); }

%token ENUM FLAGS NUMBER DEFAULT_NUMBER EQUALS IDENT LBRACE RBRACE SEMI LPAREN RPAREN LBRACKET RBRACKET LT GT COMMA MESSAGE COMPOSITE GROUP SCHEMA REQUIRED OPTIONAL DEFAULT STRING TRUE FALSE UNION.

start ::= schema.
schema ::= decl_list.

decl_list ::= decl_list decl.
decl_list ::= .

decl ::= enum_decl.
decl ::= flags_decl.
decl ::= message_decl.
decl ::= composite_decl.
decl ::= group_decl.
decl ::= schema_decl.
decl ::= union_decl.

attribute_list(A) ::= LBRACKET attr_items(B) RBRACKET. { A = B; }
attribute_list(A) ::= . { A = NULL; }

attr_items(A) ::= attr_items(B) COMMA attr_item(C). {
    A = B;
    if (map_add(A, C) != 0) {
        node_free(C);
        grammar_oom(ctx);
    }
}
attr_items(A) ::= attr_item(B). {
    A = create_node_list("attributes");
    if (A == NULL || list_add(A, B) != 0) {
        node_free(A);
        A = NULL;
        node_free(B);
        grammar_oom(ctx);
    }
}

attr_item(A) ::= IDENT(K) LPAREN IDENT(V) RPAREN. { A = create_attribute_node(ctx, K, V); }
attr_item(A) ::= IDENT(K) LPAREN NUMBER(V) RPAREN. { A = create_attribute_node(ctx, K, V); }
attr_item(A) ::= IDENT(K) LPAREN STRING(V) RPAREN. { A = create_attribute_node(ctx, K, V); }

schema_decl ::= SCHEMA IDENT(N) attribute_list(A) SEMI. {
    if (ctx->schema_node != NULL) {
        node_free(A);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Duplicate schema declaration");
        ctx->error = 1;
    } else {
        char *schema_name = tok_strdup(N);
        ctx->schema_node = create_node_map("schema");
        if (schema_name == NULL || ctx->schema_node == NULL) {
            node_free(ctx->schema_node);
            ctx->schema_node = NULL;
            free(schema_name);
            node_free(A);
            grammar_oom(ctx);
        } else {
            add_name_nodes(ctx, ctx->schema_node, "schema_name", schema_name);
            if (A && map_add(ctx->schema_node, A) != 0) {
                node_free(A);
                grammar_oom(ctx);
            }
            if (map_add(ctx->root, ctx->schema_node) != 0) {
                node_free(ctx->schema_node);
                ctx->schema_node = NULL;
                grammar_oom(ctx);
            }
        }
        free(schema_name);
    }
}

enum_decl ::= attribute_list(A) enum_header enum_body RBRACE. {
    if (A && ctx->cur_enum != NULL) {
        if (map_add(ctx->cur_enum, A) != 0) {
            node_free(A);
            grammar_oom(ctx);
        }
    } else if (A) {
        node_free(A);
    }
}

enum_header ::= ENUM IDENT(N) LBRACE. {
    char *enum_name = tok_strdup(N);
    begin_enum_like(ctx, enum_name, NULL, 0);
    free(enum_name);
}

enum_header ::= ENUM IDENT(N) LT IDENT(T) GT LBRACE. {
    char *enum_name = tok_strdup(N);
    char *underlying_type = tok_strdup(T);
    begin_enum_like(ctx, enum_name, underlying_type, 0);
    free(enum_name);
    free(underlying_type);
}

enum_body ::= enum_body enum_item.
enum_body ::= .

enum_item ::= IDENT(K) EQUALS NUMBER(V) SEMI. {
    char *key = tok_strdup(K);
    char *value = tok_strdup(V);
    unsigned long long next_value = 0;

    add_enum_item(ctx, key, value);
    if (tok_to_ull(V, &next_value)) {
        ctx->next_enum_value = next_value + 1;
    }

    free(key);
    free(value);
}

enum_item ::= IDENT(K) SEMI. {
    char *key = tok_strdup(K);
    char value_buf[32];

    snprintf(value_buf, sizeof(value_buf), "%llu", ctx->next_enum_value);
    add_enum_item(ctx, key, value_buf);
    ctx->next_enum_value++;

    free(key);
}

// Flags declarations (similar to enum but with is_flags marker)
flags_decl ::= attribute_list(A) flags_header flags_body RBRACE. {
    if (A && ctx->cur_enum != NULL) {
        if (map_add(ctx->cur_enum, A) != 0) {
            node_free(A);
            grammar_oom(ctx);
        }
    } else if (A) {
        node_free(A);
    }
}

flags_header ::= FLAGS IDENT(N) LBRACE. {
    char *flags_name = tok_strdup(N);
    begin_enum_like(ctx, flags_name, NULL, 1);
    free(flags_name);
}

flags_header ::= FLAGS IDENT(N) LT IDENT(T) GT LBRACE. {
    char *flags_name = tok_strdup(N);
    char *underlying_type = tok_strdup(T);
    begin_enum_like(ctx, flags_name, underlying_type, 1);
    free(flags_name);
    free(underlying_type);
}

flags_body ::= flags_body flags_item.
flags_body ::= .

flags_item ::= IDENT(K) EQUALS NUMBER(V) SEMI. {
    char *key = tok_strdup(K);
    char *value = tok_strdup(V);
    unsigned long long next_value = 0;

    add_enum_item(ctx, key, value);
    if (tok_to_ull(V, &next_value)) {
        // For flags, next value is next power of 2
        ctx->next_enum_value = next_value << 1;
    }

    free(key);
    free(value);
}

flags_item ::= IDENT(K) SEMI. {
    char *key = tok_strdup(K);
    char value_buf[32];

    snprintf(value_buf, sizeof(value_buf), "%llu", ctx->next_enum_value);
    add_enum_item(ctx, key, value_buf);
    // Next power of 2
    ctx->next_enum_value = ctx->next_enum_value << 1;

    free(key);
}

composite_decl ::= attribute_list(A) composite_header field_list RBRACE. {
    if (A && ctx->cur_record != NULL) {
        if (map_add(ctx->cur_record, A) != 0) {
            node_free(A);
            grammar_oom(ctx);
        }
    } else if (A) {
        node_free(A);
    }
}

union_decl ::= attribute_list(A) union_header union_body RBRACE. {
    if (A && ctx->cur_record != NULL) {
        if (map_add(ctx->cur_record, A) != 0) {
            node_free(A);
            grammar_oom(ctx);
        }
    } else if (A) {
        node_free(A);
    }
}

union_header ::= UNION IDENT(N) LBRACE. {
    char *record_name = tok_strdup(N);
    begin_record(ctx, ctx->unions_list, SCHEMA_RECORD_UNION, "union_name", record_name);
    free(record_name);
}

union_body ::= union_body union_variant.
union_body ::= .

union_variant ::= attribute_list(A) IDENT(T) IDENT(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *field_name = tok_strdup(N);
    add_field(ctx, type_name, field_name, 0, "", "", A, 0, 0, NULL);
    free(type_name);
    free(field_name);
}

composite_header ::= COMPOSITE IDENT(N) LBRACE. {
    char *record_name = tok_strdup(N);
    begin_record(ctx, ctx->composites_list, SCHEMA_RECORD_COMPOSITE, "composite_name",
                 record_name);
    free(record_name);
}

group_decl ::= attribute_list(A) group_header field_list RBRACE. {
    if (A && ctx->cur_record != NULL) {
        if (map_add(ctx->cur_record, A) != 0) {
            node_free(A);
            grammar_oom(ctx);
        }
    } else if (A) {
        node_free(A);
    }
}

group_header ::= GROUP IDENT(N) LBRACE. {
    char *record_name = tok_strdup(N);
    begin_record(ctx, ctx->groups_list, SCHEMA_RECORD_GROUP, "group_name", record_name);
    free(record_name);
}

message_decl ::= attribute_list(A) message_header field_list RBRACE. {
    if (A && ctx->cur_record != NULL) {
        if (map_add(ctx->cur_record, A) != 0) {
            node_free(A);
            grammar_oom(ctx);
        }
    } else if (A) {
        node_free(A);
    }
}

message_header ::= MESSAGE IDENT(N) LBRACE. {
    char *record_name = tok_strdup(N);
    begin_record(ctx, ctx->messages_list, SCHEMA_RECORD_MESSAGE, "message_name",
                 record_name);
    free(record_name);
}

field_list ::= field_list field_decl.
field_list ::= .

field_decl ::= field_qualifier(Q) attribute_list(A) IDENT(T) IDENT(N) field_default(D) SEMI. {
    char *type_name = tok_strdup(T);
    char *field_name = tok_strdup(N);
    int is_optional = (Q == 1);
    add_field(ctx, type_name, field_name, 0, "", "", A, 0, is_optional, D);
    free(type_name);
    free(field_name);
    if (D) free(D);
}

field_default(D) ::= DEFAULT NUMBER(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT DEFAULT_NUMBER(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT STRING(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT TRUE(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT FALSE(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT IDENT(V). { D = tok_strdup(V); }
field_default(D) ::= . { D = NULL; }

field_decl ::= field_qualifier(Q) attribute_list(A) IDENT(T) LPAREN IDENT(L) RPAREN IDENT(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *length_field = tok_strdup(L);
    char *field_name = tok_strdup(N);
    int is_optional = (Q == 1);
    add_field(ctx, type_name, field_name, 0, "", length_field, A, 0, is_optional, NULL);
    free(type_name);
    free(length_field);
    free(field_name);
}

field_decl ::= field_qualifier(Q) attribute_list(A) IDENT(T) LPAREN NUMBER(L) RPAREN IDENT(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *length_field = tok_strdup(L);
    char *field_name = tok_strdup(N);
    int is_optional = (Q == 1);
    add_field(ctx, type_name, field_name, 0, "", length_field, A, 0, is_optional, NULL);
    free(type_name);
    free(length_field);
    free(field_name);
}

field_decl ::= field_qualifier(Q) attribute_list(A) GROUP LT IDENT(I) GT IDENT(N) SEMI. {
    char *group_type = tok_strdup(I);
    char *field_name = tok_strdup(N);
    int is_optional = (Q == 1);
    add_field(ctx, "group", field_name, 0, group_type, "", A, 1, is_optional, NULL);
    free(group_type);
    free(field_name);
}

field_decl ::= field_qualifier(Q) attribute_list(A) IDENT(T) LBRACKET IDENT(L) RBRACKET IDENT(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *length_field = tok_strdup(L);
    char *field_name = tok_strdup(N);
    int is_optional = (Q == 1);
    add_field(ctx, "array", field_name, 1, type_name, length_field, A, 0, is_optional, NULL);
    free(type_name);
    free(length_field);
    free(field_name);
}

field_decl ::= field_qualifier(Q) attribute_list(A) IDENT(T) LBRACKET NUMBER(L) RBRACKET IDENT(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *length_field = tok_strdup(L);
    char *field_name = tok_strdup(N);
    int is_optional = (Q == 1);
    add_field(ctx, "array", field_name, 1, type_name, length_field, A, 0, is_optional, NULL);
    free(type_name);
    free(length_field);
    free(field_name);
}

field_decl ::= field_qualifier(Q) attribute_list(A) IDENT(T) LT IDENT(I) GT IDENT(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *inner_type = tok_strdup(I);
    char *field_name = tok_strdup(N);
    int is_optional = (Q == 1);
    add_field(ctx, type_name, field_name, 1, inner_type, "", A, 0, is_optional, NULL);
    free(type_name);
    free(inner_type);
    free(field_name);
}

field_decl ::= field_qualifier(Q) attribute_list(A) IDENT(T) LT IDENT(K) COMMA IDENT(V) GT IDENT(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *field_name = tok_strdup(N);
    char *key_type = tok_strdup(K);
    char *value_type = tok_strdup(V);
    char map_inner[256];
    int is_optional = (Q == 1);

    if (key_type == NULL || value_type == NULL) {
        grammar_oom(ctx);
        node_free(A);
    } else {
        snprintf(map_inner, sizeof(map_inner), "%s,%s", key_type, value_type);
        add_field(ctx, type_name, field_name, 1, map_inner, "", A, 0, is_optional, NULL);
    }
    free(type_name);
    free(field_name);
    free(key_type);
    free(value_type);
}

field_qualifier(Q) ::= REQUIRED. { Q = 0; }  // 0 = required
field_qualifier(Q) ::= OPTIONAL. { Q = 1; }  // 1 = optional 
field_qualifier(Q) ::= .         { Q = 0; }  // default = required

%syntax_error {
    // TOKEN is the current token that caused the error
    ctx->error_line = TOKEN.line;
    ctx->error_column = TOKEN.column;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
             "Syntax error at line %d, column %d",
             TOKEN.line, TOKEN.column);
    ctx->error = 1;
}

%parse_failure {
    if (!ctx->error) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Parse failure: unable to recover from syntax errors");
    }
    ctx->error = 1;
}
