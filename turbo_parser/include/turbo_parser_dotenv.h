#ifndef TURBO_PARSER_DOTENV_H
#define TURBO_PARSER_DOTENV_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DotEnv Parser */
/**
 * @brief Load environment variables from a specific .env file.
 * @param path Path to the .env file.
 * @param overwrite true to overwrite existing environment variables.
 * @return 0 on success, negative error code otherwise.
 */
TURBO_PARSER_API int turbo_dotenv_load(const char *path, bool overwrite);

/**
 * @brief Load environment variables from the default ".env" file in CWD.
 * @param overwrite true to overwrite existing environment variables.
 * @return 0 on success, negative error code otherwise.
 */
TURBO_PARSER_API int turbo_dotenv_load_default(bool overwrite);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_DOTENV_H

