#include "tinytest.h"
#include "turbo_parser_json.h"

#include <cbind/cbind.h>

#include <stddef.h>

#define JSON_CSERDE_DATA_PREFIX_SIZE                                                               \
  (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))

Struct(turbo_json_cserde_record, (int, value));

static const cmeta_type_identity turbo_json_cserde_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.turbo_parser.json_cserde.record");
static const cmeta_type_desc turbo_json_cserde_record_type = {
    .name = "turbo_json_cserde_record",
    .size = sizeof(turbo_json_cserde_record),
    .align = _Alignof(turbo_json_cserde_record),
    .kind = CMETA_T_OBJECT,
    .identity = &turbo_json_cserde_record_identity};
static const cmeta_data_field_desc turbo_json_cserde_record_fields[] = {
    {"test.turbo_parser.json_cserde.record.value", "value",
     offsetof(turbo_json_cserde_record, value), &cmeta_data_int}};
static const cmeta_data_struct_shape turbo_json_cserde_record_shape = {
    .layout = StructMeta(turbo_json_cserde_record),
    .fields = turbo_json_cserde_record_fields,
    .field_count = 1u};
static const cmeta_data_desc turbo_json_cserde_record_data = {
    .struct_size = JSON_CSERDE_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.turbo_parser.json_cserde.record.data",
    .display_name = "JSON reader record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &turbo_json_cserde_record_type,
    .shape = &turbo_json_cserde_record_shape};

spec("TurboParser JSON CSerde facade") {
  it("binds parsed JSON through the installed facade contract") {
    static const char json[] = "{\"value\":-42}";
    turbo_json_doc_t *root = NULL;
    cserde_reader *reader;
    unsigned char scratch[1] = {0};
    cbind_context context = CBIND_CONTEXT_INIT(scratch, sizeof(scratch), 1u);
    cbind_error error = CBIND_ERROR_INIT;
    turbo_json_cserde_record value = {0};

    check_equal(turbo_parse_json((const uint8_t *)json, sizeof(json) - 1u, &root), 0);
    reader = turbo_json_cserde_reader_create(root, 1u);
    check_not_null(reader);
    if (reader != NULL)
      check_equal(cbind_decode(&context, &turbo_json_cserde_record_data, reader, &value, &error),
                  CBIND_OK);
    check_equal(value.value, -42);

    turbo_json_cserde_reader_destroy(reader);
    turbo_free_json(&root);
  }
}
