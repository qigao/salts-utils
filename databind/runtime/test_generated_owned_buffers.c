#include "generated_owned_buffers.h"
#include "data_bind_native.h"
#include "tinytest.h"

#include <cmeta/data.h>
#include <cstl/byte_buffer.h>
#include <tstr.h>

#include <stdlib.h>
#include <string.h>

spec("generated owned buffers use canonical Salts CMeta lifecycle") {
  it("initializes moves and clears tstr and byte-buffer storage through one struct graph") {
    static const unsigned char payload[] = {0x41u, 0x00u, 0x42u, 0xffu};
    const cmeta_data_desc *native = NULL;
    const cmeta_data_struct_shape *shape = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
    NativeOwnedBufferRecord_t source = {0};
    NativeOwnedBufferRecord_t destination = {0};
    size_t workspace_bytes = 0u;
    void *workspace = NULL;

    check_equal(NativeOwnedBufferRecord_cmeta_data(&native, &error), DATA_BIND_OK);
    check_not_null(native);
    if (native == NULL) return;

    check_true(cmeta_data_desc_valid(native));
    check_equal(native->kind, CMETA_DATA_STRUCT);
    check_true(cmeta_data_value_move_supported(native));
    shape = (const cmeta_data_struct_shape *)native->shape;
    check_not_null(shape);
    if (shape != NULL) {
      check_equal(shape->field_count, (size_t)3u);
      check_true(cmeta_data_desc_equal(shape->fields[1].value,
                                       &salts_tstr_cmeta_data));
      check_true(cmeta_data_desc_equal(shape->fields[2].value,
                                       &stl_byte_buffer_cmeta_data));
    }

    options.max_depth = 8u;
    options.max_items = 32u;
    options.max_owned_bytes = 4096u;
    check_equal(data_bind_native_probe_workspace_size(
                    options.max_depth, &workspace_bytes),
                DATA_BIND_OK);
    workspace = malloc(workspace_bytes);
    check_not_null(workspace);
    if (workspace == NULL) return;
    options.workspace = workspace;
    options.workspace_bytes = workspace_bytes;

    check_equal(data_bind_native_init(
                    &options, native, &source, sizeof(source), &diagnostic),
                DATA_BIND_OK);
    check_equal(data_bind_native_init(
                    &options, native, &destination, sizeof(destination),
                    &diagnostic),
                DATA_BIND_OK);

    check_null(source.text);
    check_equal(stl_byte_buffer_size(&source.payload), (size_t)0u);
    check_null(destination.text);
    check_equal(stl_byte_buffer_size(&destination.payload), (size_t)0u);

    source.id = 7u;
    source.text = tstr_dup("canonical-owned");
    check_not_null(source.text);
    check_equal(stl_byte_buffer_resize(&source.payload, sizeof(payload)), STL_OK);
    if (stl_byte_buffer_data(&source.payload) != NULL)
      memcpy(stl_byte_buffer_data(&source.payload), payload, sizeof(payload));

    check_equal(cmeta_data_value_move(native, &destination, &source), CMETA_OK);

    check_equal(source.id, 0u);
    check_null(source.text);
    check_equal(stl_byte_buffer_size(&source.payload), (size_t)0u);

    check_equal(destination.id, 7u);
    check_not_null(destination.text);
    if (destination.text != NULL) {
      check_equal(tstr_len(destination.text), strlen("canonical-owned"));
      check(memcmp(destination.text, "canonical-owned",
                   strlen("canonical-owned")) == 0);
    }
    check_equal(stl_byte_buffer_size(&destination.payload), sizeof(payload));
    if (stl_byte_buffer_data_const(&destination.payload) != NULL)
      check(memcmp(stl_byte_buffer_data_const(&destination.payload),
                   payload, sizeof(payload)) == 0);

    check_equal(data_bind_native_clear(
                    &options, native, &source, sizeof(source), &diagnostic),
                DATA_BIND_OK);
    check_equal(data_bind_native_clear(
                    &options, native, &destination, sizeof(destination),
                    &diagnostic),
                DATA_BIND_OK);
    check_equal(destination.id, 0u);
    check_null(destination.text);
    check_equal(stl_byte_buffer_size(&destination.payload), (size_t)0u);

    free(workspace);
  }
}
