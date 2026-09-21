#define TINYTEST_NO_MAIN
#include "data_bind_native.h"
#include "reader_probe.h"

#include <salts_cmeta_data.h>
#include <tinytest.h>
#include <string.h>

typedef union BitmapWorkspace {
  max_align_t alignment;
  unsigned char bytes[4096];
} BitmapWorkspace;

/* #58/#59: exercise bitmap byte boundaries through the existing production
 * planner and decoder. These descriptors describe only the local test rows;
 * no alternate planner, reader implementation or runtime policy is used. */
enum { BITMAP_FIELDS = 9, BITMAP_BRANCHES = 2, BITMAP_STEPS = 48 };
typedef enum BitmapFault {
  BITMAP_VALID,
  BITMAP_DUPLICATE_LAST,
  BITMAP_MISSING_LAST
} BitmapFault;

static void require_bitmap_boundary(size_t field_count, size_t branches,
                                    BitmapFault fault) {
  static const char *const names[BITMAP_FIELDS] = {
      "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8"};
  static const char *const branch_names[BITMAP_BRANCHES] = {"left", "right"};
  const size_t leaf_bytes = (field_count != 0u ? field_count : 1u) * sizeof(int32_t);
  const size_t leaf_bitmap = field_count > 8u ? 2u : (field_count != 0u ? 1u : 0u);
  const size_t copies = branches != 0u ? branches : 1u;
  const size_t value_count = (field_count != 0u ? field_count : 1u) * copies;
  const size_t depth = (field_count != 0u ? 2u : 1u) + (branches != 0u ? 1u : 0u);
  const size_t nodes = branches != 0u ? 1u + copies * (1u + field_count)
                                     : 1u + field_count;
  const cmeta_type_identity leaf_id = CMETA_TYPE_ID_ATOM_INIT("test.bitmap.Leaf");
  const cmeta_type_identity root_id = CMETA_TYPE_ID_ATOM_INIT("test.bitmap.Parent");
  const cmeta_type_desc leaf_type = {
      .name = "BitmapLeaf", .size = leaf_bytes, .align = _Alignof(int32_t),
      .kind = CMETA_T_OBJECT, .identity = &leaf_id};
  const cmeta_type_desc parent_type = {
      .name = "BitmapParent", .size = leaf_bytes * copies, .align = _Alignof(int32_t),
      .kind = CMETA_T_OBJECT, .identity = &root_id};
  cmeta_field_desc fields[BITMAP_FIELDS] = {0};
  cmeta_data_field_desc values[BITMAP_FIELDS] = {0};
  cmeta_field_desc parent_fields[BITMAP_BRANCHES] = {0};
  cmeta_data_field_desc parent_values[BITMAP_BRANCHES] = {0};
  const cmeta_struct_desc leaf_layout = {
      "BitmapLeaf", leaf_bytes, _Alignof(int32_t), fields, field_count};
  const cmeta_data_struct_shape leaf_shape = {&leaf_layout, values, field_count};
  const cmeta_data_desc leaf = {
      .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.bitmap.Leaf.data", .display_name = "BitmapLeaf",
      .kind = CMETA_DATA_STRUCT, .storage_type = &leaf_type, .shape = &leaf_shape};
  const cmeta_struct_desc parent_layout = {
      "BitmapParent", leaf_bytes * copies, _Alignof(int32_t), parent_fields, branches};
  const cmeta_data_struct_shape parent_shape = {
      &parent_layout, parent_values, branches};
  const cmeta_data_desc parent = {
      .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.bitmap.Parent.data", .display_name = "BitmapParent",
      .kind = CMETA_DATA_STRUCT, .storage_type = &parent_type, .shape = &parent_shape};
  const cmeta_data_desc *shape = branches != 0u ? &parent : &leaf;
  NativeReaderProbeStep steps[BITMAP_STEPS];
  NativeReaderProbe source = {0};
  cserde_reader input = {0};
  BitmapWorkspace memory;
  DataBindNativeOptions limits = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeRequirements required = DATA_BIND_NATIVE_REQUIREMENTS_INIT;
  DataBindNativeDiagnostic error = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  int32_t output[BITMAP_FIELDS * BITMAP_BRANCHES] = {0};
  size_t n = 0u;
  size_t i, branch;
  DataBindStatus status;

  check_true(field_count <= BITMAP_FIELDS);
  check_true(branches <= BITMAP_BRANCHES);
  check_true(fault == BITMAP_VALID || (field_count == BITMAP_FIELDS && branches == 0u));
  for (i = 0u; i < field_count; ++i) {
    fields[i] = (cmeta_field_desc){
        .name = names[i], .type_name = "int32_t", .offset = i * sizeof(int32_t),
        .size = sizeof(int32_t), .align = _Alignof(int32_t),
        .type = salts_int32_cmeta_data.storage_type};
    values[i] = (cmeta_data_field_desc){names[i], names[i], i * sizeof(int32_t),
                                       &salts_int32_cmeta_data};
  }
  for (i = 0u; i < branches; ++i) {
    parent_fields[i] = (cmeta_field_desc){
        .name = branch_names[i], .type_name = "BitmapLeaf", .offset = i * leaf_bytes,
        .size = leaf_bytes, .align = _Alignof(int32_t), .type = &leaf_type};
    parent_values[i] = (cmeta_data_field_desc){
        branch_names[i], branch_names[i], i * leaf_bytes, &leaf};
  }
  if (branches != 0u) steps[n++] = native_reader_probe_token(CSERDE_MAP_BEGIN);
  for (branch = 0u; branch < copies; ++branch) {
    if (branches != 0u)
      steps[n++] = native_reader_probe_slice(CSERDE_STRING,
          (const unsigned char *)branch_names[branch], strlen(branch_names[branch]),
          CSERDE_VIEW_STABLE);
    steps[n++] = native_reader_probe_token(CSERDE_MAP_BEGIN);
    /* Reverse order prevents accidentally equating token order and field index. */
    for (i = field_count; i != 0u; --i) {
      const size_t index = i - 1u;
      if (fault == BITMAP_MISSING_LAST && index == BITMAP_FIELDS - 1u) continue;
      steps[n++] = native_reader_probe_slice(CSERDE_STRING,
          (const unsigned char *)names[index], strlen(names[index]), CSERDE_VIEW_STABLE);
      steps[n++] = native_reader_probe_sint((int64_t)(11u + index + 100u * branch));
    }
    if (fault == BITMAP_DUPLICATE_LAST) {
      steps[n++] = native_reader_probe_slice(CSERDE_STRING,
          (const unsigned char *)names[BITMAP_FIELDS - 1u], 2u, CSERDE_VIEW_STABLE);
      steps[n++] = native_reader_probe_sint(999);
    }
    steps[n++] = native_reader_probe_token(CSERDE_MAP_END);
  }
  if (branches != 0u) steps[n++] = native_reader_probe_token(CSERDE_MAP_END);
  check_true(n <= BITMAP_STEPS);
  limits.workspace = memory.bytes;
  limits.workspace_bytes = sizeof(memory.bytes);
  limits.max_depth = depth;
  limits.max_items = nodes;
  limits.max_owned_bytes = 0u;
  check_equal(native_reader_probe_open(&source, steps, n, &input), CSERDE_OK);
  check_equal(data_bind_native_measure(&limits, shape, &required, &error), DATA_BIND_OK);
  check_equal(source.calls, 0u);
  check_equal(required.descriptor_depth, depth);
  check_equal(required.container_depth, branches != 0u ? 2u : 1u);
  check_equal(required.descriptor_nodes, nodes);
  /* A parent's bitmap stays active, but completed siblings reuse their bytes. */
  check_equal(required.field_tracking_bytes, leaf_bitmap + (branches != 0u ? 1u : 0u));
  check_equal(required.staging_bytes, leaf_bytes * copies);
  check_equal((uintptr_t)memory.bytes % required.workspace_alignment, 0u);
  check_true(required.decode_bytes <= sizeof(memory.bytes));
  check_true(required.lifecycle_bytes != 0u);

  limits.workspace_bytes = required.decode_bytes - 1u;
  check_equal(data_bind_native_decode_bounded(&limits, shape, &input, output,
              sizeof(output), 0u, &error), DATA_BIND_ERR_LIMIT);
  check_equal(source.calls, 0u);
  for (i = 0u; i < value_count; ++i) check_equal(output[i], 0);

  limits.workspace_bytes = required.decode_bytes;
  status = data_bind_native_decode_bounded(&limits, shape, &input, output,
                                           sizeof(output), 0u, &error);
  if (fault == BITMAP_VALID) {
    check_equal(status, DATA_BIND_OK);
    check_equal(source.calls, n);
    for (branch = 0u; branch < copies; ++branch)
      for (i = 0u; i < field_count; ++i)
        check_equal(output[branch * field_count + i], (int32_t)(11u + i + 100u * branch));
  } else {
    check_equal(status, fault == BITMAP_DUPLICATE_LAST ? DATA_BIND_ERR_PARSE
                                                      : DATA_BIND_ERR_TYPE_MISMATCH);
    check_not_null(strstr(error.error.path, "f8"));
    /* Duplicate detection stops at the second key, before reading its value. */
    check_equal(source.calls, fault == BITMAP_DUPLICATE_LAST ? n - 2u : n);
    check_true(source.calls >= 18u);
    for (i = 0u; i < value_count; ++i) check_equal(output[i], 0);
  }
  limits.workspace_bytes = required.lifecycle_bytes;
  check_equal(data_bind_native_clear(&limits, shape, output, sizeof(output), &error), DATA_BIND_OK);
  for (i = 0u; i < value_count; ++i) check_equal(output[i], 0);
}

spec("DataBind field bitmap byte boundaries and nested lifetimes") {
  it("fits exactly eight scalar fields in one bitmap byte") {
    require_bitmap_boundary(8u, 0u, BITMAP_VALID);
  }
  it("requires two bitmap bytes for nine reverse-ordered scalar fields") {
    require_bitmap_boundary(9u, 0u, BITMAP_VALID);
  }
  it("adds the active parent bitmap to a nine-field child bitmap") {
    require_bitmap_boundary(9u, 1u, BITMAP_VALID);
  }
  it("reuses child bitmap workspace across two completed siblings") {
    require_bitmap_boundary(9u, 2u, BITMAP_VALID);
  }
  it("rejects a duplicate ninth field after all fields were decoded") {
    require_bitmap_boundary(9u, 0u, BITMAP_DUPLICATE_LAST);
  }
  it("rejects a missing ninth field without publishing the eight earlier values") {
    require_bitmap_boundary(9u, 0u, BITMAP_MISSING_LAST);
  }
  it("reports an empty root as a container without field tracking") {
    require_bitmap_boundary(0u, 0u, BITMAP_VALID);
  }
  it("counts an empty nested Struct in container depth even without scalar leaves") {
    require_bitmap_boundary(0u, 1u, BITMAP_VALID);
  }
  it("does not accumulate scratch for two empty sibling Structs") {
    require_bitmap_boundary(0u, 2u, BITMAP_VALID);
  }
  it("reports no container depth or bitmap for a scalar root") {
    BitmapWorkspace memory;
    DataBindNativeOptions limits = DATA_BIND_NATIVE_OPTIONS_INIT;
    DataBindNativeRequirements required = DATA_BIND_NATIVE_REQUIREMENTS_INIT;
    DataBindNativeDiagnostic error = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    NativeReaderProbe source = {0};
    cserde_reader input = {0};
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int32_t output = 0;
    limits.workspace = memory.bytes;
    limits.workspace_bytes = sizeof(memory.bytes);
    limits.max_depth = 1u;
    limits.max_items = 1u;
    check_equal(native_reader_probe_open(&source, steps, 1u, &input), CSERDE_OK);
    check_equal(data_bind_native_measure(&limits, &salts_int32_cmeta_data,
                                         &required, &error), DATA_BIND_OK);
    check_equal(required.container_depth, 0u);
    check_equal(required.field_tracking_bytes, 0u);
    check_equal(required.descriptor_depth, 1u);
    check_equal(required.descriptor_nodes, 1u);
    check_equal(source.calls, 0u);
    limits.workspace_bytes = required.decode_bytes - 1u;
    check_equal(data_bind_native_decode_bounded(&limits, &salts_int32_cmeta_data,
                &input, &output, sizeof(output), 0u, &error), DATA_BIND_ERR_LIMIT);
    check_equal(source.calls, 0u);
    check_equal(output, 0);
    limits.workspace_bytes = required.decode_bytes;
    check_equal(data_bind_native_decode_bounded(&limits, &salts_int32_cmeta_data,
                &input, &output, sizeof(output), 0u, &error), DATA_BIND_OK);
    check_equal(source.calls, 1u);
    check_equal(output, 7);
    limits.workspace_bytes = required.lifecycle_bytes;
    check_equal(data_bind_native_clear(&limits, &salts_int32_cmeta_data,
                                      &output, sizeof(output), &error), DATA_BIND_OK);
    check_equal(output, 0);
  }
}
