#ifndef DATA_BIND_STREAM_FORMAT_STATE_INTERNAL_H
#define DATA_BIND_STREAM_FORMAT_STATE_INTERNAL_H

#include <csv_parser.h>
#include <cyaml.h>
#include <dsv_filter.h>
#include <json_parser.h>
#include <xml_parser/xml_sax.h>

#include <tstr.h>
#include <vstr.h>

#include <stddef.h>

typedef struct data_bind_json_stream_frame {
  json_value_t *value;
  char *pending_key;
  int is_object;
} data_bind_json_stream_frame_t;

/*
 * Concrete incremental-parser ownership for the legacy DataBind stream facade.
 *
 * data_bind_stream_t owns exactly one of these as an opaque lifetime unit. The
 * next migration slice can move feed/finish dispatch behind provider ops
 * without changing the provider-neutral stream shell again.
 */
typedef struct data_bind_stream_format_state {
  char *csv_header;
  size_t csv_header_len;
  char *csv_record;
  size_t csv_record_len;
  size_t csv_record_capacity;
  char *csv_field;
  size_t csv_field_len;
  size_t csv_field_capacity;
  vstr *csv_fields;
  char **csv_field_storage;
  size_t csv_field_count;
  size_t csv_fields_capacity;
  csv_doc_t *csv_filter_doc;
  dsv_filter_t *csv_filter;

  json_sax_parser_t *json_sax;
  json_path_program_t *json_path_program;
  json_path_stream_t *json_path_stream;
  json_value_t *json_match_value;
  cyaml_sax_parser_t *yaml_sax;
  salts_xml_sax_parser_t *xml_sax;

  data_bind_json_stream_frame_t *json_frames;
  size_t json_frame_count;
  size_t json_frame_capacity;
  size_t json_sax_depth;

  char *xml_stream_target;
  tstr xml_capture;
  size_t xml_capture_depth;

  size_t csv_data_row;
  int csv_header_seen;
  int csv_in_quotes;
  int csv_quote_pending;
  int csv_skip_next_lf;
  int csv_failed;
  int sax_failed;
  int is_csv;

  int json_stream_candidate;
  int json_path_stream_mode;
  int json_stream_active;
  int json_stream_done;
  int json_root_seen;

  int xml_stream_candidate;
  int xml_capture_active;
  int xml_open_start;

  char stream_error[256];
} data_bind_stream_format_state;

void data_bind_stream_format_state_init(
    data_bind_stream_format_state *state,
    int is_csv,
    int json_stream_candidate,
    int json_path_stream_mode,
    int xml_stream_candidate);

void data_bind_stream_format_state_cleanup(
    data_bind_stream_format_state *state);

#endif /* DATA_BIND_STREAM_FORMAT_STATE_INTERNAL_H */
