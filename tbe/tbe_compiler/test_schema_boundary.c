#include "compiler_core.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int write_text_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  size_t length;

  if (!file) return -1;
  length = strlen(text);
  if (fwrite(text, 1, length, file) != length) {
    fclose(file);
    return -1;
  }
  return fclose(file);
}

static int file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  fclose(file);
  return 1;
}

spec("tbe_compiler_schema_boundary") {
  it("rejects varint before every language target can publish output") {
    static const int64_t languages[] = {
        TBE_COMPILER_LANG_C,
        TBE_COMPILER_LANG_PYTHON,
        TBE_COMPILER_LANG_RUST,
        TBE_COMPILER_LANG_CPP,
        TBE_COMPILER_LANG_GO,
        TBE_COMPILER_LANG_TS,
        TBE_COMPILER_LANG_SQLITE,
        TBE_COMPILER_LANG_POSTGRESQL,
    };
    static const char *const output_paths[] = {
        "test_varint_boundary_c.out",
        "test_varint_boundary_python.out",
        "test_varint_boundary_rust.out",
        "test_varint_boundary_cpp.out",
        "test_varint_boundary_go.out",
        "test_varint_boundary_ts.out",
        "test_varint_boundary_sqlite.out",
        "test_varint_boundary_postgresql.out",
    };
    static const char schema_path[] = "test_varint_boundary.schema";
    static const char schema[] = "message Bad { varint value; }";

    check_equal(sizeof(languages) / sizeof(languages[0]),
                sizeof(output_paths) / sizeof(output_paths[0]));
    remove(schema_path);
    check_equal(write_text_file(schema_path, schema), 0);

    for (size_t i = 0; i < sizeof(languages) / sizeof(languages[0]); ++i) {
      tbe_compiler_options_t options = {
          .schema_path = schema_path,
          .output_path = output_paths[i],
          .resource_dir = TBE_COMPILER_RESOURCE_DIR,
          .lang_enum = languages[i],
      };
      int status;

      remove(output_paths[i]);
      status = tbe_compiler_run(&options);
      info("language=%lld output=%s", (long long)languages[i], output_paths[i]);
      check_not_equal(status, 0);
      check_false(file_exists(output_paths[i]));
      remove(output_paths[i]);
    }

    remove(schema_path);
  }
}
