#include "message_native_artifact_native.h"

#include <type_traits>

static_assert(std::is_standard_layout<DataBindMessageNativeArtifact>::value,
              "Message native artifact must remain C-compatible");
static_assert(std::is_standard_layout<DataBindNativeTypeBinding>::value,
              "native binding must remain C-compatible");
static_assert(std::is_standard_layout<Event_values_vec_t>::value,
              "generated scalar Vec must remain C-compatible");
static_assert(std::is_standard_layout<Event_labels_vec_t>::value,
              "generated string Vec must remain C-compatible");
static_assert(sizeof(Event_values_vec_t) > sizeof(vec_t),
              "typed CSTL Vec must not preserve raw vec_t storage ABI");
static_assert(sizeof(Event_labels_vec_t) > sizeof(vec_t),
              "typed CSTL string Vec must not preserve raw vec_t storage ABI");
static_assert(std::is_standard_layout<Event_ids_set_t>::value,
              "generated scalar Set must remain C-compatible");
static_assert(std::is_standard_layout<Event_tags_set_t>::value,
              "generated string Set must remain C-compatible");
static_assert(sizeof(Event_ids_set_t) > sizeof(set_t),
              "typed CSTL Set must not preserve raw set_t storage ABI");
static_assert(sizeof(Event_tags_set_t) > sizeof(set_t),
              "typed CSTL string Set must not preserve raw set_t storage ABI");

extern "C" size_t databind_message_native_artifact_c_values_vec_size(void);
extern "C" size_t databind_message_native_artifact_c_values_vec_raw_offset(void);
extern "C" size_t databind_message_native_artifact_c_labels_vec_size(void);
extern "C" size_t databind_message_native_artifact_c_labels_vec_raw_offset(void);
extern "C" size_t databind_message_native_artifact_c_ids_set_size(void);
extern "C" size_t databind_message_native_artifact_c_ids_set_raw_offset(void);
extern "C" size_t databind_message_native_artifact_c_tags_set_size(void);
extern "C" size_t databind_message_native_artifact_c_tags_set_raw_offset(void);

extern "C" int databind_message_native_artifact_cpp_probe(void) {
  const DataBindMessageNativeArtifact *artifact = Event_native_artifact();
  Event_t event{};

  if (!data_bind_message_native_artifact_valid(artifact)) return 1;
  if (databind_message_native_artifact_c_values_vec_size() !=
          sizeof(Event_values_vec_t) ||
      databind_message_native_artifact_c_values_vec_raw_offset() !=
          offsetof(Event_values_vec_t, raw) ||
      databind_message_native_artifact_c_labels_vec_size() !=
          sizeof(Event_labels_vec_t) ||
      databind_message_native_artifact_c_labels_vec_raw_offset() !=
          offsetof(Event_labels_vec_t, raw) ||
      databind_message_native_artifact_c_ids_set_size() !=
          sizeof(Event_ids_set_t) ||
      databind_message_native_artifact_c_ids_set_raw_offset() !=
          offsetof(Event_ids_set_t, raw) ||
      databind_message_native_artifact_c_tags_set_size() !=
          sizeof(Event_tags_set_t) ||
      databind_message_native_artifact_c_tags_set_raw_offset() !=
          offsetof(Event_tags_set_t, raw))
    return 4;

  Event_init(&event);
  if (event.values.cmeta.descriptor == nullptr ||
      event.values.raw.element_type == nullptr ||
      !cmeta_type_equal(event.values.raw.element_type, &cmeta_type_uint32) ||
      event.labels.cmeta.descriptor == nullptr ||
      event.labels.raw.element_type == nullptr ||
      !cmeta_type_equal(
          event.labels.raw.element_type, SALTS_TSTR_CMETA_TYPE_REF) ||
      event.ids.cmeta.descriptor == nullptr ||
      event.ids.raw.element_type == nullptr ||
      !cmeta_type_equal(event.ids.raw.element_type, &cmeta_type_uint32) ||
      event.tags.cmeta.descriptor == nullptr ||
      event.tags.raw.element_type == nullptr ||
      !cmeta_type_equal(
          event.tags.raw.element_type, SALTS_TSTR_CMETA_TYPE_REF)) {
    Event_clear(&event);
    return 2;
  }

  Event_clear(&event);
  return event.values.cmeta.descriptor == nullptr &&
                 event.labels.cmeta.descriptor == nullptr &&
                 event.ids.cmeta.descriptor == nullptr &&
                 event.tags.cmeta.descriptor == nullptr
             ? 0
             : 3;
}
