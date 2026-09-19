#include "data_bind_stream_format_state_internal.h"

#include <stdlib.h>
#include <string.h>

void data_bind_stream_format_state_init(
    data_bind_stream_format_state *state,
    int is_csv,
    int json_stream_candidate,
    int json_path_stream_mode,
    int xml_stream_candidate) {
  if (state == NULL) return;
  memset(state, 0, sizeof(*state));
  state->is_csv = is_csv;
  state->json_stream_candidate = json_stream_candidate;
  state->json_path_stream_mode = json_path_stream_mode;
  state->xml_stream_candidate = xml_stream_candidate;
}

void data_bind_stream_format_state_cleanup(
    data_bind_stream_format_state *state) {
  size_t i;
  if (state == NULL) return;

  free(state->csv_header);
  free(state->csv_record);
  for (i = 0; i < state->csv_field_count; ++i)
    free(state->csv_field_storage[i]);
  free(state->csv_field);
  free(state->csv_fields);
  free(state->csv_field_storage);

  if (state->csv_filter != NULL) dsv_filter_destroy(state->csv_filter);
  if (state->json_path_stream != NULL)
    json_path_stream_destroy(state->json_path_stream);
  if (state->json_path_program != NULL)
    json_path_program_free(state->json_path_program);
  if (state->json_sax != NULL) json_sax_parser_destroy(state->json_sax);
  if (state->yaml_sax != NULL) cyaml_sax_parser_destroy(state->yaml_sax);
  if (state->xml_sax != NULL) salts_xml_sax_parser_destroy(state->xml_sax);

  if (state->json_match_value != NULL) {
    json_free(state->json_match_value);
  } else if (state->json_frame_count != 0u &&
             state->json_frames[0].value != NULL) {
    json_free(state->json_frames[0].value);
  }
  for (i = 0; i < state->json_frame_count; ++i)
    free(state->json_frames[i].pending_key);
  free(state->json_frames);

  free(state->xml_stream_target);
  tstr_free(state->xml_capture);

  if (state->csv_filter_doc != NULL) csv_free(state->csv_filter_doc);

  memset(state, 0, sizeof(*state));
}
