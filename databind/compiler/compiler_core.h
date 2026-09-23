#ifndef DATABIND_COMPILER_CORE_H
#define DATABIND_COMPILER_CORE_H

#include "node_tree.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct data_bind_compiler_options_s {
  const char *schema_path;
  const char *template_path;
  const char *output_path;
  const char *source_output_path;
  const char *lua_output_path;
  const char *guest_output_path;
  const char *dsl_output_path;
  const char *resource_dir;
  int64_t lang_enum;
} data_bind_compiler_options_t;

enum {
  DATABIND_COMPILER_LANG_C = 0,
  DATABIND_COMPILER_LANG_PYTHON = 1,
  DATABIND_COMPILER_LANG_RUST = 2,
  DATABIND_COMPILER_LANG_CPP = 3,
  DATABIND_COMPILER_LANG_GO = 4,
  DATABIND_COMPILER_LANG_TS = 5,
  DATABIND_COMPILER_LANG_SQLITE = 6,
  DATABIND_COMPILER_LANG_POSTGRESQL = 7
};

char *data_bind_compiler_read_file(const char *filename);

int data_bind_compiler_parse_language_name(const char *name, int64_t *out_lang_enum);

const char *data_bind_compiler_resolve_template(const char *user_template,
                                          int64_t lang_enum);

void data_bind_compiler_annotate_language_types(Node *root);

int data_bind_compiler_parse_schema_file(const char *schema_path, Node **out_root,
                                   char **out_schema_data);

int data_bind_compiler_render_file(Node *root, const char *template_path,
                             const char *output_path);

int data_bind_compiler_run(const data_bind_compiler_options_t *options);

#ifdef __cplusplus
}
#endif

#endif
