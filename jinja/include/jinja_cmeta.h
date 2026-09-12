#ifndef JINJA_CMETA_H
#define JINJA_CMETA_H

#include <cmeta/data.h>
#include <vstr.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef JINJA_CMETA_API
  #if defined(_WIN32) && defined(JINJA_CMETA_BUILD_DLL)
    #define JINJA_CMETA_API __declspec(dllexport)
  #elif defined(__GNUC__) && __GNUC__ >= 4
    #define JINJA_CMETA_API __attribute__((visibility("default")))
  #else
    #define JINJA_CMETA_API
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define JINJA_CMETA_MAX_BLOCK_DEPTH 64u
#define JINJA_CMETA_MAX_CONDITION_BRANCHES 64u
#define JINJA_CMETA_MAX_TEMPLATE_BYTES (16u * 1024u * 1024u)
#define JINJA_CMETA_MAX_INSTRUCTIONS 65536u
#define JINJA_CMETA_DEFAULT_MAX_NODES 1024u
#define JINJA_CMETA_DEFAULT_MAX_STRING_BYTES (1024u * 1024u)
#define JINJA_CMETA_DEFAULT_MAX_VALUE_VISITS (1024u * 1024u)
#define JINJA_CMETA_MAX_VALUE_DEPTH 64u
#define JINJA_CMETA_ERROR_NAME_BYTES 256u
#define JINJA_CMETA_DEFAULT_MAX_LOADED_TEMPLATES 64u
#define JINJA_CMETA_DEFAULT_MAX_LOADED_SOURCE_BYTES JINJA_CMETA_MAX_TEMPLATE_BYTES

typedef enum JINJA_CMETA_STATUS {
  JINJA_CMETA_OK = 0,
  JINJA_CMETA_ERR_INVALID_ARGUMENT = -1,
  JINJA_CMETA_ERR_SYNTAX = -2,
  JINJA_CMETA_ERR_UNSUPPORTED = -3,
  JINJA_CMETA_ERR_CAPACITY = -4,
  JINJA_CMETA_ERR_OUT_OF_MEMORY = -5,
  JINJA_CMETA_ERR_METADATA = -6,
  JINJA_CMETA_ERR_RENDER = -7,
  JINJA_CMETA_ERR_NOT_FOUND = -8,
  JINJA_CMETA_ERR_LOADER = -9
} JINJA_CMETA_STATUS;

typedef struct JINJA_CMETA_ERROR {
  JINJA_CMETA_STATUS status;
  size_t offset;
  char message[160];
  /** Owned UTF-8 prefix, NUL-terminated; length includes any embedded NULs.
   * offset is an original byte offset within this template, not its caller. */
  char template_name[JINJA_CMETA_ERROR_NAME_BYTES];
  size_t template_name_length;
  int template_name_truncated;
} JINJA_CMETA_ERROR;

#define JINJA_CMETA_ERROR_INIT {JINJA_CMETA_OK, 0u, {0}, {0}, 0u, 0}

typedef struct JINJA_CMETA_RENDER_OPTIONS {
  /** Bounds retained runtime nodes/activations and active write bindings. */
  size_t max_nodes;
  size_t max_string_bytes;
  /** Bounds lexical control depth and shared macro/block/include/recursive-loop call depth;
   * zero uses MAX_BLOCK_DEPTH. */
  unsigned max_render_depth;
  /** Total runtime value-comparison and dictionary-key validation visits per render;
   * zero uses DEFAULT_MAX_VALUE_VISITS. Excludes sameas and compile-time folding.
   * Exhaustion returns CAPACITY, including across loops and macro calls. */
  size_t max_value_visits;
  /** Active comparison/key-validation frames, counting roots and leaves.
   * Zero uses MAX_VALUE_DEPTH; values above MAX_VALUE_DEPTH are INVALID_ARGUMENT.
   * Independent of expression count and max_render_depth. */
  unsigned max_value_depth;
} JINJA_CMETA_RENDER_OPTIONS;

#define JINJA_CMETA_RENDER_OPTIONS_INIT {0u, 0u, 0u, 0u, 0u}

/**
 * Borrowed contiguous sequence view.
 *
 * A non-empty view requires non-NULL data, nonzero stride, and a valid element
 * descriptor. All bytes and descriptors must remain immutable until rendering
 * returns. No ownership is transferred.
 */
typedef struct JINJA_CMETA_SEQUENCE_VIEW {
  const void *data;
  size_t count;
  size_t stride;
  const cmeta_data_desc *element;
} JINJA_CMETA_SEQUENCE_VIEW;

typedef struct JINJA_CMETA_TEMPLATE JINJA_CMETA_TEMPLATE;

typedef enum JINJA_CMETA_EXTENSION_TAG {
  JINJA_CMETA_EXTENSION_TAG_DO = 1u << 0,
  JINJA_CMETA_EXTENSION_TAG_LOOP_CONTROLS = 1u << 1,
  JINJA_CMETA_EXTENSION_TAG_CUSTOM = 1u << 2,
  JINJA_CMETA_EXTENSION_TAG_I18N = 1u << 3
} JINJA_CMETA_EXTENSION_TAG;

/** Compile-only borrowed UTF-8 views, immutable until compile returns.
 * Initialize with COMPILE_OPTIONS_INIT; a zero-filled struct is not valid.
 * Delimiters are nonempty, at most 128 bytes, and opening delimiters distinct.
 * NULL/0 disables a line prefix; non-NULL/0 enables an empty prefix.
 * Boolean policies accept only 0/1. newline_sequence is LF, CRLF, or CR.
 */
typedef struct JINJA_CMETA_COMPILE_OPTIONS {
  vstr variable_start_string;
  vstr variable_end_string;
  vstr block_start_string;
  vstr block_end_string;
  vstr comment_start_string;
  vstr comment_end_string;
  vstr line_statement_prefix;
  vstr line_comment_prefix;
  int trim_blocks;
  int lstrip_blocks;
  int keep_trailing_newline;
  vstr newline_sequence;
  /** Supported bits are EXTENSION_TAG_DO, EXTENSION_TAG_LOOP_CONTROLS,
   * EXTENSION_TAG_CUSTOM, and EXTENSION_TAG_I18N. Custom tags use
   * expression-call syntax. */
  unsigned extensions;
} JINJA_CMETA_COMPILE_OPTIONS;

#define JINJA_CMETA_COMPILE_OPTIONS_INIT \
  {{"{{", 2u}, {"}}", 2u}, {"{%", 2u}, {"%}", 2u}, {"{#", 2u}, {"#}", 2u}, \
   {NULL, 0u}, {NULL, 0u}, 0, 0, 0, {"\n", 1u}, 0u}

typedef struct JINJA_CMETA_ENV JINJA_CMETA_ENV;

/** Loader-owned bytes, borrowed until release; lease is opaque to the engine. */
typedef struct JINJA_CMETA_SOURCE {
  vstr text;
  void *lease;
} JINJA_CMETA_SOURCE;

/** Synchronous, non-reentrant callbacks. Both must be supplied or both omitted.
 * A successful load grants one lease: release runs exactly once after compilation,
 * including when source validation or compilation fails. On non-OK, load owns
 * its cleanup and release is not called. NOT_FOUND means only that name is absent;
 * I/O or permission errors are LOADER. Optional error details are copied by value.
 * release cannot fail or call the engine. userdata stays alive until env_destroy.
 */
typedef struct JINJA_CMETA_LOADER {
  JINJA_CMETA_STATUS (*load)(void *userdata, vstr name,
      JINJA_CMETA_SOURCE *source, JINJA_CMETA_ERROR *error);
  void (*release)(void *userdata, JINJA_CMETA_SOURCE *source);
  void *userdata;
} JINJA_CMETA_LOADER;

/** Synchronous template-name policy. Return exactly 0 or 1 to select the
 * initial autoescape state for a named environment template. The name is
 * borrowed for the callback only and the callback must not re-enter Jinja. */
typedef int (*JINJA_CMETA_AUTOESCAPE_SELECTOR)(void *userdata, vstr name);

typedef enum JINJA_CMETA_UNDEFINED_POLICY {
  JINJA_CMETA_UNDEFINED_DEFAULT = 0,
  JINJA_CMETA_UNDEFINED_STRICT = 1
} JINJA_CMETA_UNDEFINED_POLICY;

/** A restricted scalar view exposed to a registered host function. Strings are
 * borrowed for the callback only and must not be retained. */
typedef enum JINJA_CMETA_CALL_VALUE_KIND {
  JINJA_CMETA_CALL_VALUE_UNDEFINED,
  JINJA_CMETA_CALL_VALUE_NONE,
  JINJA_CMETA_CALL_VALUE_BOOL,
  JINJA_CMETA_CALL_VALUE_INTEGER,
  JINJA_CMETA_CALL_VALUE_FLOAT,
  JINJA_CMETA_CALL_VALUE_STRING
} JINJA_CMETA_CALL_VALUE_KIND;

typedef struct JINJA_CMETA_CALL_VALUE {
  JINJA_CMETA_CALL_VALUE_KIND kind;
  int boolean;
  int64_t integer;
  double floating;
  vstr string;
  int string_safe;
} JINJA_CMETA_CALL_VALUE;

typedef struct JINJA_CMETA_CALL_ARGUMENT {
  /** Empty for positional arguments; borrowed for the callback only. */
  vstr name;
  JINJA_CMETA_CALL_VALUE value;
  /** Non-NULL for generic CMeta arguments; borrowed for the callback only. */
  /* Borrowed CMeta metadata and object for a generic node argument. */
  const cmeta_data_desc *descriptor;
  const void *object;
} JINJA_CMETA_CALL_ARGUMENT;

typedef struct JINJA_CMETA_CALL_CONTEXT {
  vstr name;
  const JINJA_CMETA_CALL_ARGUMENT *arguments;
  size_t argument_count;
  size_t positional_count;
} JINJA_CMETA_CALL_CONTEXT;

typedef struct JINJA_CMETA_CALL_RESULT {
  JINJA_CMETA_CALL_VALUE value;
} JINJA_CMETA_CALL_RESULT;

/** Synchronous translation callback. context, singular, plural, and arguments
 * are borrowed for the callback only. plural is empty for a singular trans
 * block. The callback must return a scalar string; the engine copies it before
 * invoking the renderer. */
typedef JINJA_CMETA_STATUS (*JINJA_CMETA_TRANSLATION_INVOKE)(void *userdata,
    vstr context, vstr singular, vstr plural,
    const JINJA_CMETA_CALL_ARGUMENT *arguments, size_t argument_count,
    JINJA_CMETA_CALL_RESULT *result);

typedef JINJA_CMETA_STATUS (*JINJA_CMETA_CALLABLE_INVOKE)(void *userdata,
    const JINJA_CMETA_CALL_CONTEXT *context, JINJA_CMETA_CALL_RESULT *result);

typedef enum JINJA_CMETA_EXTENSION_KIND {
  JINJA_CMETA_EXTENSION_FUNCTION = 0,
  JINJA_CMETA_EXTENSION_FILTER,
  JINJA_CMETA_EXTENSION_TEST,
  JINJA_CMETA_EXTENSION_KIND_TAG
} JINJA_CMETA_EXTENSION_KIND;

/** Registered callable. The callback receives a leading operand for filters,
 * tests, and custom tags through JINJA_CMETA_CALL_ARGUMENT[0]. For generic
 * callables, descriptor/object expose a borrowed CMeta view only for the
 * duration of the callback; the callback must not retain either pointer. */
typedef struct JINJA_CMETA_CALLABLE {
  vstr name;
  size_t min_positional;
  size_t max_positional;
  size_t max_keywords;
  JINJA_CMETA_CALLABLE_INVOKE invoke;
  void *userdata;
  JINJA_CMETA_EXTENSION_KIND extension_kind;
  /** Nonzero exposes complex CMeta arguments through descriptor/object. */
  int generic;
} JINJA_CMETA_CALLABLE;

typedef struct JINJA_CMETA_GLOBAL {
  vstr name;
  JINJA_CMETA_CALL_VALUE value;
} JINJA_CMETA_GLOBAL;

#define JINJA_CMETA_MAX_REGISTERED_FUNCTIONS 64u
#define JINJA_CMETA_MAX_CACHED_TEMPLATES 64u

typedef struct JINJA_CMETA_REGISTRY JINJA_CMETA_REGISTRY;

/** Create a mutable extension registry. It is single-threaded and must be
 * quiescent while an environment copies it. Registration owns copies of names
 * and global strings; callback userdata remains owned by the caller. */
JINJA_CMETA_API JINJA_CMETA_REGISTRY *jinja_cmeta_registry_create(
    JINJA_CMETA_ERROR *error);

/** Register a function. Names of built-in global functions are rejected. */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_register(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error);

/** Explicitly replace an existing registered function. Built-in names remain
 * reserved and cannot be replaced. */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_replace(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error);

/** Register a filter in its own namespace; duplicate names are rejected. */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_register_filter(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error);

JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_replace_filter(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error);

/** Register a test in its own namespace; duplicate names are rejected. */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_register_test(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error);

JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_replace_test(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error);

/** Register a custom statement tag; duplicate names are rejected. */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_register_tag(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error);

JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_replace_tag(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_CALLABLE *callable,
    JINJA_CMETA_ERROR *error);

/** Register an immutable scalar global; duplicate names are rejected. */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_register_global(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_GLOBAL *global,
    JINJA_CMETA_ERROR *error);

JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_registry_replace_global(
    JINJA_CMETA_REGISTRY *registry, const JINJA_CMETA_GLOBAL *global,
    JINJA_CMETA_ERROR *error);

JINJA_CMETA_API void jinja_cmeta_registry_destroy(JINJA_CMETA_REGISTRY *registry);

typedef struct JINJA_CMETA_ENV_OPTIONS {
  JINJA_CMETA_COMPILE_OPTIONS compile;
  JINJA_CMETA_LOADER loader;
  /** Zero selects DEFAULT_MAX_LOADED_*. Includes share these cumulative budgets
 * per render; named loads reuse the environment's immutable compiled cache. */
  size_t max_loaded_templates;
  size_t max_loaded_source_bytes;
  JINJA_CMETA_AUTOESCAPE_SELECTOR autoescape_selector;
  void *autoescape_userdata;
  JINJA_CMETA_UNDEFINED_POLICY undefined_policy;
  JINJA_CMETA_TRANSLATION_INVOKE translation;
  void *translation_userdata;
} JINJA_CMETA_ENV_OPTIONS;

#define JINJA_CMETA_ENV_OPTIONS_INIT \
  {JINJA_CMETA_COMPILE_OPTIONS_INIT, {NULL, NULL, NULL}, 0u, 0u, NULL, NULL, \
   JINJA_CMETA_UNDEFINED_DEFAULT, NULL, NULL}

/** Copy immutable configuration and callbacks; NULL options selects INIT defaults.
 * Returns an owned environment, or NULL with INVALID_ARGUMENT (invalid options or
 * unpaired callbacks), CAPACITY (delimiter limit), or OUT_OF_MEMORY.
 * Use each environment synchronously on one thread without callback reentry.
 * The environment cache is bounded by MAX_CACHED_TEMPLATES; no filesystem
 * lookup or automatic retry is provided. If autoescape_selector is set, it is
 * called once when each named template is compiled; its 0/1 result becomes the
 * template's initial autoescape state. Direct unnamed compilation remains off.
 * undefined_policy accepts DEFAULT or STRICT; the latter raises a render error
 * when an undefined value is consumed, while defined/undefined tests remain
 * available. EXTENSION_TAG_I18N requires translation; the callback receives
 * the compiled message id and evaluated named bindings.
 */
JINJA_CMETA_API JINJA_CMETA_ENV *jinja_cmeta_env_create(
    const JINJA_CMETA_ENV_OPTIONS *options, JINJA_CMETA_ERROR *error);

/** Create an environment and copy a completed registry into an immutable
 * environment snapshot. The registry may be destroyed after this returns.
 * Existing env_create remains the no-host-function form. */
JINJA_CMETA_API JINJA_CMETA_ENV *jinja_cmeta_env_create_with_registry(
    const JINJA_CMETA_ENV_OPTIONS *options, const JINJA_CMETA_REGISTRY *registry,
    JINJA_CMETA_ERROR *error);

/** First finish rendering and release every associated template; NULL is accepted.
 * Does not destroy templates or loader userdata on the caller's behalf. */
JINJA_CMETA_API void jinja_cmeta_env_destroy(JINJA_CMETA_ENV *env);

/** Compile source using env's configuration and retain a copy of its diagnostic name.
 * name is an exact UTF-8 key (empty and embedded NULs accepted), at most
 * MAX_TEMPLATE_BYTES; no path normalization is performed. Invalid name views or
 * UTF-8 return INVALID_ARGUMENT, excessive length CAPACITY. Source errors are as
 * for compile. The returned owned template borrows env until release.
 * Example: jinja_cmeta_env_compile(env, vstr_from_cstr("page"),
 *              vstr_from_cstr("Hello {{ name }}"), &error).
 */
JINJA_CMETA_API JINJA_CMETA_TEMPLATE *jinja_cmeta_env_compile(
    JINJA_CMETA_ENV *env, vstr name, vstr source, JINJA_CMETA_ERROR *error);

/** Load and compile one named template, without executing its body or dependencies.
 * Name/lifetime rules match env_compile. Completed templates are reused by name.
 * Missing loader is
 * LOADER, absent name NOT_FOUND, source exceeding max_loaded_source_bytes CAPACITY;
 * other loader/compile errors propagate. No source view survives this call.
 * Example: jinja_cmeta_env_load(env, vstr_from_cstr("page"), &error).
 */
JINJA_CMETA_API JINJA_CMETA_TEMPLATE *jinja_cmeta_env_load(
    JINJA_CMETA_ENV *env, vstr name, JINJA_CMETA_ERROR *error);

/** Release all environment-owned compiled templates; caller references remain valid. */
JINJA_CMETA_API void jinja_cmeta_env_cache_clear(JINJA_CMETA_ENV *env);

/** Synchronous byte sink. Return zero on success, nonzero to stop rendering.
 * text is borrowed for the callback only; size may include embedded NUL bytes.
 * Jinja owns escaping; the sink must not transform or escape these bytes.
 */
typedef struct JINJA_CMETA_RENDERER {
  int (*write)(const char *text, size_t size, void *user_data);
} JINJA_CMETA_RENDERER;

/** Compile an explicit-length UTF-8 source; NULL options uses INIT defaults.
 * Supports bounded macros/call blocks, defaults, argument
 * expansion, caller, lexical closures, named/scoped/required blocks and self.
 * Named templates support include/import/from/extends with shared render budgets,
 * optional context, candidate names and ignore missing. Use env_compile/env_load
 * to bind a loader.
 * Physical newlines normalize to newline_sequence; one trailing physical newline
 * is removed unless keep_trailing_newline is set. Explicit string escapes and
 * rendered data are not normalized. Source byte offsets remain original.
 * Returns an owned template, or NULL with optional error: INVALID_ARGUMENT for
 * invalid options/views, CAPACITY for limits, SYNTAX for malformed source,
 * UNSUPPORTED for unavailable runtime features, OUT_OF_MEMORY on allocation.
 * No source/configuration views survive the call. Example:
 * jinja_cmeta_compile(vstr_from_cstr("Hello {{ name }}"), NULL, &error).
 */
JINJA_CMETA_API JINJA_CMETA_TEMPLATE *jinja_cmeta_compile(vstr source,
    const JINJA_CMETA_COMPILE_OPTIONS *options, JINJA_CMETA_ERROR *error);

/** Release a compiled template; NULL is accepted. */
JINJA_CMETA_API void jinja_cmeta_release(JINJA_CMETA_TEMPLATE *templ);

/**
 * Render to a caller-provided Jinja byte sink.
 *
 * root_desc and root are borrowed and must remain immutable for the call.
 * Output is streaming: bytes emitted before an error are not rolled back.
 */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_render(
    const JINJA_CMETA_TEMPLATE *templ, const cmeta_data_desc *root_desc, const void *root,
    const JINJA_CMETA_RENDER_OPTIONS *options, const JINJA_CMETA_RENDERER *renderer,
    void *renderer_data, JINJA_CMETA_ERROR *error);

/**
 * Render to a malloc-owned NUL-terminated string.
 *
 * On success, *out_text must be released with free(). On failure, *out_text is
 * NULL and any partial internal output is discarded.
 * The final byte string (excluding its terminator) is also bounded by
 * options->max_string_bytes, or the default limit when omitted or zero.
 */
JINJA_CMETA_API JINJA_CMETA_STATUS jinja_cmeta_render_string(
    const JINJA_CMETA_TEMPLATE *templ, const cmeta_data_desc *root_desc, const void *root,
    const JINJA_CMETA_RENDER_OPTIONS *options, char **out_text, JINJA_CMETA_ERROR *error);

/** CMeta STRING descriptor for borrowed vstr fields. */
JINJA_CMETA_API const cmeta_data_desc *jinja_cmeta_vstr_data(void);

/** CMeta CUSTOM descriptor for JINJA_CMETA_SEQUENCE_VIEW fields. */
JINJA_CMETA_API const cmeta_data_desc *jinja_cmeta_sequence_data(void);

#ifdef __cplusplus
}
#endif

#endif
