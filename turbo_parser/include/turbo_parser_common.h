#ifndef TURBO_PARSER_COMMON_H
#define TURBO_PARSER_COMMON_H

#include <platform.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <turbo_error.h>
#include <turbo_str_view.h>

/** Serialized byte sink. Calls may use arbitrary non-empty chunk boundaries. */
typedef int (*turbo_write_fn)(const void *data, size_t len, void *user);

#define TURBO_QUERY_DEFAULT_MAX_INSTRUCTIONS 1048576U
#define TURBO_QUERY_DEFAULT_MAX_OPERANDS 1048576U
#define TURBO_QUERY_DEFAULT_MAX_REGEXES 65536U
#define TURBO_QUERY_DEFAULT_MAX_STEPS 1048576U
#define TURBO_QUERY_DIAGNOSTIC_MESSAGE_CAPACITY 160U

typedef enum turbo_query_status_e {
  TURBO_QUERY_OK = 0,
  TURBO_QUERY_INVALID_ARGUMENT = -1,
  TURBO_QUERY_INVALID_PROGRAM = -2,
  TURBO_QUERY_UNSUPPORTED = -3,
  TURBO_QUERY_BACKEND_ERROR = -4,
  TURBO_QUERY_NO_MEMORY = -5,
  TURBO_QUERY_RESOURCE_LIMIT = -6,
  TURBO_QUERY_BUFFER_TOO_SMALL = -7
} turbo_query_status_t;

/** Per-query hard limits. Copy semantics; every field must be non-zero. */
typedef struct turbo_query_limits_s {
  size_t size;
  uint32_t max_instructions;
  uint32_t max_operands;
  uint32_t max_regexes;
  uint32_t max_steps;
} turbo_query_limits_t;

#define TURBO_QUERY_LIMITS_INIT                                                   \
  {                                                                              \
    sizeof(turbo_query_limits_t), TURBO_QUERY_DEFAULT_MAX_INSTRUCTIONS,          \
        TURBO_QUERY_DEFAULT_MAX_OPERANDS, TURBO_QUERY_DEFAULT_MAX_REGEXES,       \
        TURBO_QUERY_DEFAULT_MAX_STEPS                                             \
  }

/** Caller-owned diagnostic with an owned message copy; no parser lifetime dependency. */
typedef struct turbo_query_diagnostic_s {
  size_t size;
  turbo_query_status_t status;
  uint32_t instruction;
  uint8_t opcode;
  uint8_t reserved[3];
  uint32_t operand;
  char message[TURBO_QUERY_DIAGNOSTIC_MESSAGE_CAPACITY];
} turbo_query_diagnostic_t;

#define TURBO_QUERY_DIAGNOSTIC_INIT                                                   \
  {                                                                                  \
    sizeof(turbo_query_diagnostic_t), TURBO_QUERY_OK, UINT32_MAX, UINT8_MAX,          \
        {0, 0, 0}, UINT32_MAX, {0}                                                    \
  }

#endif // TURBO_PARSER_COMMON_H

