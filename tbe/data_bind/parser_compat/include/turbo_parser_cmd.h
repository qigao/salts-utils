#ifndef TURBO_PARSER_CMD_H
#define TURBO_PARSER_CMD_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CMD Parser */
typedef struct turbo_cmd_parser_s turbo_cmd_parser_t;
typedef struct turbo_cmd_node_s turbo_cmd_node_t;
typedef turbo_cmd_node_t turbo_cmd_subcommand_t;

#define TURBO_CMD_PARSE_RESULT_V1_SIZE sizeof(turbo_cmd_parse_result_t)

typedef int (*turbo_cmd_write_fn)(const char *data, size_t size,
                                  void *write_context);

typedef enum {
  TURBO_CMD_PARSE_OK = 0,
  TURBO_CMD_PARSE_HELP,
  TURBO_CMD_PARSE_VERSION,
  TURBO_CMD_PARSE_INVALID
} turbo_cmd_parse_status_t;

/**
 * Structured result for the non-printing parser. String pointers are owned by
 * the parser and remain valid until the next parse or parser destruction.
 * argv-derived option and positional values remain borrowed from argv.
 */
typedef struct {
  size_t size;
  turbo_cmd_parse_status_t status;
  const turbo_cmd_node_t *leaf;
  int argument_index;
  const char *error_code;
  const char *message;
} turbo_cmd_parse_result_t;

/* Enum choice for turbo_cmd_add_enum */
typedef struct {
  const char *name;
  const char *info;
  int64_t value;
} turbo_cmd_enum_t;

/* Custom validator signature */
typedef bool (*turbo_cmd_validator_t)(const char *value, const char **error_message);

/**
 * @brief Create a command line argument parser.
 * @param app_name Name of the application.
 * @param version Application version string.
 * @return Pointer to the new command parser.
 */
TURBO_PARSER_API turbo_cmd_parser_t *turbo_cmd_create(const char *app_name, const char *version);

/**
 * @brief Destroy a command line argument parser and free its resources.
 * @param parser Pointer to the command parser.
 */
TURBO_PARSER_API void turbo_cmd_destroy(turbo_cmd_parser_t *parser);

/* Basic argument types */
/**
 * @brief Add a boolean flag argument.
 * @param parser Pointer to the command parser.
 * @param out Pointer to store the result (true if flag present).
 * @param name Long name (e.g., "--verbose").
 * @param short_name Short name (e.g., "-v").
 * @param desc Argument description for help message.
 */
TURBO_PARSER_API void turbo_cmd_add_flag(turbo_cmd_parser_t *parser, bool *out, const char *name,
                                  const char *short_name, const char *desc);

/**
 * @brief Add a string argument.
 * @param parser Pointer to the command parser.
 * @param out Pointer to store the result string address.
 * @param name Long name.
 * @param short_name Short name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_add_string(turbo_cmd_parser_t *parser, char **out, const char *name,
                                    const char *short_name, const char *desc);

/**
 * @brief Add an integer argument.
 * @param parser Pointer to the command parser.
 * @param out Pointer to store the numeric result.
 * @param name Long name.
 * @param short_name Short name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_add_integer(turbo_cmd_parser_t *parser, int64_t *out, const char *name,
                                     const char *short_name, const char *desc);

/**
 * @brief Add a floating point argument.
 * @param parser Pointer to the command parser.
 * @param out Pointer to store the numeric result.
 * @param name Long name.
 * @param short_name Short name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_add_float(turbo_cmd_parser_t *parser, double *out, const char *name,
                                   const char *short_name, const char *desc);

/**
 * @brief Add an argument that accepts a list of strings.
 * @param parser Pointer to the command parser.
 * @param out_arr Array to store result string addresses.
 * @param out_count Pointer to store actual count of strings received.
 * @param max_count Maximum size of out_arr.
 * @param name Long name.
 * @param short_name Short name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_add_string_list(turbo_cmd_parser_t *parser, char **out_arr,
                                         uint32_t *out_count, uint32_t max_count, const char *name,
                                         const char *short_name, const char *desc);

/**
 * @brief Add an argument constrained to a set of enumerated choices.
 * @param parser Pointer to the command parser.
 * @param out Pointer to store the selected value (choice ID).
 * @param name Long name.
 * @param short_name Short name.
 * @param desc Description.
 * @param choices Array of valid options.
 * @param choices_count Size of choices array.
 */
TURBO_PARSER_API void turbo_cmd_add_enum(turbo_cmd_parser_t *parser, int64_t *out, const char *name,
                                  const char *short_name, const char *desc,
                                  turbo_cmd_enum_t *choices, uint32_t choices_count);

/* Required positional arguments */
/**
 * @brief Add a required positional string argument.
 * @param parser Pointer to the command parser.
 * @param out Pointer to store the string result.
 * @param name Internal identifier name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_add_required_string(turbo_cmd_parser_t *parser, char **out,
                                             const char *name, const char *desc);

/**
 * @brief Add a required positional integer argument.
 * @param parser Pointer to the command parser.
 * @param out Pointer to store the numeric result.
 * @param name Internal identifier name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_add_required_integer(turbo_cmd_parser_t *parser, int64_t *out,
                                              const char *name, const char *desc);

/* Argument modifiers (return index for chaining) */
/**
 * @brief Bind a command line argument to an environment variable.
 * @param parser Pointer to the command parser.
 * @param index Index of the argument to bind.
 * @param env_var Name of the environment variable.
 */
TURBO_PARSER_API void turbo_cmd_set_env(turbo_cmd_parser_t *parser, uint32_t index, const char *env_var);

/**
 * @brief Assign an argument to a logical group for help formatting.
 * @param parser Pointer to the command parser.
 * @param index Index of the argument.
 * @param group Group name.
 */
TURBO_PARSER_API void turbo_cmd_set_group(turbo_cmd_parser_t *parser, uint32_t index, const char *group);

/**
 * @brief Restrict a string argument to a fixed set of valid choices.
 * @param parser Pointer to the command parser.
 * @param index Index of the string argument.
 * @param choices Array of valid string choices.
 * @param count Number of choices in the array.
 */
TURBO_PARSER_API void turbo_cmd_set_choices(turbo_cmd_parser_t *parser, uint32_t index,
                                     const char **choices, uint32_t count);

/**
 * @brief Attach a custom validation function to an argument.
 * @param parser Pointer to the command parser.
 * @param index Index of the argument.
 * @param validator Pointer to the validator function.
 */
TURBO_PARSER_API void turbo_cmd_set_validator(turbo_cmd_parser_t *parser, uint32_t index,
                                       turbo_cmd_validator_t validator);

/**
 * @brief Mark an optional argument as required.
 * @param parser Pointer to the command parser.
 * @param index Index of the argument.
 */
TURBO_PARSER_API void turbo_cmd_set_required(turbo_cmd_parser_t *parser, uint32_t index);

/* Get last added argument index (for modifier chaining) */
/**
 * @brief Get the numeric index of the most recently added argument.
 * @param parser Pointer to the command parser.
 * @return The 0-based index of the last argument added.
 */
TURBO_PARSER_API uint32_t turbo_cmd_last_index(turbo_cmd_parser_t *parser);

/* Subcommand support */
/**
 * @brief Add a subcommand to the parser (e.g., "commit" for "git").
 * @param parser Pointer to the command parser.
 * @param name Subcommand name.
 * @param desc Subcommand description.
 * @return Pointer to the new subcommand object.
 */
TURBO_PARSER_API turbo_cmd_subcommand_t *turbo_cmd_add_subcommand(turbo_cmd_parser_t *parser,
                                                           const char *name, const char *desc);

/** Return the root command node owned by @p parser. */
TURBO_PARSER_API turbo_cmd_node_t *turbo_cmd_root(turbo_cmd_parser_t *parser);

/**
 * Add a recursively nested command. Node pointers remain stable until parser
 * destruction. Registration fails after the first parse freezes the tree.
 */
TURBO_PARSER_API turbo_cmd_node_t *turbo_cmd_add_command(turbo_cmd_node_t *parent,
                                                  const char *name,
                                                  const char *description);

/* Node option APIs mirror the root parser APIs and return 0 on success. */
TURBO_PARSER_API int turbo_cmd_node_add_flag(turbo_cmd_node_t *node, bool *out,
                                      const char *name, const char *short_name,
                                      const char *desc);
TURBO_PARSER_API int turbo_cmd_node_add_string(turbo_cmd_node_t *node, char **out,
                                        const char *name, const char *short_name,
                                        const char *desc);
TURBO_PARSER_API int turbo_cmd_node_add_integer(turbo_cmd_node_t *node, int64_t *out,
                                         const char *name, const char *short_name,
                                         const char *desc);
TURBO_PARSER_API int turbo_cmd_node_add_float(turbo_cmd_node_t *node, double *out,
                                       const char *name, const char *short_name,
                                       const char *desc);
TURBO_PARSER_API int turbo_cmd_node_add_string_list(turbo_cmd_node_t *node,
                                             char **out_arr,
                                             uint32_t *out_count,
                                             uint32_t max_count,
                                             const char *name,
                                             const char *short_name,
                                             const char *desc);
TURBO_PARSER_API int turbo_cmd_node_add_enum(turbo_cmd_node_t *node, int64_t *out,
                                      const char *name, const char *short_name,
                                      const char *desc,
                                      turbo_cmd_enum_t *choices,
                                      uint32_t choices_count);
TURBO_PARSER_API int turbo_cmd_node_add_required_string(turbo_cmd_node_t *node,
                                                 char **out,
                                                 const char *name,
                                                 const char *desc);
TURBO_PARSER_API int turbo_cmd_node_add_required_integer(turbo_cmd_node_t *node,
                                                  int64_t *out,
                                                  const char *name,
                                                  const char *desc);
TURBO_PARSER_API uint32_t turbo_cmd_node_last_index(const turbo_cmd_node_t *node);
TURBO_PARSER_API int turbo_cmd_node_set_env(turbo_cmd_node_t *node, uint32_t index,
                                     const char *env_var);
TURBO_PARSER_API int turbo_cmd_node_set_group(turbo_cmd_node_t *node, uint32_t index,
                                       const char *group);
TURBO_PARSER_API int turbo_cmd_node_set_choices(turbo_cmd_node_t *node,
                                         uint32_t index,
                                         const char **choices,
                                         uint32_t count);
TURBO_PARSER_API int turbo_cmd_node_set_validator(turbo_cmd_node_t *node,
                                           uint32_t index,
                                           turbo_cmd_validator_t validator);
TURBO_PARSER_API int turbo_cmd_node_set_required(turbo_cmd_node_t *node,
                                          uint32_t index);

/**
 * @brief Add a boolean flag to a subcommand.
 * @param sub Pointer to the subcommand.
 * @param out Pointer to store the result.
 * @param name Long name.
 * @param short_name Short name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_sub_add_flag(turbo_cmd_subcommand_t *sub, bool *out, const char *name,
                                      const char *short_name, const char *desc);

/**
 * @brief Add a string argument to a subcommand.
 * @param sub Pointer to the subcommand.
 * @param out Pointer to store the result string address.
 * @param name Long name.
 * @param short_name Short name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_sub_add_string(turbo_cmd_subcommand_t *sub, char **out, const char *name,
                                        const char *short_name, const char *desc);

/**
 * @brief Add an integer argument to a subcommand.
 * @param sub Pointer to the subcommand.
 * @param out Pointer to store the numeric result.
 * @param name Long name.
 * @param short_name Short name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_sub_add_integer(turbo_cmd_subcommand_t *sub, int64_t *out,
                                         const char *name, const char *short_name,
                                         const char *desc);

/**
 * @brief Add a required positional string argument to a subcommand.
 * @param sub Pointer to the subcommand.
 * @param out Pointer to store the string result.
 * @param name Internal identifier name.
 * @param desc Description.
 */
TURBO_PARSER_API void turbo_cmd_sub_add_required_string(turbo_cmd_subcommand_t *sub, char **out,
                                                 const char *name, const char *desc);

/* Parsing */
/**
 * @brief Parse the command line arguments.
 * @param parser Pointer to the command parser.
 * @param argc Number of arguments.
 * @param argv Array of argument strings.
 * @param colors true to enable colored help output.
 */
TURBO_PARSER_API void turbo_cmd_parse(turbo_cmd_parser_t *parser, int argc, char **argv, bool colors);

/**
 * @brief Parse arguments starting from a subcommand.
 * @param parser Pointer to the command parser.
 * @param argc Number of arguments.
 * @param argv Array of argument strings.
 * @param colors true to enable colored output.
 * @return 0 on success, non-zero if internal error occurs.
 */
TURBO_PARSER_API int turbo_cmd_parse_subcommand(turbo_cmd_parser_t *parser, int argc, char **argv,
                                         bool colors);

/**
 * Parse a recursive command tree without printing or terminating the process.
 * The caller initializes result.size to sizeof(turbo_cmd_parse_result_t).
 * Returns 0 when a structured result was produced and -1 for invalid API use
 * or allocation failure.
 */
TURBO_PARSER_API int turbo_cmd_parse_ex(turbo_cmd_parser_t *parser, int argc,
                                 char **argv,
                                 turbo_cmd_parse_result_t *result);

/** Render help for root or a selected node through a caller-owned sink. */
TURBO_PARSER_API int turbo_cmd_render_help(const turbo_cmd_parser_t *parser,
                                    const turbo_cmd_node_t *node,
                                    turbo_cmd_write_fn write_fn,
                                    void *write_context);

/**
 * @brief Display the auto-generated help documentation to stdout.
 * @param parser Pointer to the command parser.
 * @param colors true to enable colored output.
 */
TURBO_PARSER_API void turbo_cmd_show_help(turbo_cmd_parser_t *parser, bool colors);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_CMD_H
