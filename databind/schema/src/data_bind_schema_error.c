#include "data_bind_schema_error.h"

#include <string.h>

enum { DATA_BIND_SCHEMA_ERROR_MESSAGE_MAX = 255 };

const char *data_bind_schema_error_string(DataBindSchemaErrorCode code) {
    switch (code) {
    case DATA_BIND_SCHEMA_OK:
        return "Success";
    case DATA_BIND_SCHEMA_ERR_INVALID_ARGUMENT:
        return "Invalid argument";
    case DATA_BIND_SCHEMA_ERR_OUT_OF_MEMORY:
        return "Out of memory";
    case DATA_BIND_SCHEMA_ERR_LEXER:
        return "Lexer error";
    case DATA_BIND_SCHEMA_ERR_SYNTAX:
        return "Syntax error";
    case DATA_BIND_SCHEMA_ERR_SEMANTIC:
        return "Semantic error";
    case DATA_BIND_SCHEMA_ERR_IO:
        return "I/O error";
    default:
        return "Unknown error";
    }
}

void data_bind_schema_error_init(DataBindSchemaError *error) {
    if (error == NULL) return;
    error->code = DATA_BIND_SCHEMA_OK;
    error->line = -1;
    error->column = -1;
    error->message[0] = '\0';
}

void data_bind_schema_error_set(
    DataBindSchemaError *error,
    DataBindSchemaErrorCode code,
    int line,
    int column,
    const char *message) {
    const char *text;
    size_t length;

    if (error == NULL) return;

    error->code = code;
    error->line = line;
    error->column = column;

    text = message != NULL && message[0] != '\0'
               ? message
               : data_bind_schema_error_string(code);
    length = strlen(text);
    if (length > DATA_BIND_SCHEMA_ERROR_MESSAGE_MAX)
        length = DATA_BIND_SCHEMA_ERROR_MESSAGE_MAX;
    memcpy(error->message, text, length);
    error->message[length] = '\0';
}
