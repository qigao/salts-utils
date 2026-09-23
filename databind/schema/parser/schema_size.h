#ifndef TBE_SCHEMA_SIZE_H
#define TBE_SCHEMA_SIZE_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static inline int schema_parse_fixed_layout_size(const char *text, size_t *out) {
    char *end = NULL;
    unsigned long long value;
    const char *p;

    if (text == NULL || out == NULL || text[0] == '\0') {
        return 0;
    }

    for (p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }

    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == NULL || end == text || *end != '\0') {
        return 0;
    }
    if (value > (unsigned long long)SIZE_MAX ||
        value > (unsigned long long)PTRDIFF_MAX) {
        return 0;
    }

    *out = (size_t)value;
    return 1;
}

#endif /* TBE_SCHEMA_SIZE_H */
