/**
 * @file schema_grammar.y
 * @brief DataBind IDL grammar for databindc (Lemon)
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
#include "schema_size.h"
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

static int validate_type_name_supported(schema_parse_ctx_t *ctx, const char *type_name) {
    if (type_name == NULL) {
        grammar_oom(ctx);
        return 0;
    }
    if (strcmp(type_name, "varint") == 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Unsupported type 'varint': TBE runtime/compiler support is not implemented");
        ctx->error = 1;
        return 0;
    }
    return 1;
}

static int validate_fixed_length(schema_parse_ctx_t *ctx,
                                 const char *field_name,
                                 const char *length_text) {
    size_t ignored;

    if (!is_numeric_literal(length_text)) {
        return 1;
    }

    if (!schema_parse_fixed_layout_size(length_text, &ignored)) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Fixed field length '%s' for field '%s' exceeds the safe TBE layout range",
                 length_text, field_name ? field_name : "<unnamed>");
        ctx->error = 1;
        return 0;
    }
    return 1;
}

static char *join_map_inner_types(schema_parse_ctx_t *ctx,
                                  const char *key_type,
                                  const char *value_type) {
    size_t key_len;
    size_t value_len;
    size_t total_len;
    char *joined;

    if (key_type == NULL || value_type == NULL) {
        grammar_oom(ctx);
        return NULL;
    }

    key_len = strlen(key_type);
    value_len = strlen(value_type);
    if (value_len > SIZE_MAX - 2u || key_len > SIZE_MAX - value_len - 2u) {
        grammar_oom(ctx);
        return NULL;
    }

    total_len = key_len + value_len + 1u;
    joined = (char *)malloc(total_len + 1u);
    if (joined == NULL) {
        grammar_oom(ctx);
        return NULL;
    }

    memcpy(joined, key_type, key_len);
    joined[key_len] = ',';
    memcpy(joined + key_len + 1u, value_type, value_len);
    joined[total_len] = '\0';
    return joined;
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

static void add_string_slice(schema_parse_ctx_t *ctx, Node *map, const char *name,
                             const char *value, size_t value_len) {
    char *copy;

    if (ctx->error) return;
    if (value == NULL || value_len > SIZE_MAX - 1u) {
        grammar_oom(ctx);
        return;
    }

    copy = (char *)malloc(value_len + 1u);
    if (copy == NULL) {
        grammar_oom(ctx);
        return;
    }
    memcpy(copy, value, value_len);
    copy[value_len] = '\0';
    add_string(ctx, map, name, copy);
    free(copy);
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
    Node *node;
    Node *items;

    ctx->cur_enum = NULL;
    ctx->cur_enum_items = NULL;
    if (underlying_type != NULL && !validate_type_name_supported(ctx, underlying_type)) {
        return;
    }

    node = create_node_map(NULL);
    items = create_node_list("items");
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
                                 const char *field_name,
                                 int is_collection,
                                 const char *collection_inner,
                                 int is_group_field,
                                 const char *length_field) {
    schema_field_section_t section;

    if (field_type == NULL) {
        grammar_oom(ctx);
        return 0;
    }
    if (!validate_type_name_supported(ctx, field_type)) {
        return 0;
    }
    if (collection_inner != NULL && collection_inner[0] != '\0' &&
        strchr(collection_inner, ',') == NULL &&
        !validate_type_name_supported(ctx, collection_inner)) {
        return 0;
    }
    if (!validate_fixed_length(ctx, field_name, length_field)) {
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
        if (section < ctx->cur_field_section) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "Invalid TBE field order for field '%s' of type '%s': fixed, then group, then variable data",
                     field_name ? field_name : "<unnamed>", field_type);
            ctx->error = 1;
            return 0;
        }
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
    const cmeta_data_desc *builtin_data = builtin_type != NULL ? builtin_type->data : NULL;
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
    } else if (builtin_data != NULL &&
               (builtin_data->kind == CMETA_DATA_SINT ||
                builtin_data->kind == CMETA_DATA_UINT ||
                builtin_data->kind == CMETA_DATA_FLOAT)) {
        size = (int)builtin_type->size;
        is_numeric = 1;
        is_unsigned = builtin_data->kind == CMETA_DATA_UINT;
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
        add_string(ctx, field_map, "host_type", "salts_uuid_t");
        add_true(ctx, field_map, "is_uuid");
        add_true(ctx, field_map, "is_primitive");
        add_true(ctx, field_map, "is_fixed_size");
    }

    if (!is_group_field && builtin_data != NULL &&
        (builtin_data->kind == CMETA_DATA_SINT || builtin_data->kind == CMETA_DATA_UINT)) {
        add_true(ctx, field_map, "is_integer");
    } else if (!is_group_field && builtin_data != NULL &&
               builtin_data->kind == CMETA_DATA_FLOAT) {
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
                add_string_slice(ctx, field_map, "key_type", collection_inner, key_len);
                add_string(ctx, field_map, "value_type", map_value_type + 1);
                add_string(ctx, field_map, "inner_type", map_value_type + 1);
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

static int validation_token_equals(schema_token_t token, const char *text) {
    size_t length = text != NULL ? strlen(text) : 0u;
    return token.value != NULL && token.length == length &&
           memcmp(token.value, text, length) == 0;
}

static Node *validation_constraint_node(schema_parse_ctx_t *ctx,
                                        const char *kind) {
    Node *node = create_node_map(NULL);
    if (node == NULL) {
        grammar_oom(ctx);
        return NULL;
    }
    add_string(ctx, node, "kind", kind);
    if (ctx->error) {
        node_free(node);
        return NULL;
    }
    return node;
}

static Node *create_validation_numeric_constraint(
    schema_parse_ctx_t *ctx, schema_token_t annotation,
    schema_token_t value_token) {
    Node *node = NULL;
    char *value = tok_strdup(value_token);
    const char *kind = NULL;

    if (validation_token_equals(annotation, "Min"))
        kind = "min";
    else if (validation_token_equals(annotation, "Max"))
        kind = "max";
    else {
        char *name = tok_strdup(annotation);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Validation annotation @%s does not accept one numeric argument",
                 name != NULL ? name : "<invalid>");
        free(name);
        free(value);
        ctx->error = 1;
        return NULL;
    }

    if (value == NULL) {
        grammar_oom(ctx);
        return NULL;
    }
    node = validation_constraint_node(ctx, kind);
    if (node != NULL)
        add_string(ctx, node, "value", value);
    free(value);
    if (ctx->error) {
        node_free(node);
        return NULL;
    }
    return node;
}

static Node *create_validation_pattern_constraint(
    schema_parse_ctx_t *ctx, schema_token_t annotation,
    schema_token_t pattern_token) {
    Node *node = NULL;
    char *pattern = tok_strdup(pattern_token);

    if (!validation_token_equals(annotation, "Pattern")) {
        char *name = tok_strdup(annotation);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Validation annotation @%s does not accept a string argument",
                 name != NULL ? name : "<invalid>");
        free(name);
        free(pattern);
        ctx->error = 1;
        return NULL;
    }

    if (pattern == NULL) {
        grammar_oom(ctx);
        return NULL;
    }
    node = validation_constraint_node(ctx, "pattern");
    if (node != NULL)
        add_string(ctx, node, "pattern", pattern);
    free(pattern);
    if (ctx->error) {
        node_free(node);
        return NULL;
    }
    return node;
}

static int validation_size_arg(
    schema_parse_ctx_t *ctx, Node *node,
    schema_token_t name_token, schema_token_t value_token,
    size_t *out_value) {
    char *name = tok_strdup(name_token);
    char *value = tok_strdup(value_token);
    const char *key = NULL;
    size_t parsed = 0u;

    if (name == NULL || value == NULL) {
        free(name);
        free(value);
        grammar_oom(ctx);
        return 0;
    }
    if (strcmp(name, "min") == 0)
        key = "min";
    else if (strcmp(name, "max") == 0)
        key = "max";
    else {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "@Size accepts only min/max arguments, got '%s'", name);
        ctx->error = 1;
    }

    if (!ctx->error &&
        (!is_numeric_literal(value) ||
         !schema_parse_fixed_layout_size(value, &parsed))) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "@Size %s must be a bounded non-negative decimal integer",
                 key != NULL ? key : "argument");
        ctx->error = 1;
    }

    if (!ctx->error && map_get_string_value(node, key) != NULL) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "@Size repeats argument '%s'", key);
        ctx->error = 1;
    }

    if (!ctx->error) {
        add_string(ctx, node, key, value);
        if (out_value != NULL) *out_value = parsed;
    }

    free(name);
    free(value);
    return !ctx->error;
}

static Node *create_validation_size_constraint(
    schema_parse_ctx_t *ctx, schema_token_t annotation,
    schema_token_t name1, schema_token_t value1,
    int has_second, schema_token_t name2, schema_token_t value2) {
    Node *node;
    size_t first_value = 0u;
    size_t second_value = 0u;

    if (!validation_token_equals(annotation, "Size")) {
        char *name = tok_strdup(annotation);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Validation annotation @%s does not accept named size arguments",
                 name != NULL ? name : "<invalid>");
        free(name);
        ctx->error = 1;
        return NULL;
    }

    node = validation_constraint_node(ctx, "size");
    if (node == NULL) return NULL;
    if (!validation_size_arg(ctx, node, name1, value1, &first_value) ||
        (has_second &&
         !validation_size_arg(ctx, node, name2, value2, &second_value))) {
        node_free(node);
        return NULL;
    }

    {
        const char *minimum = map_get_string_value(node, "min");
        const char *maximum = map_get_string_value(node, "max");
        if (minimum != NULL && maximum != NULL) {
            size_t min_value = 0u;
            size_t max_value = 0u;
            if (!schema_parse_fixed_layout_size(minimum, &min_value) ||
                !schema_parse_fixed_layout_size(maximum, &max_value) ||
                min_value > max_value) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                         "@Size requires min <= max");
                ctx->error = 1;
                node_free(node);
                return NULL;
            }
        }
    }
    (void)first_value;
    (void)second_value;
    return node;
}

static Node *validation_constraints_append(schema_parse_ctx_t *ctx,
                                           Node *list, Node *constraint) {
    const char *kind;
    size_t i;

    if (constraint == NULL || ctx->error) {
        node_free(constraint);
        return list;
    }
    if (list == NULL) {
        list = create_node_list("constraints");
        if (list == NULL) {
            node_free(constraint);
            grammar_oom(ctx);
            return NULL;
        }
    }

    kind = map_get_string_value(constraint, "kind");
    for (i = 0u; i < list->data.list.count; ++i) {
        const char *existing =
            map_get_string_value(list->data.list.items[i], "kind");
        if (kind != NULL && existing != NULL && strcmp(kind, existing) == 0) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "Duplicate validation constraint '%s'", kind);
            ctx->error = 1;
            node_free(constraint);
            return list;
        }
    }

    if (list_add(list, constraint) != 0) {
        node_free(constraint);
        grammar_oom(ctx);
    }
    return list;
}

static void add_field(schema_parse_ctx_t *ctx,
                      const char *type_str, const char *name_str,
                      int is_collection, const char *inner, const char *len_field,
                      Node *attrs, int is_group_field, int is_optional, int is_nullable,
                      const char *default_value) {
    Node *field_map;

    if (ctx->error) {
        node_free(attrs);
        return;
    }
    if (!validate_field_layout(ctx, type_str, name_str, is_collection, inner,
                               is_group_field, len_field)) {
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

    if (is_nullable) {
        add_true(ctx, field_map, "is_nullable");
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

static Node *create_attribute_value_node(schema_parse_ctx_t *ctx, schema_token_t value_tok) {
    char *value = tok_strdup(value_tok);
    Node *node;

    if (value == NULL) {
        grammar_oom(ctx);
        return NULL;
    }
    node = create_node_string(NULL, value);
    free(value);
    if (node == NULL) grammar_oom(ctx);
    return node;
}

static Node *create_attribute_node(schema_parse_ctx_t *ctx, schema_token_t key_tok,
                                   Node *values) {
    char *key = tok_strdup(key_tok);
    Node *attr = NULL;
    const char *value = NULL;

    if (ctx->error || key == NULL || values == NULL || values->type != NODE_LIST ||
        values->data.list.count == 0 || values->data.list.items[0] == NULL ||
        values->data.list.items[0]->type != NODE_STRING) {
        free(key);
        node_free(values);
        grammar_oom(ctx);
        return NULL;
    }
    value = values->data.list.items[0]->data.string_val;
    attr = create_node_map(key);

    if (attr == NULL) {
        grammar_oom(ctx);
    } else {
        add_string(ctx, attr, "name", key);
        add_string(ctx, attr, "value", value);
        if (!ctx->error && map_add(attr, values) == 0) {
            values = NULL;
        } else {
            grammar_oom(ctx);
        }
    }

    free(key);
    node_free(values);
    if (ctx->error) {
        node_free(attr);
        attr = NULL;
    }
    return attr;
}

static Node *create_bare_attribute_node(schema_parse_ctx_t *ctx,
                                        schema_token_t key_tok) {
    char *key = tok_strdup(key_tok);
    Node *attr = NULL;
    Node *values = NULL;
    Node *marker = NULL;

    if (ctx->error || key == NULL) {
        free(key);
        grammar_oom(ctx);
        return NULL;
    }

    attr = create_node_map(key);
    values = create_node_list("values");
    marker = create_node_string(NULL, "1");
    if (attr == NULL || values == NULL || marker == NULL) {
        node_free(attr);
        node_free(values);
        node_free(marker);
        free(key);
        grammar_oom(ctx);
        return NULL;
    }

    add_string(ctx, attr, "name", key);
    add_string(ctx, attr, "value", "1");
    add_true(ctx, attr, "bare");
    if (ctx->error) {
        node_free(attr);
        node_free(values);
        node_free(marker);
        free(key);
        return NULL;
    }
    if (list_add(values, marker) != 0) {
        node_free(marker);
        node_free(values);
        node_free(attr);
        free(key);
        grammar_oom(ctx);
        return NULL;
    }
    marker = NULL;
    if (map_add(attr, values) != 0) {
        node_free(values);
        node_free(attr);
        free(key);
        grammar_oom(ctx);
        return NULL;
    }
    values = NULL;

    free(key);
    return attr;
}

static void begin_service(schema_parse_ctx_t *ctx, const char *name) {
    Node *service;
    Node *operations;
    Node *operations_view;

    ctx->cur_service = NULL;
    ctx->cur_operations = NULL;
    if (ctx->error || name == NULL) return;

    service = create_node_map(NULL);
    operations = create_node_list("operations");
    if (service == NULL || operations == NULL) {
        node_free(service);
        node_free(operations);
        grammar_oom(ctx);
        return;
    }

    add_name_nodes(ctx, service, "service_name", name);
    if (ctx->error) {
        node_free(operations);
        node_free(service);
        return;
    }
    operations_view = operations;
    if (map_add(service, operations) != 0) {
        node_free(operations);
        node_free(service);
        grammar_oom(ctx);
        return;
    }
    operations = NULL;
    if (list_add(ctx->services_list, service) != 0) {
        node_free(service);
        grammar_oom(ctx);
        return;
    }

    ctx->cur_service = service;
    ctx->cur_operations = operations_view;
}

static void begin_component(schema_parse_ctx_t *ctx, const char *name) {
    Node *component;
    Node *capabilities;
    Node *capabilities_view;

    ctx->cur_component = NULL;
    ctx->cur_component_capabilities = NULL;
    if (ctx->error || name == NULL) return;

    component = create_node_map(NULL);
    capabilities = create_node_list("capabilities");
    if (component == NULL || capabilities == NULL) {
        node_free(component);
        node_free(capabilities);
        grammar_oom(ctx);
        return;
    }

    add_name_nodes(ctx, component, "component_name", name);
    if (ctx->error) {
        node_free(capabilities);
        node_free(component);
        return;
    }

    capabilities_view = capabilities;
    if (map_add(component, capabilities) != 0) {
        node_free(capabilities);
        node_free(component);
        grammar_oom(ctx);
        return;
    }
    capabilities = NULL;

    if (list_add(ctx->components_list, component) != 0) {
        node_free(component);
        grammar_oom(ctx);
        return;
    }

    ctx->cur_component = component;
    ctx->cur_component_capabilities = capabilities_view;
}

static void add_component_capability_ref(
    schema_parse_ctx_t *ctx, const char *kind, const char *name) {
    Node *capability;

    if (ctx->error || ctx->cur_component == NULL ||
        ctx->cur_component_capabilities == NULL ||
        kind == NULL || name == NULL) {
        if (!ctx->error) grammar_oom(ctx);
        return;
    }

    capability = create_node_map(NULL);
    if (capability == NULL) {
        grammar_oom(ctx);
        return;
    }

    add_string(ctx, capability, "kind", kind);
    add_string(ctx, capability, "name", name);
    if (ctx->error ||
        list_add(ctx->cur_component_capabilities, capability) != 0) {
        node_free(capability);
        if (!ctx->error) grammar_oom(ctx);
    }
}

static void add_channel_contract(
    schema_parse_ctx_t *ctx,
    const char *name,
    const char *message_type) {
    Node *channel;

    if (ctx->error || ctx->channels_list == NULL ||
        name == NULL || message_type == NULL) {
        if (!ctx->error) grammar_oom(ctx);
        return;
    }

    channel = create_node_map(NULL);
    if (channel == NULL) {
        grammar_oom(ctx);
        return;
    }

    add_name_nodes(ctx, channel, "channel_name", name);
    add_string(ctx, channel, "message_type", message_type);
    if (ctx->error || list_add(ctx->channels_list, channel) != 0) {
        node_free(channel);
        if (!ctx->error) grammar_oom(ctx);
    }
}

static Node *create_error_type_list(schema_parse_ctx_t *ctx,
                                    schema_token_t type_tok) {
    char *type_name = tok_strdup(type_tok);
    Node *errors = create_node_list("errors");
    Node *item = NULL;

    if (type_name != NULL)
        item = create_node_string(NULL, type_name);
    free(type_name);

    if (errors == NULL || item == NULL || list_add(errors, item) != 0) {
        node_free(errors);
        node_free(item);
        grammar_oom(ctx);
        return NULL;
    }
    return errors;
}

static void append_error_type(schema_parse_ctx_t *ctx, Node *errors,
                              schema_token_t type_tok) {
    char *type_name;
    Node *item;

    if (ctx->error || errors == NULL) return;
    type_name = tok_strdup(type_tok);
    item = type_name != NULL ? create_node_string(NULL, type_name) : NULL;
    free(type_name);
    if (item == NULL || list_add(errors, item) != 0) {
        node_free(item);
        grammar_oom(ctx);
    }
}

static void add_service_operation(schema_parse_ctx_t *ctx,
                                  const char *name,
                                  const char *request_type,
                                  const char *response_type,
                                  Node *attrs,
                                  Node *errors) {
    Node *operation;

    if (ctx->error || ctx->cur_service == NULL ||
        ctx->cur_operations == NULL || name == NULL ||
        request_type == NULL || response_type == NULL) {
        node_free(attrs);
        node_free(errors);
        if (!ctx->error) grammar_oom(ctx);
        return;
    }

    operation = create_node_map(NULL);
    if (operation == NULL) {
        node_free(attrs);
        node_free(errors);
        grammar_oom(ctx);
        return;
    }

    add_string(ctx, operation, "name", name);
    add_string(ctx, operation, "operation_name", name);
    add_string(ctx, operation, "service_name",
               map_get_string_value(ctx->cur_service, "name"));
    add_string(ctx, operation, "request_type", request_type);
    add_string(ctx, operation, "response_type", response_type);

    if (errors == NULL)
        errors = create_node_list("errors");
    if (errors == NULL || ctx->error) {
        node_free(operation);
        node_free(attrs);
        node_free(errors);
        if (!ctx->error) grammar_oom(ctx);
        return;
    }
    if (attrs != NULL) {
        if (map_add(operation, attrs) != 0) {
            node_free(attrs);
            node_free(errors);
            node_free(operation);
            grammar_oom(ctx);
            return;
        }
        attrs = NULL;
    }
    if (map_add(operation, errors) != 0) {
        node_free(errors);
        node_free(operation);
        grammar_oom(ctx);
        return;
    }
    errors = NULL;
    if (list_add(ctx->cur_operations, operation) != 0) {
        node_free(operation);
        grammar_oom(ctx);
        return;
    }
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
%type attr_values {Node *}
%type attr_value {Node *}
%type field_default {char *}
%type field_presence {int}
%type field_nullability {int}
%type service_errors {Node *}
%type error_types {Node *}
%type idl_ident {schema_token_t}
%destructor attribute_list { (void)ctx; node_free($$); }
%destructor attr_items { (void)ctx; node_free($$); }
%destructor field_default { (void)ctx; free($$); }
%destructor attr_item { (void)ctx; node_free($$); }
%destructor attr_values { (void)ctx; node_free($$); }
%destructor attr_value { (void)ctx; node_free($$); }
%destructor service_errors { (void)ctx; node_free($$); }
%destructor error_types { (void)ctx; node_free($$); }

%token ENUM FLAGS NUMBER DEFAULT_NUMBER EQUALS IDENT LBRACE RBRACE SEMI LPAREN RPAREN LBRACKET RBRACKET LT GT COMMA MESSAGE COMPOSITE GROUP SCHEMA REQUIRED OPTIONAL NULLABLE DEFAULT STRING TRUE FALSE UNION SERVICE COMPONENT CHANNEL THROWS COLON ARROW.

start ::= schema.
schema ::= decl_list.

idl_ident(A) ::= IDENT(B). { A = B; }
idl_ident(A) ::= SERVICE(B). { A = B; }
idl_ident(A) ::= COMPONENT(B). { A = B; }
idl_ident(A) ::= CHANNEL(B). { A = B; }
idl_ident(A) ::= THROWS(B). { A = B; }

decl_list ::= decl_list decl.
decl_list ::= .

decl ::= enum_decl.
decl ::= flags_decl.
decl ::= message_decl.
decl ::= composite_decl.
decl ::= group_decl.
decl ::= schema_decl.
decl ::= union_decl.
decl ::= service_decl.
decl ::= channel_decl.
decl ::= component_decl.

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

attr_item(A) ::= idl_ident(K) LPAREN attr_values(V) RPAREN. {
    A = create_attribute_node(ctx, K, V);
}
attr_item(A) ::= idl_ident(K). {
    A = create_bare_attribute_node(ctx, K);
}

attr_values(A) ::= attr_values(B) COMMA attr_value(C). {
    A = B;
    if (list_add(A, C) != 0) {
        node_free(C);
        grammar_oom(ctx);
    }
}
attr_values(A) ::= attr_value(B). {
    A = create_node_list("values");
    if (A == NULL || list_add(A, B) != 0) {
        node_free(A);
        A = NULL;
        node_free(B);
        grammar_oom(ctx);
    }
}

attr_value(A) ::= idl_ident(V). { A = create_attribute_value_node(ctx, V); }
attr_value(A) ::= NUMBER(V). { A = create_attribute_value_node(ctx, V); }
attr_value(A) ::= STRING(V). { A = create_attribute_value_node(ctx, V); }

schema_decl ::= SCHEMA idl_ident(N) attribute_list(A) SEMI. {
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

service_decl ::= service_header service_body RBRACE. {
    ctx->cur_service = NULL;
    ctx->cur_operations = NULL;
}

service_header ::= SERVICE idl_ident(N) LBRACE. {
    char *service_name = tok_strdup(N);
    begin_service(ctx, service_name);
    free(service_name);
}

service_body ::= service_body service_operation.
service_body ::= .

service_operation ::= attribute_list(A) idl_ident(N) COLON idl_ident(I) ARROW idl_ident(O) service_errors(E) SEMI. {
    char *operation_name = tok_strdup(N);
    char *request_type = tok_strdup(I);
    char *response_type = tok_strdup(O);
    add_service_operation(ctx, operation_name, request_type, response_type, A, E);
    free(operation_name);
    free(request_type);
    free(response_type);
}

service_errors(A) ::= THROWS error_types(B). { A = B; }
service_errors(A) ::= . { A = NULL; }

channel_decl ::= CHANNEL idl_ident(N) COLON idl_ident(T) SEMI. {
    char *channel_name = tok_strdup(N);
    char *message_type = tok_strdup(T);
    add_channel_contract(ctx, channel_name, message_type);
    free(channel_name);
    free(message_type);
}


component_decl ::= component_header component_body RBRACE. {
    ctx->cur_component = NULL;
    ctx->cur_component_capabilities = NULL;
}

component_header ::= COMPONENT idl_ident(N) LBRACE. {
    char *component_name = tok_strdup(N);
    begin_component(ctx, component_name);
    free(component_name);
}

component_body ::= component_body component_capability.
component_body ::= .

component_capability ::= SERVICE idl_ident(N) SEMI. {
    char *service_name = tok_strdup(N);
    add_component_capability_ref(ctx, "service", service_name);
    free(service_name);
}
component_capability ::= CHANNEL idl_ident(N) SEMI. {
    char *channel_name = tok_strdup(N);
    add_component_capability_ref(ctx, "channel", channel_name);
    free(channel_name);
}


error_types(A) ::= error_types(B) COMMA idl_ident(T). {
    A = B;
    append_error_type(ctx, A, T);
}
error_types(A) ::= idl_ident(T). {
    A = create_error_type_list(ctx, T);
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

enum_header ::= ENUM idl_ident(N) LBRACE. {
    char *enum_name = tok_strdup(N);
    begin_enum_like(ctx, enum_name, NULL, 0);
    free(enum_name);
}

enum_header ::= ENUM idl_ident(N) LT idl_ident(T) GT LBRACE. {
    char *enum_name = tok_strdup(N);
    char *underlying_type = tok_strdup(T);
    begin_enum_like(ctx, enum_name, underlying_type, 0);
    free(enum_name);
    free(underlying_type);
}

enum_body ::= enum_body enum_item.
enum_body ::= .

enum_literal(A) ::= NUMBER(N). { A = N; }
enum_literal(A) ::= DEFAULT_NUMBER(N). { A = N; }

enum_item ::= idl_ident(K) EQUALS enum_literal(V) SEMI. {
    char *key = tok_strdup(K);
    char *value = tok_strdup(V);
    add_enum_item(ctx, key, value);
    free(key);
    free(value);
}

enum_item ::= idl_ident(K) SEMI. {
    char *key = tok_strdup(K);
    /* Resolve omitted values only after the storage domain is known. */
    add_enum_item(ctx, key, "");
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

flags_header ::= FLAGS idl_ident(N) LBRACE. {
    char *flags_name = tok_strdup(N);
    begin_enum_like(ctx, flags_name, NULL, 1);
    free(flags_name);
}

flags_header ::= FLAGS idl_ident(N) LT idl_ident(T) GT LBRACE. {
    char *flags_name = tok_strdup(N);
    char *underlying_type = tok_strdup(T);
    begin_enum_like(ctx, flags_name, underlying_type, 1);
    free(flags_name);
    free(underlying_type);
}

flags_body ::= flags_body flags_item.
flags_body ::= .

flags_item ::= idl_ident(K) EQUALS enum_literal(V) SEMI. {
    char *key = tok_strdup(K);
    char *value = tok_strdup(V);
    add_enum_item(ctx, key, value);
    free(key);
    free(value);
}

flags_item ::= idl_ident(K) SEMI. {
    char *key = tok_strdup(K);
    add_enum_item(ctx, key, "");
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

union_header ::= UNION idl_ident(N) LBRACE. {
    char *record_name = tok_strdup(N);
    begin_record(ctx, ctx->unions_list, SCHEMA_RECORD_UNION, "union_name", record_name);
    free(record_name);
}

union_body ::= union_body union_variant.
union_body ::= .

union_variant ::= attribute_list(A) idl_ident(T) idl_ident(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *field_name = tok_strdup(N);
    add_field(ctx, type_name, field_name, 0, "", "", A, 0, 0, 0, NULL);
    free(type_name);
    free(field_name);
}

composite_header ::= COMPOSITE idl_ident(N) LBRACE. {
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

group_header ::= GROUP idl_ident(N) LBRACE. {
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

message_header ::= MESSAGE idl_ident(N) LBRACE. {
    char *record_name = tok_strdup(N);
    begin_record(ctx, ctx->messages_list, SCHEMA_RECORD_MESSAGE, "message_name",
                 record_name);
    free(record_name);
}

field_list ::= field_list field_decl.
field_list ::= .

field_decl ::= field_presence(P) field_nullability(Z) attribute_list(A) idl_ident(T) idl_ident(N) field_default(D) SEMI. {
    char *type_name = tok_strdup(T);
    char *field_name = tok_strdup(N);
    int is_optional = P != 0;
    int is_nullable = Z != 0;
    add_field(ctx, type_name, field_name, 0, "", "", A, 0, is_optional, is_nullable, D);
    free(type_name);
    free(field_name);
    if (D) free(D);
}

field_default(D) ::= DEFAULT NUMBER(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT DEFAULT_NUMBER(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT STRING(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT TRUE(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT FALSE(V). { D = tok_strdup(V); }
field_default(D) ::= DEFAULT idl_ident(V). { D = tok_strdup(V); }
field_default(D) ::= . { D = NULL; }

field_decl ::= field_presence(P) field_nullability(Z) attribute_list(A) idl_ident(T) LPAREN idl_ident(L) RPAREN idl_ident(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *length_field = tok_strdup(L);
    char *field_name = tok_strdup(N);
    int is_optional = P != 0;
    int is_nullable = Z != 0;
    add_field(ctx, type_name, field_name, 0, "", length_field, A, 0, is_optional, is_nullable, NULL);
    free(type_name);
    free(length_field);
    free(field_name);
}

field_decl ::= field_presence(P) field_nullability(Z) attribute_list(A) idl_ident(T) LPAREN NUMBER(L) RPAREN idl_ident(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *length_field = tok_strdup(L);
    char *field_name = tok_strdup(N);
    int is_optional = P != 0;
    int is_nullable = Z != 0;
    add_field(ctx, type_name, field_name, 0, "", length_field, A, 0, is_optional, is_nullable, NULL);
    free(type_name);
    free(length_field);
    free(field_name);
}

field_decl ::= field_presence(P) field_nullability(Z) attribute_list(A) GROUP LT idl_ident(I) GT idl_ident(N) SEMI. {
    char *group_type = tok_strdup(I);
    char *field_name = tok_strdup(N);
    int is_optional = P != 0;
    int is_nullable = Z != 0;
    add_field(ctx, "group", field_name, 0, group_type, "", A, 1, is_optional, is_nullable, NULL);
    free(group_type);
    free(field_name);
}

field_decl ::= field_presence(P) field_nullability(Z) attribute_list(A) idl_ident(T) LBRACKET idl_ident(L) RBRACKET idl_ident(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *length_field = tok_strdup(L);
    char *field_name = tok_strdup(N);
    int is_optional = P != 0;
    int is_nullable = Z != 0;
    add_field(ctx, "array", field_name, 1, type_name, length_field, A, 0, is_optional, is_nullable, NULL);
    free(type_name);
    free(length_field);
    free(field_name);
}

field_decl ::= field_presence(P) field_nullability(Z) attribute_list(A) idl_ident(T) LBRACKET NUMBER(L) RBRACKET idl_ident(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *length_field = tok_strdup(L);
    char *field_name = tok_strdup(N);
    int is_optional = P != 0;
    int is_nullable = Z != 0;
    add_field(ctx, "array", field_name, 1, type_name, length_field, A, 0, is_optional, is_nullable, NULL);
    free(type_name);
    free(length_field);
    free(field_name);
}

field_decl ::= field_presence(P) field_nullability(Z) attribute_list(A) idl_ident(T) LT idl_ident(I) GT idl_ident(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *inner_type = tok_strdup(I);
    char *field_name = tok_strdup(N);
    int is_optional = P != 0;
    int is_nullable = Z != 0;
    add_field(ctx, type_name, field_name, 1, inner_type, "", A, 0, is_optional, is_nullable, NULL);
    free(type_name);
    free(inner_type);
    free(field_name);
}

field_decl ::= field_presence(P) field_nullability(Z) attribute_list(A) idl_ident(T) LT idl_ident(K) COMMA idl_ident(V) GT idl_ident(N) SEMI. {
    char *type_name = tok_strdup(T);
    char *field_name = tok_strdup(N);
    char *key_type = tok_strdup(K);
    char *value_type = tok_strdup(V);
    char *map_inner = NULL;
    int is_optional = P != 0;
    int is_nullable = Z != 0;

    if (key_type == NULL || value_type == NULL) {
        grammar_oom(ctx);
        node_free(A);
    } else if (!validate_type_name_supported(ctx, key_type) ||
               !validate_type_name_supported(ctx, value_type)) {
        node_free(A);
    } else {
        map_inner = join_map_inner_types(ctx, key_type, value_type);
        if (map_inner != NULL) {
            add_field(ctx, type_name, field_name, 1, map_inner, "", A, 0, is_optional, is_nullable, NULL);
        } else {
            node_free(A);
        }
    }
    free(map_inner);
    free(type_name);
    free(field_name);
    free(key_type);
    free(value_type);
}

/*
 * Presence and nullability are orthogonal DataBind semantics with one
 * unambiguous surface order:
 *
 *   [required|optional] [nullable] <type> <name>
 *
 * Both dimensions default independently: required + non-null.
 * NULLABLE is intentionally a reserved keyword rather than an idl_ident so
 * Lemon never has to guess whether it starts a modifier or names a type.
 */
field_presence(P) ::= REQUIRED. { P = 0; }
field_presence(P) ::= OPTIONAL. { P = 1; }
field_presence(P) ::= .         { P = 0; }

field_nullability(Z) ::= NULLABLE. { Z = 1; }
field_nullability(Z) ::= .         { Z = 0; }

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
