/* #99 direct-consumer requirements. This deliberately needs the production
 * DataBind header/implementation; no test decoder or replacement symbol. */
#include "data_bind_native.h"
#include "reader_probe.h"
#include <cmeta/struct.h>
#include <salts_cmeta_data.h>
#include <tinytest.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

enum {
  CONTRACT_WORKSPACE_BYTES = 4096,
  CONTRACT_MAX_DEPTH = 8,
  CONTRACT_MAX_ITEMS = 64,
  CONTRACT_MAX_OWNED_BYTES = 32
};

typedef union ContractWorkspace {
  max_align_t alignment;
  unsigned char bytes[CONTRACT_WORKSPACE_BYTES];
} ContractWorkspace;

Struct(ContractRow, (int, id), (tstr, name));
static const cmeta_type_identity row_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.native-reader.ContractRow");
static const cmeta_type_desc row_type = {
    .name = "ContractRow", .size = sizeof(ContractRow),
    .align = _Alignof(ContractRow), .kind = CMETA_T_OBJECT,
    .identity = &row_identity};
static cmeta_data_field_desc row_fields[2];
static cmeta_field_desc row_layout_fields[2];
static const cmeta_struct_desc row_layout = {
    "ContractRow", sizeof(ContractRow), _Alignof(ContractRow), row_layout_fields, 2u};
static const cmeta_data_struct_shape row_shape = {
    .layout = &row_layout, .fields = row_fields, .field_count = 2u};
static const cmeta_data_desc row_data = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-reader.ContractRow.data", .display_name = "ContractRow",
    .kind = CMETA_DATA_STRUCT, .storage_type = &row_type, .shape = &row_shape};

static ContractWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;
static NativeReaderProbe probe;
static cserde_reader reader;
static ContractRow row;
static tstr text;

static NativeReaderProbeStep string_token(const char *value) {
  return native_reader_probe_slice(CSERDE_STRING, (const unsigned char *)value,
                                  strlen(value), CSERDE_VIEW_TRANSIENT);
}

static void open_source(const NativeReaderProbeStep *steps, size_t count) {
  check_equal(native_reader_probe_open(&probe, steps, count, &reader), CSERDE_OK);
  check_equal(probe.calls, 0u);
}

static DataBindStatus decode(const cmeta_data_desc *data, void *out, size_t capacity) {
  return data_bind_native_decode(&options, data, &reader, out, capacity, &diagnostic);
}

static void check_empty_row(void) {
  check_equal(row.id, 0);
  check_true(salts_tstr_cmeta_buffer_ops.is_zero(&row.name));
}

spec("DataBind direct reader to native contract") {
  before_each() {
    memset(&workspace, 0, sizeof(workspace));
    memset(&probe, 0, sizeof(probe));
    memset(&reader, 0, sizeof(reader));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = CONTRACT_MAX_DEPTH;
    options.max_items = CONTRACT_MAX_ITEMS;
    options.max_owned_bytes = CONTRACT_MAX_OWNED_BYTES;
    row.id = 0;
    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data, &row.name), CMETA_OK);
    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data, &text), CMETA_OK);
    /* Preserve generated offsets; supply the explicit provider type that the
     * generic Struct macro cannot infer for tstr. No alternate type identity. */
    row_layout_fields[0] = StructMeta(ContractRow)->fields[0];
    row_layout_fields[1] = StructMeta(ContractRow)->fields[1];
    row_layout_fields[1].type = salts_tstr_cmeta_data.storage_type;
    /* Imported Salts metadata is bound at runtime, also on Windows. */
    row_fields[0] = (cmeta_data_field_desc){"row.id", "id", offsetof(ContractRow, id), &cmeta_data_int};
    row_fields[1] = (cmeta_data_field_desc){"row.name", "name", offsetof(ContractRow, name), &salts_tstr_cmeta_data};
    check_true(cmeta_data_desc_valid(&row_data));
  }
  after_each() {
    salts_tstr_cmeta_buffer_ops.restore_zero(&row.name);
    salts_tstr_cmeta_buffer_ops.restore_zero(&text);
  }

  it("consumes two scalar values separately without an extra EOF read") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7), native_reader_probe_sint(-9)};
    int first = 0, second = 0;
    open_source(steps, 2u);
    check_equal(decode(&cmeta_data_int, &first, sizeof(first)), DATA_BIND_OK);
    check_equal(first, 7);
    check_equal(probe.calls, 1u);
    check_equal(reader.state, CSERDE_READER_READY);
    check_equal(decode(&cmeta_data_int, &second, sizeof(second)), DATA_BIND_OK);
    check_equal(second, -9);
    check_equal(probe.calls, 2u);
    check_equal(diagnostic.error.code, DATA_BIND_OK);
    check_equal(diagnostic.source_status, CSERDE_OK);
  }

  it("binds a schema-free Struct by canonical field names and owns transient text") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN), string_token("name"), string_token("Alice"),
        string_token("id"), native_reader_probe_sint(7), native_reader_probe_token(CSERDE_MAP_END),
        native_reader_probe_sint(99)};
    cserde_token next = {0};
    open_source(steps, 7u);
    check_equal(decode(&row_data, &row, sizeof(row)), DATA_BIND_OK);
    check_equal(row.id, 7);
    check_equal(tstr_len(row.name), (size_t)5u);
    check_equal(memcmp(row.name, "Alice", 5u), 0);
    check_equal(probe.calls, 6u);
    check_equal(cserde_reader_next(&reader, &next), CSERDE_OK);
    check_equal(next.value.sint, INT64_C(99));
    check_equal(memcmp(row.name, "Alice", 5u), 0);
  }

  it("rejects incompatible option ABI before reading or changing output") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int value = 0;
    open_source(steps, 1u);
    ++options.abi_version;
    check_equal(decode(&cmeta_data_int, &value, sizeof(value)), DATA_BIND_ERR_INVALID_ARG);
    check_equal(probe.calls, 0u);
    check_equal(value, 0);
  }

  it("rejects a truncated options record before reading input") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int value = 0;
    open_source(steps, 1u);
    options.size = offsetof(DataBindNativeOptions, workspace_bytes);
    check_equal(decode(&cmeta_data_int, &value, sizeof(value)), DATA_BIND_ERR_INVALID_ARG);
    check_equal(probe.calls, 0u);
    check_equal(value, 0);
  }

  it("rejects insufficient output storage before reading input") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int value = 0;
    open_source(steps, 1u);
    check_equal(decode(&cmeta_data_int, &value, sizeof(value) - 1u), DATA_BIND_ERR_INVALID_ARG);
    check_equal(probe.calls, 0u);
    check_equal(value, 0);
  }

  it("rejects exhausted workspace before reading or publishing a scalar") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int value = 0;
    open_source(steps, 1u);
    options.workspace_bytes = 0u;
    check_equal(decode(&cmeta_data_int, &value, sizeof(value)), DATA_BIND_ERR_LIMIT);
    check_equal(probe.calls, 0u);
    check_equal(value, 0);
  }

  it("rejects a nonempty destination rather than replacing it") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int value = 123;
    open_source(steps, 1u);
    check_equal(decode(&cmeta_data_int, &value, sizeof(value)), DATA_BIND_ERR_INVALID_ARG);
    check_equal(probe.calls, 0u);
    check_equal(value, 123);
  }

  it("rejects workspace overlapping destination before any input callback") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int value = 0;
    open_source(steps, 1u);
    options.workspace = &value;
    options.workspace_bytes = sizeof(value);
    check_equal(decode(&cmeta_data_int, &value, sizeof(value)), DATA_BIND_ERR_INVALID_ARG);
    check_equal(probe.calls, 0u);
    check_equal(value, 0);
  }

  it("rejects integer overflow without publishing a partial scalar") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(INT64_MAX)};
    int32_t value = 0;
    open_source(steps, 1u);
    check_equal(decode(&cmeta_data_int32, &value, sizeof(value)), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(probe.calls, 1u);
    check_equal(value, 0);
    check_equal(diagnostic.source_status, CSERDE_OK);
  }

  it("binds a Boolean token to native C bool without bool8 substitution") {
    NativeReaderProbeStep steps[] = {native_reader_probe_token(CSERDE_BOOL)};
    bool value = false;
    steps[0].token.value.boolean = true;
    open_source(steps, 1u);
    check_equal(decode(&cmeta_data_bool, &value, sizeof(value)), DATA_BIND_OK);
    check_true(value);
    check_equal(probe.calls, 1u);
  }

  it("preserves source failure separately from DataBind conversion status") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_error(CSERDE_SOURCE_ERROR)};
    int value = 0;
    open_source(steps, 1u);
    check_equal(decode(&cmeta_data_int, &value, sizeof(value)), DATA_BIND_ERR_IO);
    check_equal(diagnostic.source_status, CSERDE_SOURCE_ERROR);
    check_equal(diagnostic.error.code, DATA_BIND_ERR_IO);
    check_equal(probe.calls, 1u);
    check_equal(reader.state, CSERDE_READER_FAILED);
    check_equal(value, 0);
  }

  it("rolls back owned text on truncated input and leaves the destination empty") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN), string_token("name"), string_token("Alice")};
    open_source(steps, 3u);
    check_equal(decode(&row_data, &row, sizeof(row)), DATA_BIND_ERR_PARSE);
    check_equal(diagnostic.source_status, CSERDE_DONE);
    check_equal(probe.calls, 4u);
    check_empty_row();
  }

  it("rolls back a late field error and reuses the workspace with an explicit fresh reader") {
    const NativeReaderProbeStep invalid[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN), string_token("name"), string_token("Alice"),
        string_token("id"), string_token("not-an-integer"), native_reader_probe_token(CSERDE_MAP_END)};
    const NativeReaderProbeStep valid[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN), string_token("name"), string_token("Bob"),
        string_token("id"), native_reader_probe_sint(8), native_reader_probe_token(CSERDE_MAP_END)};
    open_source(invalid, 6u);
    check_equal(decode(&row_data, &row, sizeof(row)), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(probe.calls, 5u);
    check_not_null(strstr(diagnostic.error.path, "id"));
    check_empty_row();
    /* This source is deliberately abandoned by the caller, never rewound by DataBind. */
    reader = (cserde_reader){0};
    open_source(valid, 6u);
    check_equal(decode(&row_data, &row, sizeof(row)), DATA_BIND_OK);
    check_equal(row.id, 8);
    check_equal(tstr_len(row.name), (size_t)3u);
    check_equal(memcmp(row.name, "Bob", 3u), 0);
    check_equal(probe.calls, 6u);
  }

  it("enforces owned payload budget before publishing text") {
    const NativeReaderProbeStep steps[] = {string_token("four")};
    open_source(steps, 1u);
    options.max_owned_bytes = 3u;
    check_equal(decode(&salts_tstr_cmeta_data, &text, sizeof(text)), DATA_BIND_ERR_LIMIT);
    check_equal(probe.calls, 1u);
    check_true(salts_tstr_cmeta_buffer_ops.is_zero(&text));
  }

  it("rejects zero depth budget before any reader consumption") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int value = 0;
    open_source(steps, 1u);
    options.max_depth = 0u;
    check_equal(decode(&cmeta_data_int, &value, sizeof(value)), DATA_BIND_ERR_LIMIT);
    check_equal(probe.calls, 0u);
    check_equal(value, 0);
  }

  it("rejects duplicate fields without publishing either owned value") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN), string_token("name"), string_token("Alice"),
        string_token("name"), string_token("Bob"), native_reader_probe_token(CSERDE_MAP_END)};
    open_source(steps, 6u);
    check_equal(decode(&row_data, &row, sizeof(row)), DATA_BIND_ERR_PARSE);
    check_equal(probe.calls, 4u);
    check_empty_row();
  }

  it("rejects unknown fields without consuming their values") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN), string_token("extra"), native_reader_probe_sint(7)};
    open_source(steps, 3u);
    check_equal(decode(&row_data, &row, sizeof(row)), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(probe.calls, 2u);
    check_empty_row();
  }

  it("rejects a missing required field and rolls back earlier owned text") {
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_token(CSERDE_MAP_BEGIN), string_token("name"), string_token("Alice"),
        native_reader_probe_token(CSERDE_MAP_END)};
    open_source(steps, 4u);
    check_equal(decode(&row_data, &row, sizeof(row)), DATA_BIND_ERR_TYPE_MISMATCH);
    check_equal(probe.calls, 4u);
    check_empty_row();
  }

  it("keeps decoded owned bytes after the source invalidates its transient slice") {
    static const unsigned char bytes[] = {'A', 0u, 'B'};
    const NativeReaderProbeStep steps[] = {
        native_reader_probe_slice(CSERDE_BYTES, bytes, sizeof(bytes), CSERDE_VIEW_TRANSIENT)};
    cmeta_data_desc data = salts_tstr_cmeta_data;
    cserde_token next = {0};
    data.kind = CMETA_DATA_BYTES;
    open_source(steps, 1u);
    check_equal(decode(&data, &text, sizeof(text)), DATA_BIND_OK);
    check_equal(probe.calls, 1u);
    check_equal(cserde_reader_next(&reader, &next), CSERDE_DONE);
    check_equal(tstr_len(text), sizeof(bytes));
    check_equal(memcmp(text, bytes, sizeof(bytes)), 0);
  }

  it("does not silently coerce textual true into a canonical Boolean") {
    const NativeReaderProbeStep steps[] = {string_token("true")};
    bool value = false;
    open_source(steps, 1u);
    check_equal(decode(&cmeta_data_bool, &value, sizeof(value)), DATA_BIND_ERR_TYPE_MISMATCH);
    check_false(value);
    check_equal(probe.calls, 1u);
  }

  it("rejects an invalid descriptor before consuming input or touching output") {
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    cmeta_data_desc data = cmeta_data_int;
    int value = 0;
    data.storage_type = NULL;
    open_source(steps, 1u);
    check_equal(decode(&data, &value, sizeof(value)), DATA_BIND_ERR_SCHEMA);
    check_equal(probe.calls, 0u);
    check_equal(value, 0);
  }
}
