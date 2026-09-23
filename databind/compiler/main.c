/**
 * @file main.c
 * @brief databindc — DataBind IDL/compiler frontend.
 *
 * Reads a .schema file, parses it, and renders output through a Mustache
 * template.  Supports multiple target languages by selecting different
 * template files (built-in or custom).
 *
 * CLI (via cmd_arger):
 *   databindc <file> [--template <file>]
 *              [--lang c|cpp|cxx|go|rust|python|py|ts|typescript|sqlite|postgresql|postgres]
 *              [--output <file>] [--source-output <file>] [--lua-output <file>]
 *              [--dsl-output <file>]
 * Database DDL languages require explicit --output. Auxiliary source, guest, Lua,
 * and DSL outputs remain part of the built-in C generation path only.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmd_arger.h"
#include "compiler_core.h"
#include "salts_fs.h"

static const char *DATABIND_COMPILER_LANG_OPTION_LIST =
    "c, cpp, cxx, go, rust, python, py, ts, typescript, sqlite, postgresql, postgres";
static const char *DATABIND_COMPILER_LANG_OPTION_HELP =
    "Target language (built-in template: c, cpp, cxx, go, rust, python, py, ts, "
    "typescript, sqlite, postgresql, postgres)";

static int resolve_resource_dir(const char *argv0, char *out, size_t out_size) {
    const char *path_env;
    const char *cursor;
    char candidate[SALTS_FS_MAX_PATH];
    char directory[SALTS_FS_MAX_PATH];

    if (argv0 == NULL || out == NULL || out_size == 0) return 0;
    if (strchr(argv0, '/') != NULL || strchr(argv0, '\\') != NULL) {
        return salts_fs_path_dirname(argv0, out, out_size) == 0;
    }
    path_env = getenv("PATH");
    if (path_env == NULL) return 0;
    cursor = path_env;
    while (*cursor != '\0') {
        const char *end = strchr(cursor,
#ifdef _WIN32
                                 ';'
#else
                                 ':'
#endif
        );
        size_t len = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (len > 0 && len < sizeof(directory)) {
            memcpy(directory, cursor, len);
            directory[len] = '\0';
            if (salts_fs_path_join(candidate, sizeof(candidate), directory, argv0) == 0 &&
                salts_fs_access(candidate, SALTS_FS_ACCESS_EXISTS) == 0)
                return salts_fs_path_dirname(candidate, out, out_size) == 0;
#ifdef _WIN32
            if (snprintf(candidate, sizeof(candidate), "%s\\%s.exe", directory, argv0) > 0 &&
                salts_fs_access(candidate, SALTS_FS_ACCESS_EXISTS) == 0)
                return salts_fs_path_dirname(candidate, out, out_size) == 0;
#endif
        }
        if (end == NULL) break;
        cursor = end + 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char    *schema_path   = NULL;
    char    *template_path = NULL;
    char    *lang_name = NULL;
    char    *output_path   = NULL;
    char    *source_output_path = NULL;
    char    *lua_output_path = NULL;
    char    *guest_output_path = NULL;
    char    *dsl_output_path = NULL;
    int64_t  lang_enum     = DATABIND_COMPILER_LANG_C;
    char resource_dir[SALTS_FS_MAX_PATH];

    if (!resolve_resource_dir(argc > 0 ? argv[0] : NULL, resource_dir, sizeof(resource_dir))) {
        fprintf(stderr, "Failed to locate databindc resource directory\n");
        return 1;
    }

    CmdArgerDesc required_args[] = {
        cmd_arger_desc_string(&schema_path, "schema",
                              "Path to the .schema definition file"),
    };
    CmdArgerDesc optional_args[] = {
        cmd_arger_desc_string_sh(&template_path, "template", "t",
                                 "Path to a custom Mustache template file"),
        cmd_arger_desc_string_sh(&lang_name, "lang", "l",
                                 DATABIND_COMPILER_LANG_OPTION_HELP),
        cmd_arger_desc_string_sh(
            &output_path, "output", "o",
            "Output file path (required for sqlite/postgresql/postgres; "
            "default: stdout for other languages)"),
        cmd_arger_desc_string_sh(&source_output_path, "source-output", "s",
                                 "Generate the C typed serde companion source"),
        cmd_arger_desc_string(&lua_output_path, "lua-output",
                              "Generate C adapters from typed records to Lua tables"),
        cmd_arger_desc_string_sh(&guest_output_path, "guest-output", "g",
                                 "Generate the C Wasm guest adapter source"),
        cmd_arger_desc_string_sh(&dsl_output_path, "dsl-output", "d",
                                 "Generate DSL type declarations (.rfl file)"),
    };

    cmd_arger_parse(optional_args,
                    (uint32_t)(sizeof(optional_args) / sizeof(optional_args[0])),
                    required_args,
                    (uint32_t)(sizeof(required_args) / sizeof(required_args[0])),
                    argc, argv, "databindc 3.0", cmd_arger_true);

    if (lang_name != NULL &&
        data_bind_compiler_parse_language_name(lang_name, &lang_enum) != 0) {
        fprintf(stderr, "Unsupported --lang '%s'. Expected one of: %s\n",
                lang_name, DATABIND_COMPILER_LANG_OPTION_LIST);
        return 1;
    }

    data_bind_compiler_options_t options = {
        .schema_path = schema_path,
        .template_path = template_path,
        .output_path = output_path,
        .source_output_path = source_output_path,
        .lua_output_path = lua_output_path,
        .guest_output_path = guest_output_path,
        .dsl_output_path = dsl_output_path,
        .resource_dir = resource_dir,
        .lang_enum = lang_enum,
    };

    int res = data_bind_compiler_run(&options);
    return res;
}
