#include "compiler_core.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sql_projection_write_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  size_t length = strlen(text);
  if (file == NULL) return 0;
  if (fwrite(text, 1u, length, file) != length) {
    fclose(file);
    return 0;
  }
  return fclose(file) == 0;
}

static int sql_projection_output_contains(
    const char *path, const char *needle) {
  FILE *file = fopen(path, "rb");
  char buffer[4096];
  size_t length;
  if (file == NULL) return 0;
  length = fread(buffer, 1u, sizeof(buffer) - 1u, file);
  if (ferror(file) || !feof(file)) {
    fclose(file);
    return 0;
  }
  buffer[length] = '\0';
  fclose(file);
  return strstr(buffer, needle) != NULL;
}

spec("sql_idl_format_independence") {
  it("accepts SQL output for a valid contract with Binary-incompatible field order") {
    static const char input[] = "sql_binary_independence.schema";
    static const char sqlite[] = "sql_binary_independence.sqlite.sql";
    static const char postgres[] = "sql_binary_independence.postgresql.sql";
    static const char schema[] =
        "schema SqlOnly; "
        "[db_table(records)] message Record { "
        "string description; uint32 code; "
        "}";
    static const struct {
      int64_t language;
      const char *output;
    } cases[] = {
        {TBE_COMPILER_LANG_SQLITE, sqlite},
        {TBE_COMPILER_LANG_POSTGRESQL, postgres},
    };

    remove(input);
    remove(sqlite);
    remove(postgres);
    check_true(sql_projection_write_file(input, schema));

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tbe_compiler_options_t options = {
          .schema_path = input,
          .output_path = cases[i].output,
          .resource_dir = TBE_COMPILER_RESOURCE_DIR,
          .lang_enum = cases[i].language,
      };
      check_equal(tbe_compiler_run(&options), 0);
      check_true(sql_projection_output_contains(cases[i].output, "CREATE TABLE"));
      check_true(sql_projection_output_contains(cases[i].output, "description"));
      check_true(sql_projection_output_contains(cases[i].output, "code"));
    }

    remove(input);
    remove(sqlite);
    remove(postgres);
  }

  it("continues rejecting the same field order for Binary-dependent C generation") {
    static const char input[] = "sql_binary_independence.schema";
    static const char output[] = "sql_binary_independence.h";
    static const char schema[] =
        "schema SqlOnly; "
        "[db_table(records)] message Record { "
        "string description; uint32 code; "
        "}";
    tbe_compiler_options_t options = {
        .schema_path = input,
        .output_path = output,
        .resource_dir = TBE_COMPILER_RESOURCE_DIR,
        .lang_enum = TBE_COMPILER_LANG_C,
    };
    remove(input);
    remove(output);
    check_true(sql_projection_write_file(input, schema));
    check_not_equal(tbe_compiler_run(&options), 0);
    {
      FILE *file = fopen(output, "rb");
      check_null(file);
      if (file != NULL) fclose(file);
    }
    remove(input);
    remove(output);
  }
}
