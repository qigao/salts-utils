#include "optional_scalar_lifecycle_generated.h"
#include "tinytest.h"

#include <string.h>

spec("optional local lifecycle uses canonical CMeta") {
  it("releases owned overlays through nested records regardless of logical state") {
    enum { PAYLOAD_SIZE = 16, REUSED_PAYLOAD_SIZE = 8 };
    OverlayEnvelope_t value;
    memset(&value, 0xa5, sizeof(value));
    OverlayEnvelope_init(&value);
    check_null(value.child.text.label);
    check_null(value.child.text.note);
    check_null(value.note);
    check_equal(stl_byte_buffer_size(&value.child.bytes.payload), (size_t)0u);
    check_equal(value.child.text._presence[0], 0u);
    check_equal(value.child.text._nulls[0], 0u);
    check_equal(value.child.bytes._presence[0], 0u);
    check_equal(value.child.bytes._nulls[0], 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    value.child.text.label = tstr_dup("absent owned label");
    value.child.text.note = tstr_dup("null owned note");
    value.note = tstr_dup("outer owned note");
    check_not_null(value.child.text.label);
    check_not_null(value.child.text.note);
    check_not_null(value.note);
    check_equal(stl_byte_buffer_resize(&value.child.bytes.payload, PAYLOAD_SIZE), STL_OK);
    check_equal(stl_byte_buffer_resize(&value.child.bytes.note, PAYLOAD_SIZE), STL_OK);
    value.child.text._nulls[0] = 1u;
    value.child.bytes._nulls[0] = 1u;
    value._nulls[0] = 1u;
    value.child.text.count = 7u;
    value.child.bytes.count = 9u;
    /* Absence/null affects semantic state, never ownership of native storage. */
    OverlayEnvelope_clear(&value);
    check_null(value.child.text.label);
    check_null(value.child.text.note);
    check_null(value.note);
    check_equal(stl_byte_buffer_size(&value.child.bytes.payload), (size_t)0u);
    check_equal(stl_byte_buffer_size(&value.child.bytes.note), (size_t)0u);
    check_equal(value.child.text.count, 0u);
    check_equal(value.child.bytes.count, 0u);
    check_equal(value.child.text._presence[0], 0u);
    check_equal(value.child.text._nulls[0], 0u);
    check_equal(value.child.bytes._presence[0], 0u);
    check_equal(value.child.bytes._nulls[0], 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
    OverlayEnvelope_clear(&value);

    OverlayEnvelope_init(&value);
    value.child.text.label = tstr_dup("reused owner");
    check_not_null(value.child.text.label);
    check_equal(stl_byte_buffer_resize(&value.child.bytes.payload, REUSED_PAYLOAD_SIZE), STL_OK);
    OverlayEnvelope_clear(&value);
    OverlayEnvelope_init(NULL);
    OverlayEnvelope_clear(NULL);
  }

  it("keeps nested overlay conversions closed without mutating owned storage") {
    OverlayHolder_t value;
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    const cmeta_data_desc *data = NULL;
    static const char json[] = "{}";
    char *output = (char *)&value;
    size_t output_size = sizeof(value);
    tstr retained;
    OverlayHolder_init(&value);
    check_equal(OverlayHolder_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    value.text.label = tstr_dup("keep ownership on rejection");
    retained = value.text.label;
    check_not_null(retained);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    if (codec != NULL) {
      check_equal(OverlayHolder_from_json(codec, &value, json, sizeof(json) - 1u,
                                         &error), DATA_BIND_ERR_SCHEMA);
      check(value.text.label == retained);
      check_equal(OverlayHolder_to_json(codec, &value, &output, &output_size,
                                       &error), DATA_BIND_ERR_SCHEMA);
      check_null(output);
      check_equal(output_size, (size_t)0u);
      check(value.text.label == retained);
    }
    OverlayHolder_clear(&value);
    data_bind_free(codec);
  }

  it("initializes and clears UUID storage and state overlays") {
    UuidState_t value;
    memset(&value, 0xa5, sizeof(value));
    UuidState_init(&value);
    for (size_t i = 0u; i < sizeof(value.optional_value.bytes); ++i) {
      check_equal(value.optional_value.bytes[i], (uint8_t)0u);
      check_equal(value.nullable_value.bytes[i], (uint8_t)0u);
      check_equal(value.tri_value.bytes[i], (uint8_t)0u);
    }
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    memset(&value.optional_value, 0x5a, sizeof(value.optional_value));
    memset(&value.nullable_value, 0x5a, sizeof(value.nullable_value));
    memset(&value.tri_value, 0x5a, sizeof(value.tri_value));
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    UuidState_clear(&value);
    for (size_t i = 0u; i < sizeof(value.optional_value.bytes); ++i) {
      check_equal(value.optional_value.bytes[i], (uint8_t)0u);
      check_equal(value.nullable_value.bytes[i], (uint8_t)0u);
      check_equal(value.tri_value.bytes[i], (uint8_t)0u);
    }
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
  }

  it("initializes and clears enum storage and state overlays") {
    EnumState_t value;
    memset(&value, 0xa5, sizeof(value));
    EnumState_init(&value);
    check_equal(value.optional_value, State_Idle);
    check_equal(value.nullable_value, State_Idle);
    check_equal(value.tri_value, State_Idle);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    value.optional_value = State_Active;
    value.nullable_value = State_Active;
    value.tri_value = State_Active;
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    EnumState_clear(&value);
    check_equal(value.optional_value, State_Idle);
    check_equal(value.nullable_value, State_Idle);
    check_equal(value.tri_value, State_Idle);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
  }

  it("initializes and clears scalar storage and state overlays") {
    ScalarState_t value;
    memset(&value, 0xa5, sizeof(value));
    ScalarState_init(&value);
    check_equal(value.optional_value, 0);
    check_equal(value.nullable_value, 0u);
    check_equal(value.tri_value, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    value.optional_value = 7;
    value.nullable_value = 8u;
    value.tri_value = 9u;
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    ScalarState_clear(&value);
    check_equal(value.optional_value, 0);
    check_equal(value.nullable_value, 0u);
    check_equal(value.tri_value, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
  }

  it("initializes and releases owned strings with local state overlays") {
    OwnedState_t value;
    const cmeta_data_desc *data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    memset(&value, 0xa5, sizeof(value));
    check_equal(OwnedState_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (data != NULL) {
      check_equal(cmeta_data_value_init_zero(data, &value), CMETA_OK);
      check_equal(cmeta_data_value_restore_zero(data, &value), CMETA_OK);
    }
    OwnedState_init(&value);
    check_null(value.label);
    check_null(value.note);
    check_null(value.tri_value);
    check_equal(value.count, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    value.label = tstr_dup("label");
    value.note = tstr_dup("note");
    value.tri_value = tstr_dup("tri");
    check_not_null(value.label);
    check_not_null(value.note);
    check_not_null(value.tri_value);
    value.count = 17u;
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    OwnedState_clear(&value);
    check_null(value.label);
    check_null(value.note);
    check_null(value.tri_value);
    check_equal(value.count, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    value.label = tstr_dup("direct-restore");
    check_not_null(value.label);
    if (data != NULL && value.label != NULL)
      check_equal(cmeta_data_value_restore_zero(data, &value), CMETA_OK);
    OwnedState_clear(&value);
    check_null(value.label);
  }

  it("initializes and releases owned byte buffers with local state overlays") {
    OwnedBytesState_t value;
    const cmeta_data_desc *data = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    memset(&value, 0xa5, sizeof(value));
    check_equal(OwnedBytesState_cmeta_data(&data, &error), DATA_BIND_OK);
    check_not_null(data);
    if (data != NULL) {
      check_equal(cmeta_data_value_init_zero(data, &value), CMETA_OK);
      check_equal(cmeta_data_value_restore_zero(data, &value), CMETA_OK);
    }
    OwnedBytesState_init(&value);
    check_equal(stl_byte_buffer_size(&value.payload), (size_t)0u);
    check_equal(stl_byte_buffer_size(&value.note), (size_t)0u);
    check_equal(stl_byte_buffer_size(&value.tri_value), (size_t)0u);
    check_equal(value.count, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);

    check_equal(stl_byte_buffer_resize(&value.payload, 16u), STL_OK);
    check_equal(stl_byte_buffer_resize(&value.note, 17u), STL_OK);
    check_equal(stl_byte_buffer_resize(&value.tri_value, 18u), STL_OK);
    value.count = 19u;
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    OwnedBytesState_clear(&value);
    check_equal(stl_byte_buffer_size(&value.payload), (size_t)0u);
    check_equal(stl_byte_buffer_size(&value.note), (size_t)0u);
    check_equal(stl_byte_buffer_size(&value.tri_value), (size_t)0u);
    check_equal(value.count, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
  }

  it("uses canonical CSTL lifecycle for an optional map") {
    MapState_t value;
    memset(&value, 0xa5, sizeof(value));
    MapState_init(&value);
    check_equal(MapState_attrs_map_t_size(&value.attrs), (size_t)0u);
    check_equal(value._presence[0], 0u);
    MapState_clear(&value);
    check_equal(MapState_attrs_map_t_size(&value.attrs), (size_t)0u);
    check_equal(value._presence[0], 0u);
  }

  it("initializes fixed byte arrays and their state overlays") {
    FixedBytesState_t value;
    memset(&value, 0xa5, sizeof(value));
    FixedBytesState_init(&value);
    for (size_t i = 0u; i < sizeof(value.payload); ++i)
      check_equal(value.payload[i], (uint8_t)0u);
    for (size_t i = 0u; i < sizeof(value.note); ++i)
      check_equal(value.note[i], (uint8_t)0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
    memset(value.payload, 0x5a, sizeof(value.payload));
    memset(value.note, 0x5a, sizeof(value.note));
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    FixedBytesState_clear(&value);
    for (size_t i = 0u; i < sizeof(value.payload); ++i)
      check_equal(value.payload[i], (uint8_t)0u);
    for (size_t i = 0u; i < sizeof(value.note); ++i)
      check_equal(value.note[i], (uint8_t)0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
  }

  it("uses CMeta lifecycle for nested canonical messages") {
    NestedHolder_t value;
    memset(&value, 0xa5, sizeof(value));
    NestedHolder_init(&value);
    check_equal(value.child.id, 0u);
    value.child.id = 23u;
    NestedHolder_clear(&value);
    check_equal(value.child.id, 0u);
  }

  it("decodes a nested message through the canonical MessagePlan") {
    static const char json[] = "{\"child\":{\"id\":23}}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    NestedHolder_t value;

    NestedHolder_init(&value);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(NestedHolder_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(value.child.id, 23u);
    }
    NestedHolder_clear(&value);
    data_bind_free(codec);
  }

  it("decodes a nested composite through the canonical MessagePlan") {
    static const char json[] = "{\"child\":{\"id\":29}}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    CompositeHolder_t value;

    CompositeHolder_init(&value);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(CompositeHolder_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(value.child.id, 29u);
    }
    CompositeHolder_clear(&value);
    data_bind_free(codec);
  }

  it("decodes a provider-backed enum through the canonical MessagePlan") {
    static const char json[] = "{\"value\":7}";
    static const char invalid_json[] = "{\"value\":8}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    EnumHolder_t value;

    EnumHolder_init(&value);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(EnumHolder_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(value.value, State_Active);
      check(EnumHolder_from_json(
                codec, &value, invalid_json, sizeof(invalid_json) - 1u,
                &error) != DATA_BIND_OK);
      check_equal(value.value, State_Active);
    }
    EnumHolder_clear(&value);
    data_bind_free(codec);
  }

  it("matches signed enum and flags domains through canonical providers") {
    static const char signed_json[] = "{\"value\":-1}";
    static const char flags_json[] = "{\"value\":3}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    SignedEnumHolder_t signed_value;
    FlagsHolder_t flags_value;

    SignedEnumHolder_init(&signed_value);
    FlagsHolder_init(&flags_value);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(SignedEnumHolder_from_json(
                      codec, &signed_value, signed_json,
                      sizeof(signed_json) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(signed_value.value, SignedState_Negative);
      check_equal(FlagsHolder_from_json(
                      codec, &flags_value, flags_json,
                      sizeof(flags_json) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(flags_value.value, Permissions_Read | Permissions_Write);
    }
    SignedEnumHolder_clear(&signed_value);
    FlagsHolder_clear(&flags_value);
    data_bind_free(codec);
  }

  it("preserves the full-width unsigned enum value") {
    static const char json[] = "{\"value\":18446744073709551615}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    WideEnumHolder_t value;

    WideEnumHolder_init(&value);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(WideEnumHolder_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(value.value, WideState_Maximum);
    }
    WideEnumHolder_clear(&value);
    data_bind_free(codec);
  }

  it("uses the declared default enum storage domain") {
    static const char json[] = "{\"value\":2}";
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DefaultEnumHolder_t value;

    DefaultEnumHolder_init(&value);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(DefaultEnumHolder_from_json(
                      codec, &value, json, sizeof(json) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(value.value, DefaultState_Two);
    }
    DefaultEnumHolder_clear(&value);
    data_bind_free(codec);
  }

  it("releases owned child storage through local CMeta overlay lifecycle") {
    NestedOwnedState_t value;
    memset(&value, 0xa5, sizeof(value));
    NestedOwnedState_init(&value);
    check_null(value.child.label);
    check_equal(value.note, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
    value.child.label = tstr_dup("nested-owned");
    check_not_null(value.child.label);
    value.note = 23u;
    value._presence[0] = 0xffu;
    value._nulls[0] = 0xffu;
    NestedOwnedState_clear(&value);
    check_null(value.child.label);
    check_equal(value.note, 0u);
    check_equal(value._presence[0], 0u);
    check_equal(value._nulls[0], 0u);
  }

  it("preserves absent null and value state through canonical JSON conversion") {
    static const char input[] =
        "{\"nullable_value\":null,\"tri_value\":9}";
    DataBind *codec = NULL;
    ScalarState_t value;
    char *json = NULL;
    size_t json_len = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    ScalarState_init(&value);
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(ScalarState_from_json(
                      codec, &value, input, sizeof(input) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(value.optional_value, 0);
      check_equal(value.nullable_value, 0u);
      check_equal(value.tri_value, 9u);
      check_equal(value._presence[0], (uint8_t)(1u << ScalarState_OPTIONAL_tri_value));
      check_equal(value._nulls[0], (uint8_t)(1u << ScalarState_NULLABLE_nullable_value));
      check_equal(ScalarState_to_json(
                      codec, &value, &json, &json_len, &error),
                  DATA_BIND_OK);
      check_equal(json_len, sizeof(input) - 1u);
      check_equal(json, input);
    }
    data_bind_serialized_free(json);
    ScalarState_clear(&value);
    data_bind_free(codec);
  }

  it("retains owned strings on rejection and replaces them after complete validation") {
    static const char input[] = "{\"count\":5,\"label\":\"new\"}";
    static const char complete[] = "{\"count\":5,\"label\":\"new\",\"note\":null}";
    DataBind *codec = NULL;
    OwnedState_t value;
    tstr original;
    char *json = NULL;
    size_t json_len = 0u;
    DataBindError error = DATA_BIND_ERROR_INIT;

    OwnedState_init(&value);
    value.label = tstr_dup("keep");
    check_not_null(value.label);
    original = value.label;
    check_equal(ScalarLifecycle_codec_create(&codec, &error), DATA_BIND_OK);
    if (codec != NULL && original != NULL) {
      check_equal(OwnedState_from_json(
                      codec, &value, input, sizeof(input) - 1u, &error),
                  DATA_BIND_ERR_TYPE_NOT_FOUND);
      check(value.label == original);
      check_equal(value.label, "keep");
      check_equal(OwnedState_from_json(codec, &value, complete, sizeof(complete) - 1u, &error),
                  DATA_BIND_OK);
      check_equal(value.label, "new");
      check_null(value.note);
      check_equal(value.count, 5u);
      check_equal(value._presence[0], (uint8_t)(1u << OwnedState_OPTIONAL_label));
      check_equal(value._nulls[0], (uint8_t)(1u << OwnedState_NULLABLE_note));
      check_equal(OwnedState_to_json(codec, &value, &json, &json_len, &error),
                  DATA_BIND_OK);
      check_equal(json_len, sizeof(complete) - 1u);
      check_equal(json, complete);
    }
    data_bind_serialized_free(json);
    OwnedState_clear(&value);
    OwnedState_clear(&value);
    data_bind_free(codec);
  }
}
