#include "tinytest.h"

#include "message_native_artifact_native.h"

#include <stddef.h>
#include <string.h>

size_t databind_message_native_artifact_c_values_vec_size(void) {
  return sizeof(Event_values_vec_t);
}

size_t databind_message_native_artifact_c_values_vec_raw_offset(void) {
  return offsetof(Event_values_vec_t, raw);
}

size_t databind_message_native_artifact_c_labels_vec_size(void) {
  return sizeof(Event_labels_vec_t);
}

size_t databind_message_native_artifact_c_labels_vec_raw_offset(void) {
  return offsetof(Event_labels_vec_t, raw);
}

size_t databind_message_native_artifact_c_ids_set_size(void) {
  return sizeof(Event_ids_set_t);
}

size_t databind_message_native_artifact_c_ids_set_raw_offset(void) {
  return offsetof(Event_ids_set_t, raw);
}

size_t databind_message_native_artifact_c_tags_set_size(void) {
  return sizeof(Event_tags_set_t);
}

size_t databind_message_native_artifact_c_tags_set_raw_offset(void) {
  return offsetof(Event_tags_set_t, raw);
}

size_t databind_message_native_artifact_c_counters_map_size(void) {
  return sizeof(Event_counters_map_t);
}

size_t databind_message_native_artifact_c_counters_map_raw_offset(void) {
  return offsetof(Event_counters_map_t, raw);
}

size_t databind_message_native_artifact_c_aliases_map_size(void) {
  return sizeof(Event_aliases_map_t);
}

size_t databind_message_native_artifact_c_aliases_map_raw_offset(void) {
  return offsetof(Event_aliases_map_t, raw);
}

spec("DataBind public Message native artifact") {
  it("publishes exact optional and nullable overlays") {
    const DataBindMessageNativeArtifact *artifact = Event_native_artifact();
    DataBindNativeTypeBinding binding = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *data = NULL;

    check_not_null(artifact);
    check_true(data_bind_message_native_artifact_valid(artifact));
    check_equal(artifact->type_name, "Event");
    check_not_null(artifact->native_binding);
    check_equal(artifact->native_binding(&binding, &error), DATA_BIND_OK);
    check_equal(Event_cmeta_data(&data, &error), DATA_BIND_OK);
    check_true(binding.data == data);
    check_equal(binding.idl_type_name, "Event");

    check_equal(binding.presence_count, (size_t)2u);
    check_not_null(binding.presence);
    check_equal(binding.presence[0].field_name, "sequence");
    check_equal(binding.presence[0].byte_offset, offsetof(Event_t, _presence));
    check_equal(binding.presence[0].bit, 0u);
    check_equal(binding.presence[1].field_name, "tri");
    check_equal(binding.presence[1].byte_offset, offsetof(Event_t, _presence));
    check_equal(binding.presence[1].bit, 1u);

    check_equal(binding.null_count, (size_t)2u);
    check_not_null(binding.nulls);
    check_equal(binding.nulls[0].field_name, "result");
    check_equal(binding.nulls[0].byte_offset, offsetof(Event_t, _nulls));
    check_equal(binding.nulls[0].bit, 0u);
    check_equal(binding.nulls[1].field_name, "tri");
    check_equal(binding.nulls[1].byte_offset, offsetof(Event_t, _nulls));
    check_equal(binding.nulls[1].bit, 1u);
  }

  it("uses canonical CMeta lifecycle and fails legacy typed conversion closed") {
    static const char json[] =
        "{\"id\":1,\"values\":[7],\"labels\":[\"alpha\"]}";
    Event_t event;
    DataBindError error = DATA_BIND_ERROR_INIT;
    tstr source = tstr_dup("owned");

    memset(&event, 0xa5, sizeof(event));
    Event_init(&event);
    check_equal(event._presence[0], (uint8_t)0u);
    check_equal(event._nulls[0], (uint8_t)0u);
    check_equal(Event_values_vec_t_size(&event.values), (size_t)0u);
    check_equal(Event_labels_vec_t_size(&event.labels), (size_t)0u);
    check_equal(Event_ids_set_t_size(&event.ids), (size_t)0u);
    check_equal(Event_tags_set_t_size(&event.tags), (size_t)0u);
    check_equal(Event_counters_map_t_size(&event.counters), (size_t)0u);
    check_equal(Event_aliases_map_t_size(&event.aliases), (size_t)0u);
    check_equal(Event_values_vec_t_push(&event.values, UINT32_C(11)), STL_OK);
    check_equal(Event_ids_set_t_add(&event.ids, UINT32_C(11)), STL_OK);
    check_not_null(source);
    if (source != NULL) {
      check_equal(Event_labels_vec_t_push(&event.labels, source), STL_OK);
      check_equal(Event_tags_set_t_add(&event.tags, source), STL_OK);
      check_equal(Event_counters_map_t_put(
                      &event.counters, source, UINT32_C(11)),
                  STL_OK);
      check_equal(Event_aliases_map_t_put(
                      &event.aliases, source, source),
                  STL_OK);
    }

    check_equal(
        Event_from_json(NULL, &event, json, sizeof(json) - 1u, &error),
        DATA_BIND_ERR_SCHEMA);
    check_contains(error.message, "Legacy typed conversion");
    check_equal(Event_values_vec_t_size(&event.values), (size_t)1u);
    check_equal(Event_ids_set_t_size(&event.ids), (size_t)1u);
    if (source != NULL) {
      check_equal(Event_labels_vec_t_size(&event.labels), (size_t)1u);
      check_equal(Event_tags_set_t_size(&event.tags), (size_t)1u);
      check_equal(Event_counters_map_t_size(&event.counters), (size_t)1u);
      check_equal(Event_aliases_map_t_size(&event.aliases), (size_t)1u);
    }

    event._presence[0] = UINT8_C(0xff);
    event._nulls[0] = UINT8_C(0xff);
    Event_clear(&event);
    check_equal(event._presence[0], (uint8_t)0u);
    check_equal(event._nulls[0], (uint8_t)0u);
    check_equal(Event_values_vec_t_size(&event.values), (size_t)0u);
    check_equal(Event_labels_vec_t_size(&event.labels), (size_t)0u);
    check_equal(Event_ids_set_t_size(&event.ids), (size_t)0u);
    check_equal(Event_tags_set_t_size(&event.tags), (size_t)0u);
    check_equal(Event_counters_map_t_size(&event.counters), (size_t)0u);
    check_equal(Event_aliases_map_t_size(&event.aliases), (size_t)0u);
    check_not_null(source);
    if (source != NULL) {
      check_equal(tstr_len(source), (size_t)5u);
      tstr_free(source);
    }
  }

  it("publishes typed CSTL map providers for required scalar/string maps") {
    Event_counters_map_t counters = {0};
    Event_aliases_map_t aliases = {0};
    tstr alpha = tstr_dup("alpha");
    tstr beta = tstr_dup("beta");
    tstr label = tstr_dup("label");
    const uint32_t *counter = NULL;
    const tstr *stored_label = NULL;
    cmeta_data_map_borrow_cursor cursor = {0};
    const void *borrowed_key = NULL;
    const void *borrowed_value = NULL;
    const cmeta_data_desc *counter_key =
        cmeta_data_map_key_data(&Event_counters_map_t_map_data);
    const cmeta_data_desc *counter_value =
        cmeta_data_map_value_data(&Event_counters_map_t_map_data);
    const cmeta_data_desc *alias_key =
        cmeta_data_map_key_data(&Event_aliases_map_t_map_data);
    const cmeta_data_desc *alias_value =
        cmeta_data_map_value_data(&Event_aliases_map_t_map_data);

    check_equal(Event_counters_map_t_map_data.kind, CMETA_DATA_MAP);
    check_equal(Event_aliases_map_t_map_data.kind, CMETA_DATA_MAP);
    check_true(cmeta_data_desc_equal(counter_key, SALTS_TSTR_CMETA_DATA_REF));
    check_true(cmeta_data_desc_equal(counter_value, &cmeta_data_uint32));
    check_true(cmeta_data_desc_equal(alias_key, SALTS_TSTR_CMETA_DATA_REF));
    check_true(cmeta_data_desc_equal(alias_value, SALTS_TSTR_CMETA_DATA_REF));

    check_not_null(alpha);
    check_not_null(beta);
    check_not_null(label);
    if (alpha != NULL && beta != NULL) {
      /* Insert reverse lexical order; canonical Map iteration is key-sorted. */
      check_equal(Event_counters_map_t_put(
                      &counters, beta, UINT32_C(20)),
                  STL_OK);
      check_equal(Event_counters_map_t_put(
                      &counters, alpha, UINT32_C(10)),
                  STL_OK);
      check_equal(Event_counters_map_t_size(&counters), (size_t)2u);
      counter = Event_counters_map_t_get_const(&counters, alpha);
      check_not_null(counter);
      if (counter != NULL) check_equal(*counter, UINT32_C(10));

      check_equal(
          cmeta_data_map_borrow_begin(
              &Event_counters_map_t_map_data, &counters, &cursor),
          CMETA_OK);
      check_equal(
          cmeta_data_map_borrow_next(
              &cursor, &borrowed_key, &borrowed_value),
          CMETA_GEN_VALUE);
      check_not_null(borrowed_key);
      check_not_null(borrowed_value);
      if (borrowed_key != NULL) {
        const tstr *key = (const tstr *)borrowed_key;
        check_not_null(*key);
        if (*key != NULL) {
          check_equal(tstr_len(*key), (size_t)5u);
          check_equal(memcmp(*key, "alpha", 5u), 0);
        }
      }
      if (borrowed_value != NULL)
        check_equal(*(const uint32_t *)borrowed_value, UINT32_C(10));
    }

    if (alpha != NULL && label != NULL) {
      check_equal(Event_aliases_map_t_put(&aliases, alpha, label), STL_OK);
      stored_label = Event_aliases_map_t_get_const(&aliases, alpha);
      check_not_null(stored_label);
      if (stored_label != NULL) {
        check_not_null(*stored_label);
        check_true(*stored_label != label);
        if (*stored_label != NULL)
          check_equal(tstr_len(*stored_label), tstr_len(label));
      }
    }

    Event_counters_map_t_destroy(&counters);
    Event_aliases_map_t_destroy(&aliases);
    if (alpha != NULL) check_equal(tstr_len(alpha), (size_t)5u);
    if (beta != NULL) check_equal(tstr_len(beta), (size_t)4u);
    if (label != NULL) check_equal(tstr_len(label), (size_t)5u);
    tstr_free(alpha);
    tstr_free(beta);
    tstr_free(label);
  }

  it("publishes typed CSTL set providers for required scalar/string sets") {
    Event_ids_set_t ids = {0};
    Event_tags_set_t tags = {0};
    tstr source = tstr_dup("alpha");
    cmeta_data_collection_borrow_cursor cursor = {0};
    const void *borrowed = NULL;
    const tstr *stored = NULL;
    const cmeta_data_desc *id_element =
        cmeta_data_collection_element_data(&Event_ids_set_t_collection_data);
    const cmeta_data_desc *tag_element =
        cmeta_data_collection_element_data(&Event_tags_set_t_collection_data);

    check_equal(Event_ids_set_t_collection_data.kind, CMETA_DATA_SET);
    check_equal(Event_tags_set_t_collection_data.kind, CMETA_DATA_SET);
    check_true(cmeta_data_desc_equal(id_element, &cmeta_data_uint32));
    check_true(cmeta_data_desc_equal(tag_element, SALTS_TSTR_CMETA_DATA_REF));

    check_equal(Event_ids_set_t_init(&ids, 4u), STL_OK);
    check_equal(Event_ids_set_t_add(&ids, UINT32_C(7)), STL_OK);
    check_true(Event_ids_set_t_contains(&ids, UINT32_C(7)));
    check_equal(Event_ids_set_t_size(&ids), (size_t)1u);

    check_not_null(source);
    if (source != NULL) {
      check_equal(Event_tags_set_t_init(&tags, 4u), STL_OK);
      check_equal(Event_tags_set_t_add(&tags, source), STL_OK);
      check_true(Event_tags_set_t_contains(&tags, source));
      check_equal(Event_tags_set_t_size(&tags), (size_t)1u);
      check_equal(
          cmeta_data_collection_borrow_begin(
              &Event_tags_set_t_collection_data, &tags, &cursor),
          CMETA_OK);
      {
        cmeta_gen_status generated =
            cmeta_data_collection_borrow_next(&cursor, &borrowed);
        const void *terminal = (const void *)(uintptr_t)1u;
        check_true(generated == CMETA_GEN_VALUE ||
                   generated == CMETA_GEN_VALUE_AND_DONE);
        stored = (const tstr *)borrowed;
        if (generated == CMETA_GEN_VALUE) {
          check_equal(
              cmeta_data_collection_borrow_next(&cursor, &terminal),
              CMETA_GEN_DONE);
          check_null(terminal);
        }
      }
      check_not_null(stored);
      if (stored != NULL) {
        check_not_null(*stored);
        check_true(*stored != source);
        check_equal(tstr_len(*stored), tstr_len(source));
      }
    }

    Event_ids_set_t_destroy(&ids);
    Event_tags_set_t_destroy(&tags);
    tstr_free(source);
  }

  it("publishes typed CSTL sequence providers for required scalar/string lists") {
    Event_values_vec_t values = {0};
    Event_labels_vec_t labels = {0};
    tstr source = tstr_dup("alpha");
    const cmeta_data_desc *value_element =
        cmeta_data_collection_element_data(&Event_values_vec_t_collection_data);
    const cmeta_data_desc *label_element =
        cmeta_data_collection_element_data(&Event_labels_vec_t_collection_data);

    check_true(cmeta_data_desc_equal(value_element, &cmeta_data_uint32));
    check_true(cmeta_data_desc_equal(label_element, SALTS_TSTR_CMETA_DATA_REF));
    check_equal(Event_values_vec_t_init(&values, 4u), STL_OK);
    check_equal(Event_values_vec_t_push(&values, UINT32_C(7)), STL_OK);
    check_equal(Event_values_vec_t_size(&values), (size_t)1u);

    check_not_null(source);
    if (source != NULL) {
      check_equal(Event_labels_vec_t_init(&labels, 4u), STL_OK);
      check_equal(Event_labels_vec_t_push(&labels, source), STL_OK);
      check_equal(Event_labels_vec_t_size(&labels), (size_t)1u);
      check_not_null(*Event_labels_vec_t_at_const(&labels, 0u));
      check_true(*Event_labels_vec_t_at_const(&labels, 0u) != source);
    }

    Event_values_vec_t_destroy(&values);
    Event_labels_vec_t_destroy(&labels);
    tstr_free(source);
  }
}
