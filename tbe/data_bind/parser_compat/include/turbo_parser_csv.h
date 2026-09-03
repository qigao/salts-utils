#ifndef TURBO_PARSER_CSV_H
#define TURBO_PARSER_CSV_H
#include <turbo_parser_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CSV */
typedef struct csv_doc_s turbo_csv_doc_t;
typedef struct csv_sax_parser_s turbo_csv_sax_parser_t;
typedef struct dsv_filter_s turbo_dsv_filter_t;
typedef struct csv_stream_processor_s turbo_csv_stream_processor_t;

typedef struct turbo_csv_options_s {
  bool has_header;
  char delimiter;
  char quote;
  bool skip_empty_rows;
} turbo_csv_options_t;

typedef struct turbo_csv_sax_handler_s {
  int (*on_row_start)(void *ctx, size_t row_index);
  int (*on_field)(void *ctx, size_t row_index, size_t column_index, const char *value,
                  size_t value_len);
  int (*on_row_end)(void *ctx, size_t row_index, size_t field_count);
} turbo_csv_sax_handler_t;

typedef void (*turbo_dsv_row_callback_t)(void *user_data, size_t row_index,
                                         const char *rendered_row);

/**
 * @brief Parse CSV data.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param out Address of a pointer (turbo_csv_doc_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
TURBO_PARSER_API int turbo_parse_csv(const uint8_t *data, size_t len, turbo_csv_doc_t **out);

/**
 * @brief Parse CSV data with options.
 * @param data Input buffer.
 * @param len Buffer length.
 * @param opts CSV options.
 * @param out Address of a pointer (turbo_csv_doc_t **) to store the result.
 * @return 0 on success, error code otherwise.
 */
TURBO_PARSER_API int turbo_parse_csv_opts(const uint8_t *data, size_t len, const turbo_csv_options_t *opts,
                                   turbo_csv_doc_t **out);

TURBO_PARSER_API int turbo_parse_csv_sax(const uint8_t *data, size_t len,
                                  const turbo_csv_sax_handler_t *handler, void *ctx,
                                  const turbo_csv_options_t *opts);
TURBO_PARSER_API turbo_csv_sax_parser_t *
turbo_csv_sax_parser_create(const turbo_csv_sax_handler_t *handler, void *ctx,
                            const turbo_csv_options_t *opts);
TURBO_PARSER_API int turbo_csv_sax_parser_feed(turbo_csv_sax_parser_t *parser, const char *data,
                                        size_t len);
TURBO_PARSER_API int turbo_csv_sax_parser_finish(turbo_csv_sax_parser_t *parser);
TURBO_PARSER_API const char *turbo_csv_sax_parser_error(const turbo_csv_sax_parser_t *parser);
TURBO_PARSER_API void turbo_csv_sax_parser_destroy(turbo_csv_sax_parser_t *parser);

/**
 * @brief Free CSV data and set pointer to NULL.
 * @param out Address of the pointer (turbo_csv_doc_t **) to free.
 */
TURBO_PARSER_API void turbo_free_csv(turbo_csv_doc_t **out);

/**
 * @brief Get number of rows in CSV.
 * @param doc Pointer to CSV document.
 * @return Row count.
 */
TURBO_PARSER_API size_t turbo_csv_row_count(const turbo_csv_doc_t *doc);

/**
 * @brief Get number of columns in CSV.
 * @param doc Pointer to CSV document.
 * @return Column count.
 */
TURBO_PARSER_API size_t turbo_csv_column_count(const turbo_csv_doc_t *doc);

/**
 * @brief Get cell value as string.
 * @param doc Pointer to CSV document.
 * @param row Row index.
 * @param col Column index.
 * @return Cell string value.
 */
TURBO_PARSER_API const char *turbo_csv_get(const turbo_csv_doc_t *doc, size_t row, size_t col);

/**
 * @brief Get cell value as integer.
 * @param doc Pointer to CSV document.
 * @param row Row index.
 * @param col Column index.
 * @param def Default value.
 * @return cell integer value.
 */
TURBO_PARSER_API int turbo_csv_get_int(const turbo_csv_doc_t *doc, size_t row, size_t col, int def);

/**
 * @brief Get cell value as double.
 * @param doc Pointer to CSV document.
 * @param row Row index.
 * @param col Column index.
 * @param def Default value.
 * @return cell double value.
 */
TURBO_PARSER_API double turbo_csv_get_double(const turbo_csv_doc_t *doc, size_t row, size_t col,
                                      double def);

/**
 * @brief Get cell value as boolean.
 * @param doc Pointer to CSV document.
 * @param row Row index.
 * @param col Column index.
 * @param def Default value.
 * @return cell boolean value.
 */
TURBO_PARSER_API bool turbo_csv_get_bool(const turbo_csv_doc_t *doc, size_t row, size_t col, bool def);

/**
 * @brief Find column index by header name.
 * @param doc Pointer to CSV document.
 * @param header_name Header name.
 * @return Column index, or (size_t)-1 if not found.
 */
TURBO_PARSER_API size_t turbo_csv_find_column(const turbo_csv_doc_t *doc, const char *header_name);

/** Serialize the complete CSV document. Free with turbo_csv_serialize_free(). */
TURBO_PARSER_API char *turbo_csv_serialize(const turbo_csv_doc_t *doc, size_t *out_len);
TURBO_PARSER_API void turbo_csv_string_free(char *str);
TURBO_PARSER_API void turbo_csv_serialize_free(char *str);

/** Serialize to a byte sink. Callback boundaries have no record semantics. */
TURBO_PARSER_API int turbo_csv_write(const turbo_csv_doc_t *doc, turbo_write_fn write, void *user);

/** Serialize one complete logical CSV record per callback invocation. */
TURBO_PARSER_API int turbo_csv_write_records(const turbo_csv_doc_t *doc, turbo_write_fn write, void *user);

/**
 * @brief Write CSV document to file.
 * @param doc Pointer to CSV document.
 * @param filename Target filename.
 * @return 0 on success, non-zero on failure.
 */
TURBO_PARSER_API int turbo_csv_write_file(const turbo_csv_doc_t *doc, const char *filename);

/**
 * @brief Create a CSVPath filter bound to a parsed CSV document.
 * @param doc Parsed CSV
 * document.
 * @param header_row_index Header row index (0-based).
 * @return CSVPath filter
 * handle, or NULL on failure.
 */
TURBO_PARSER_API turbo_dsv_filter_t *turbo_dsv_filter_create(const turbo_csv_doc_t *doc,
                                                      size_t header_row_index);

/**
 * @brief Destroy a CSVPath filter.
 * @param filter CSVPath filter handle.
 */
TURBO_PARSER_API void turbo_dsv_filter_destroy(turbo_dsv_filter_t *filter);

/**
 * @brief Get last CSVPath filter error message.
 * @param filter CSVPath filter handle.
 *
 * @return Error string, or empty/null when no error.
 */
TURBO_PARSER_API const char *turbo_dsv_filter_error(turbo_dsv_filter_t *filter);

/**
 * @brief Compile CSVPath filter expression.
 * @details Supports:
 *          - logical join:
 * and/or
 *          - comparison: == != > >= < <=
 *          - numeric lhs arithmetic: + - * /,
 * unary +/- and parentheses
 *          - string literal rhs: "..."
 *          See
 * tScript/docs/csv_filter_expression.md for full syntax and
 *          error semantics.
 * @param
 * filter CSVPath filter handle.
 * @param expression Expression string.
 * @return true on success,
 * false on failure.
 */
TURBO_PARSER_API bool turbo_dsv_filter_compile(turbo_dsv_filter_t *filter, const char *expression);
/** Compile a filter to QVM with copied limits. Unlike the compatibility entry,
 * this strict entry does not use the native evaluator when QVM lowering fails. */
TURBO_PARSER_API bool turbo_dsv_filter_compile_ex(turbo_dsv_filter_t *filter,
                                            const char *expression,
                                            const turbo_query_limits_t *limits,
                                            turbo_query_diagnostic_t *diagnostic);
/** Copy the last compile/evaluation diagnostic from filter. */
TURBO_PARSER_API turbo_query_status_t turbo_dsv_filter_query_diagnostic(
    const turbo_dsv_filter_t *filter, turbo_query_diagnostic_t *diagnostic);

/**
 * @brief Set output delimiter for rendered rows.
 * @param filter CSVPath filter handle.
 *
 * @param delimiter Delimiter character.
 */
TURBO_PARSER_API void turbo_dsv_filter_set_output_delimiter(turbo_dsv_filter_t *filter, char delimiter);

/**
 * @brief Evaluate filter on one row.
 * @param filter CSVPath filter handle.
 * @param
 * row_index Row index.
 * @return 1 match, 0 mismatch, -1 error.
 */
TURBO_PARSER_API int turbo_dsv_filter_check_row(turbo_dsv_filter_t *filter, size_t row_index);

/**
 * @brief Evaluate filter against one row represented as field views.
 * @param filter Compiled
 * CSVPath filter handle.
 * @param fields Non-owning field views for the current row.
 * @param
 * field_count Number of field views.
 * @return 1 match, 0 mismatch, -1 error.
 */
TURBO_PARSER_API int turbo_dsv_filter_check_values(turbo_dsv_filter_t *filter, const vstr *fields,
                                            size_t field_count);

/**
 * @brief Run filter across rows and emit matched rendered rows.
 * @param filter CSVPath filter
 * handle.
 * @param callback Callback for each matched row.
 * @param user_data User context passed
 * to callback.
 */
TURBO_PARSER_API void turbo_dsv_filter_run(turbo_dsv_filter_t *filter, turbo_dsv_row_callback_t callback,
                                    void *user_data);

/**
 * @brief Create CSV stream processor.
 * @param opts Optional CSV options. NULL uses defaults.
 * @return Processor handle, or NULL on failure.
 */
TURBO_PARSER_API turbo_csv_stream_processor_t *
turbo_csv_stream_processor_create(const turbo_csv_options_t *opts);

/**
 * @brief Destroy CSV stream processor.
 * @param p Processor handle.
 */
TURBO_PARSER_API void turbo_csv_stream_processor_destroy(turbo_csv_stream_processor_t *p);

/**
 * @brief Set filter expression before feeding rows.
 * @param p Processor handle.
 * @param expr Filter expression.
 * @return true on success.
 */
TURBO_PARSER_API bool turbo_csv_stream_processor_set_filter(turbo_csv_stream_processor_t *p,
                                                     const char *expr);

/**
 * @brief Select columns to accumulate.
 * @param p Processor handle.
 * @param names Comma-separated column names.
 */
TURBO_PARSER_API void turbo_csv_stream_processor_set_columns(turbo_csv_stream_processor_t *p,
                                                      const char *names);

/**
 * @brief Feed raw CSV bytes to processor.
 * @param data Data chunk.
 * @param len Data length.
 * @param user_data Processor handle.
 */
TURBO_PARSER_API void turbo_csv_stream_processor_feed(const char *data, size_t len, void *user_data);

/**
 * @brief Finish streaming and flush remaining buffered row.
 * @param p Processor handle.
 */
TURBO_PARSER_API void turbo_csv_stream_processor_finish(turbo_csv_stream_processor_t *p);

/**
 * @brief Get matched row count.
 * @param p Processor handle.
 * @return Number of matched rows.
 */
TURBO_PARSER_API size_t turbo_csv_stream_processor_row_count(const turbo_csv_stream_processor_t *p);

/**
 * @brief Get detected column count.
 * @param p Processor handle.
 * @return Number of columns.
 */
TURBO_PARSER_API size_t turbo_csv_stream_processor_col_count(const turbo_csv_stream_processor_t *p);

/**
 * @brief Get raw column name by index.
 * @param p Processor handle.
 * @param idx Column index.
 * @return Column name or NULL.
 */
TURBO_PARSER_API const char *turbo_csv_stream_processor_col_name(const turbo_csv_stream_processor_t *p,
                                                          size_t idx);

/**
 * @brief Resolve column index by name.
 * @param p Processor handle.
 * @param name Column name.
 * @return Column index or (size_t)-1.
 */
TURBO_PARSER_API size_t turbo_csv_stream_processor_col_index(const turbo_csv_stream_processor_t *p,
                                                      const char *name);

/**
 * @brief Get numeric column data.
 * @param p Processor handle.
 * @param col Column index.
 * @param out_len Receives length.
 * @return Pointer to internal double array or NULL.
 */
TURBO_PARSER_API const double *turbo_csv_stream_processor_col_data(const turbo_csv_stream_processor_t *p,
                                                            size_t col, size_t *out_len);

/**
 * @brief Get string value from matched row/column.
 * @param p Processor handle.
 * @param row Row index in matched set.
 * @param col Column index.
 * @return String pointer or NULL.
 */
TURBO_PARSER_API const char *turbo_csv_stream_processor_get_str(const turbo_csv_stream_processor_t *p,
                                                         size_t row, size_t col);

/**
 * @brief Get processor error text.
 * @param p Processor handle.
 * @return Error string.
 */
TURBO_PARSER_API const char *turbo_csv_stream_processor_error(const turbo_csv_stream_processor_t *p);


#ifdef __cplusplus
}
#endif
#endif // TURBO_PARSER_CSV_H
