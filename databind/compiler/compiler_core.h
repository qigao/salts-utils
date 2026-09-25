#ifndef TBE_COMPILER_CORE_H
#define TBE_COMPILER_CORE_H

#include "node_tree.h"
#include "projection.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tbe_compiler_options_s {
  const char *schema_path;
  const char *template_path;
  const char *output_path;
  const char *source_output_path;
  const char *guest_output_path;
  const char *dsl_output_path;
  const char *resource_dir;
  int64_t lang_enum;

  /*
   * Compiler-private artifact projection selection.
   *
   * Source-language rendering remains orthogonal. The current CLI leaves these
   * fields empty; #142/#144 backends populate them through the unified
   * projection frontend rather than adding backend-specific option fields.
   */
  const databind_compiler_projection_request *projection_requests;
  size_t projection_count;
  const databind_compiler_projection_backend *projection_backends;
  size_t projection_backend_count;
} tbe_compiler_options_t;

enum {
  TBE_COMPILER_LANG_C = 0,
  TBE_COMPILER_LANG_PYTHON = 1,
  TBE_COMPILER_LANG_RUST = 2,
  TBE_COMPILER_LANG_CPP = 3,
  TBE_COMPILER_LANG_GO = 4,
  TBE_COMPILER_LANG_TS = 5,
  TBE_COMPILER_LANG_SQLITE = 6,
  TBE_COMPILER_LANG_POSTGRESQL = 7
};

char *tbe_compiler_read_file(const char *filename);

int tbe_compiler_parse_language_name(const char *name, int64_t *out_lang_enum);

const char *tbe_compiler_resolve_template(const char *user_template,
                                          int64_t lang_enum);

void tbe_compiler_annotate_language_types(Node *root);

int tbe_compiler_parse_schema_file(const char *schema_path, Node **out_root,
                                   char **out_schema_data);

int tbe_compiler_render_file(Node *root, const char *template_path,
                             const char *output_path);

int tbe_compiler_run(const tbe_compiler_options_t *options);

#ifdef __cplusplus
}
#endif

#endif
