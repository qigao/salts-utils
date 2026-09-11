#include "schema_enum.h"
#include "schema_builtin_type.h"

#include <fmt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { ENUM_DECIMAL_CAPACITY = 32, ENUM_LITERAL_CAPACITY = 80, ENUM_BITS_PER_BYTE = 8 };

typedef struct enum_number {
    uint64_t magnitude;
    int negative;
} enum_number_t;

typedef struct enum_entry {
    enum_number_t value;
    const char *name;
} enum_entry_t;

static Node *enum_child(Node *map, const char *name) {
    if (!map || map->type != NODE_MAP) return NULL;
    for (size_t i = 0; i < map->data.map.count; ++i) {
        Node *child = map->data.map.items[i];
        if (child->name && strcmp(child->name, name) == 0) return child;
    }
    return NULL;
}

static const char *enum_string(Node *map, const char *name) {
    Node *child = enum_child(map, name);
    return child && child->type == NODE_STRING ? child->data.string_val : NULL;
}

static int enum_set_string(Node *map, const char *name, const char *value) {
    Node *old = enum_child(map, name);
    Node *replacement = create_node_string(name, value);
    if (!replacement) return -1;
    if (old) {
        if (old->type != NODE_STRING) { node_free(replacement); return -1; }
        free(old->data.string_val);
        old->data.string_val = replacement->data.string_val;
        replacement->data.string_val = NULL;
        node_free(replacement);
    } else if (map_add(map, replacement) != 0) {
        node_free(replacement);
        return -1;
    }
    return 0;
}

static int enum_fail(tbe_error_t *error, tbe_error_code_t code,
                     const char *name, const char *reason) {
    char message[sizeof(((tbe_error_t *)0)->message)];
    fmt(message, sizeof(message), "enum/flags '{}': {}", name ? name : "<unnamed>", reason);
    tbe_error_set(error, code, -1, -1, message);
    return -1;
}

/* Decimal is never octal. Sign and magnitude preserve the whole uint64 domain
 * without passing a wide unsigned literal through a signed intermediate. */
static int enum_parse_number(const char *text, enum_number_t *number) {
    unsigned base = 10u;
    uint64_t value = 0;
    number->negative = 0;
    if (!text || !text[0]) return 0;
    if (*text == '-' || *text == '+') {
        number->negative = *text == '-';
        ++text;
    }
    if (text[0] == '0' && text[1] == 'x') { text += 2; base = 16u; }
    if (!*text) return 0;
    for (; *text; ++text) {
        unsigned digit;
        if (*text >= '0' && *text <= '9') digit = (unsigned)(*text - '0');
        else if (*text >= 'a' && *text <= 'f') digit = (unsigned)(*text - 'a') + 10u;
        else if (*text >= 'A' && *text <= 'F') digit = (unsigned)(*text - 'A') + 10u;
        else return 0;
        if (digit >= base || value > (UINT64_MAX - digit) / base) return 0;
        value = value * base + digit;
    }
    number->magnitude = value;
    return 1;
}

static int enum_next_number(enum_number_t previous, int is_flags, enum_number_t *next) {
    *next = previous;
    if (is_flags) {
        if (previous.magnitude > UINT64_MAX / 2u) return 0;
        next->magnitude *= 2u;
    } else if (previous.negative) {
        --next->magnitude;
        if (next->magnitude == 0) next->negative = 0;
    } else {
        if (previous.magnitude == UINT64_MAX) return 0;
        ++next->magnitude;
    }
    return 1;
}

static int enum_number_fits(enum_number_t value, const schema_builtin_type_info_t *type) {
    unsigned bits = (unsigned)type->size * ENUM_BITS_PER_BYTE;
    if (type->is_unsigned) {
        uint64_t limit = bits == 64u ? UINT64_MAX : (UINT64_C(1) << bits) - 1u;
        return !value.negative && value.magnitude <= limit;
    }
    uint64_t sign = UINT64_C(1) << (bits - 1u);
    return value.magnitude <= (value.negative ? sign : sign - 1u);
}

static int enum_compare_value(const void *left, const void *right) {
    const enum_number_t a = ((const enum_entry_t *)left)->value;
    const enum_number_t b = ((const enum_entry_t *)right)->value;
    if (a.negative != b.negative) return a.negative ? -1 : 1;
    int order = (a.magnitude > b.magnitude) - (a.magnitude < b.magnitude);
    return a.negative ? -order : order;
}

static int enum_compare_name(const void *left, const void *right) {
    return strcmp(((const enum_entry_t *)left)->name, ((const enum_entry_t *)right)->name);
}

static int enum_set_constant(Node *owner, const char *key,
                             const char *enum_name, const char *item_name) {
    size_t a = strlen(enum_name), b = strlen(item_name);
    if (a > SIZE_MAX - 2u || b > SIZE_MAX - a - 2u) return -1;
    char *constant = (char *)malloc(a + b + 2u);
    if (!constant) return -1;
    memcpy(constant, enum_name, a);
    constant[a] = '_';
    memcpy(constant + a + 1u, item_name, b + 1u);
    int result = enum_set_string(owner, key, constant);
    free(constant);
    return result;
}

static int enum_normalize_item(Node *item, enum_number_t number) {
    char decimal[ENUM_DECIMAL_CAPACITY];
    char literal[ENUM_LITERAL_CAPACITY];
    fmt(decimal, sizeof(decimal), "{}{}", number.negative ? "-" : "", number.magnitude);
    if (!number.negative) {
        fmt(literal, sizeof(literal), "UINT64_C({})", number.magnitude);
    } else if (number.magnitude == (UINT64_C(1) << 63u)) {
        /* The positive magnitude of INT64_MIN is not a signed C constant. */
        fmt_text(literal, sizeof(literal), "(-INT64_C(9223372036854775807) - INT64_C(1))");
    } else {
        fmt(literal, sizeof(literal), "(-INT64_C({}))", number.magnitude);
    }
    return enum_set_string(item, "value", decimal) != 0 ||
           enum_set_string(item, "c_literal", literal) != 0 ? -1 : 0;
}

static int enum_validate_one(Node *owner, tbe_error_t *error) {
    const char *name = enum_string(owner, "enum_name");
    const int is_flags = enum_child(owner, "is_flags") != NULL;
    const char *declared = enum_string(owner, "underlying_type");
    const schema_builtin_type_info_t *type =
        schema_builtin_type_find(declared ? declared : (is_flags ? "uint32" : "int32"));
    Node *items = enum_child(owner, "items");
    enum_entry_t *entries = NULL;
    enum_number_t previous = {0, 0};
    int ordinal = 1;
    int status = -1;
    char canonical[ENUM_DECIMAL_CAPACITY];
    if (!type || !type->is_integer) {
        return enum_fail(error, TBE_ERR_SEMANTIC_ERROR, name,
                         "underlying type must be an 8/16/32/64-bit integer");
    }
    if (!name || !items || items->type != NODE_LIST || items->data.list.count == 0) {
        return enum_fail(error, TBE_ERR_SEMANTIC_ERROR, name, "at least one named value is required");
    }
    size_t count = items->data.list.count;
    if (count > SIZE_MAX / sizeof(*entries) || !(entries = malloc(count * sizeof(*entries))))
        goto oom;
    fmt(canonical, sizeof(canonical), "{}int{}", type->is_unsigned ? "u" : "",
        type->size * ENUM_BITS_PER_BYTE);
    if (enum_set_string(owner, "underlying_type", canonical) != 0) goto oom;
    for (size_t i = 0; i < count; ++i) {
        Node *item = items->data.list.items[i];
        const char *text = enum_string(item, "value");
        enum_number_t value = {is_flags ? 1u : 0u, 0};
        entries[i].name = enum_string(item, "name");
        if (!entries[i].name || !text ||
            (*text ? !enum_parse_number(text, &value)
                   : (i != 0 && !enum_next_number(previous, is_flags, &value))) ||
            !enum_number_fits(value, type)) {
            enum_fail(error, TBE_ERR_SEMANTIC_ERROR, name,
                      "integer literal or implicit value is outside its declared storage range");
            goto cleanup;
        }
        if (value.magnitude == 0) value.negative = 0;
        entries[i].value = previous = value;
        if (value.negative || value.magnitude != i) ordinal = 0;
        if (enum_normalize_item(item, value) != 0) goto oom;
    }
    /* Sorting a side array leaves the declaration order intact and avoids a
     * quadratic scan for each value in an untrusted schema. */
    qsort(entries, count, sizeof(*entries), enum_compare_value);
    for (size_t i = 1; i < count; ++i) {
        if (enum_compare_value(&entries[i - 1], &entries[i]) == 0) {
            enum_fail(error, TBE_ERR_SEMANTIC_ERROR, name, "duplicate numeric aliases are not allowed");
            goto cleanup;
        }
    }
    if (enum_set_constant(owner, "min_value", name, entries[0].name) != 0 ||
        enum_set_constant(owner, "max_value", name, entries[count - 1u].name) != 0) goto oom;
    qsort(entries, count, sizeof(*entries), enum_compare_name);
    for (size_t i = 1; i < count; ++i) {
        if (enum_compare_name(&entries[i - 1], &entries[i]) == 0) {
            enum_fail(error, TBE_ERR_SEMANTIC_ERROR, name, "duplicate member names are not allowed");
            goto cleanup;
        }
    }
    if (ordinal && enum_set_string(owner, "is_ordinal", "1") != 0) goto oom;
    status = 0;
    goto cleanup;
oom:
    enum_fail(error, TBE_ERR_OUT_OF_MEMORY, name, "cannot allocate canonical enum metadata");
cleanup:
    free(entries);
    return status;
}

int schema_validate_enums(Node *root, tbe_error_t *error) {
    Node *enums = enum_child(root, "enums");
    if (!enums || enums->type != NODE_LIST) return 0;
    for (size_t i = 0; i < enums->data.list.count; ++i)
        if (enum_validate_one(enums->data.list.items[i], error) != 0) return -1;
    return 0;
}
