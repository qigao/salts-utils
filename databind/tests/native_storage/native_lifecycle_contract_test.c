/* #99 plain-CMeta lifecycle contract. This intentionally requires the
 * production lifecycle API; no local fallback or typed-descriptor adapter. */
#include "data_bind_native.h"

#include <cmeta/struct.h>
#include <salts_cmeta_data.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  LIFECYCLE_WORKSPACE_BYTES = 4096,
  LIFECYCLE_MAX_DEPTH = 8,
  LIFECYCLE_MAX_ITEMS = 64
};

typedef union LifecycleWorkspace {
  max_align_t alignment;
  unsigned char bytes[LIFECYCLE_WORKSPACE_BYTES];
} LifecycleWorkspace;

Struct(LifecycleRow, (int, id), (tstr, name));

static const cmeta_type_identity lifecycle_row_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.native-lifecycle.LifecycleRow");
static const cmeta_type_desc lifecycle_row_type = {
    .name = "LifecycleRow",
    .size = sizeof(LifecycleRow),
    .align = _Alignof(LifecycleRow),
    .kind = CMETA_T_OBJECT,
    .identity = &lifecycle_row_identity};
static cmeta_data_field_desc lifecycle_row_fields[2];
static cmeta_field_desc lifecycle_layout_fields[2];
static const cmeta_struct_desc lifecycle_layout = {
    "LifecycleRow", sizeof(LifecycleRow), _Alignof(LifecycleRow),
    lifecycle_layout_fields, 2u};
static const cmeta_data_struct_shape lifecycle_row_shape = {
    .layout = &lifecycle_layout,
    .fields = lifecycle_row_fields,
    .field_count = 2u};
static const cmeta_data_desc lifecycle_row_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.native-lifecycle.LifecycleRow.data",
    .display_name = "LifecycleRow",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &lifecycle_row_type,
    .shape = &lifecycle_row_shape};

typedef struct LifecyclePair {
  int32_t a;
  int32_t b;
} LifecyclePair;

static LifecycleWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;

static DataBindStatus lifecycle_init(const cmeta_data_desc *shape, void *destination,
                                     size_t bytes) {
  return data_bind_native_init(&options, shape, destination, bytes, &diagnostic);
}

static DataBindStatus lifecycle_clear(const cmeta_data_desc *shape, void *destination,
                                      size_t bytes) {
  return data_bind_native_clear(&options, shape, destination, bytes, &diagnostic);
}

static void bind_lifecycle_row_metadata(void) {
  lifecycle_layout_fields[0] = StructMeta(LifecycleRow)->fields[0];
  lifecycle_layout_fields[1] = StructMeta(LifecycleRow)->fields[1];
  lifecycle_layout_fields[1].type = salts_tstr_cmeta_data.storage_type;
  lifecycle_row_fields[0] =
      (cmeta_data_field_desc){"lifecycle.id", "id", offsetof(LifecycleRow, id),
                             &cmeta_data_int};
  lifecycle_row_fields[1] =
      (cmeta_data_field_desc){"lifecycle.name", "name", offsetof(LifecycleRow, name),
                             &salts_tstr_cmeta_data};
}

static void require_overlapping_layout_rejected(void) {
  LifecyclePair value = {17, 19};
  LifecyclePair before = value;
  const cmeta_type_identity identity =
      CMETA_TYPE_ID_ATOM_INIT("test.native-lifecycle.Pair");
  const cmeta_type_desc type = {
      .name = "LifecyclePair",
      .size = sizeof(value),
      .align = _Alignof(LifecyclePair),
      .kind = CMETA_T_OBJECT,
      .identity = &identity};
  const cmeta_field_desc fields[] = {
      {.name = "a", .type_name = "int32_t", .offset = offsetof(LifecyclePair, a),
       .size = sizeof(int32_t), .align = _Alignof(int32_t),
       .type = cmeta_data_int32.storage_type},
      {.name = "b", .type_name = "int32_t", .offset = offsetof(LifecyclePair, a),
       .size = sizeof(int32_t), .align = _Alignof(int32_t),
       .type = cmeta_data_int32.storage_type}};
  const cmeta_struct_desc layout = {
      "LifecyclePair", sizeof(value), _Alignof(LifecyclePair), fields, 2u};
  const cmeta_data_field_desc values[] = {
      {"lifecycle.a", "a", offsetof(LifecyclePair, a), &cmeta_data_int32},
      {"lifecycle.b", "b", offsetof(LifecyclePair, a), &cmeta_data_int32}};
  const cmeta_data_struct_shape record = {&layout, values, 2u};
  const cmeta_data_desc shape = {
      .struct_size = sizeof(cmeta_data_desc),
      .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.native-lifecycle.Pair.data",
      .display_name = "LifecyclePair",
      .kind = CMETA_DATA_STRUCT,
      .storage_type = &type,
      .shape = &record};

  check_true(cmeta_data_desc_valid(&shape));
  check_equal(lifecycle_init(&shape, &value, sizeof(value)), DATA_BIND_ERR_SCHEMA);
  check_equal(memcmp(&value, &before, sizeof(value)), 0);
}

spec("DataBind plain-CMeta native lifecycle") {
  before_each() {
    memset(&workspace, 0, sizeof(workspace));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = LIFECYCLE_MAX_DEPTH;
    options.max_items = LIFECYCLE_MAX_ITEMS;
    options.max_owned_bytes = 64u;
    bind_lifecycle_row_metadata();
    check_true(cmeta_data_desc_valid(&lifecycle_row_data));
  }

  it("initializes raw scalar storage to canonical semantic zero") {
    int value = 91;
    check_equal(lifecycle_init(&cmeta_data_int, &value, sizeof(value)), DATA_BIND_OK);
    check_equal(value, 0);
    check_equal(diagnostic.error.code, DATA_BIND_OK);
  }

  it("initializes a poisoned Struct including provider-defined tstr zero") {
    LifecycleRow row;
    memset(&row, 0xa5, sizeof(row));
    check_equal(lifecycle_init(&lifecycle_row_data, &row, sizeof(row)), DATA_BIND_OK);
    check_equal(row.id, 0);
    check_true(salts_tstr_cmeta_buffer_ops.is_zero(&row.name));
  }

  it("clears owned provider state and remains idempotent") {
    static const unsigned char name[] = {'A', 'l', 'i', 'c', 'e'};
    LifecycleRow row = {0};
    check_equal(cmeta_data_buffer_init_zero(&salts_tstr_cmeta_data, &row.name), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(&salts_tstr_cmeta_data, &row.name, name,
                                        sizeof(name), sizeof(name)), CMETA_OK);
    row.id = 7;
    check_equal(lifecycle_clear(&lifecycle_row_data, &row, sizeof(row)), DATA_BIND_OK);
    check_equal(row.id, 0);
    check_true(salts_tstr_cmeta_buffer_ops.is_zero(&row.name));
    check_equal(lifecycle_clear(&lifecycle_row_data, &row, sizeof(row)), DATA_BIND_OK);
    check_true(salts_tstr_cmeta_buffer_ops.is_zero(&row.name));
  }

  it("rejects scalar width mismatch before modifying raw storage") {
    cmeta_data_desc shape = cmeta_data_int32;
    const cmeta_data_integer_shape narrow = {8u};
    int32_t value = INT32_C(123456);
    shape.shape = &narrow;
    check_true(cmeta_data_desc_valid(&shape));
    check_equal(lifecycle_init(&shape, &value, sizeof(value)), DATA_BIND_ERR_SCHEMA);
    check_equal(value, INT32_C(123456));
  }

  it("rejects overlapping Struct fields before mutation") {
    require_overlapping_layout_rejected();
  }

  it("enforces descriptor item budget before mutation") {
    LifecycleRow row = {17, NULL};
    options.max_items = 1u;
    check_equal(lifecycle_init(&lifecycle_row_data, &row, sizeof(row)),
                DATA_BIND_ERR_LIMIT);
    check_equal(row.id, 17);
    check_null(row.name);
  }

  it("rejects workspace overlapping destination before mutation") {
    int *value = (int *)(void *)workspace.bytes;
    *value = 73;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    check_equal(lifecycle_init(&cmeta_data_int, value, sizeof(*value)),
                DATA_BIND_ERR_INVALID_ARG);
    check_equal(*value, 73);
  }

  it("does not write through a diagnostic that aliases destination") {
    DataBindNativeDiagnostic local = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    unsigned char before[sizeof(local)];
    memcpy(before, &local, sizeof(local));
    check_equal(data_bind_native_init(&options, &cmeta_data_bool, &local, sizeof(local),
                                     &local),
                DATA_BIND_ERR_INVALID_ARG);
    check_equal(memcmp(before, &local, sizeof(local)), 0);
  }
}
