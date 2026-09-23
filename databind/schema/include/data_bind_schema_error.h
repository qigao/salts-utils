#ifndef DATA_BIND_SCHEMA_ERROR_H
#define DATA_BIND_SCHEMA_ERROR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DataBindSchemaErrorCode {
    DATA_BIND_SCHEMA_OK = 0,
    DATA_BIND_SCHEMA_ERR_INVALID_ARGUMENT,
    DATA_BIND_SCHEMA_ERR_OUT_OF_MEMORY,
    DATA_BIND_SCHEMA_ERR_LEXER,
    DATA_BIND_SCHEMA_ERR_SYNTAX,
    DATA_BIND_SCHEMA_ERR_SEMANTIC,
    DATA_BIND_SCHEMA_ERR_IO
} DataBindSchemaErrorCode;

typedef struct DataBindSchemaError {
    DataBindSchemaErrorCode code;
    int line;
    int column;
    char message[256];
} DataBindSchemaError;

const char *data_bind_schema_error_string(DataBindSchemaErrorCode code);
void data_bind_schema_error_init(DataBindSchemaError *error);
void data_bind_schema_error_set(
    DataBindSchemaError *error,
    DataBindSchemaErrorCode code,
    int line,
    int column,
    const char *message);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_SCHEMA_ERROR_H */
