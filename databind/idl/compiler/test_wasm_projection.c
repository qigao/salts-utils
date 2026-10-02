#include "compiler_core.h"
#include "projection.h"
#include "wasm_projection.h"

#include "salts_fs.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WASM_PROJECTION_SCHEMA
#error "WASM_PROJECTION_SCHEMA is required"
#endif
#ifndef WASM_OPTIONAL_SCHEMA
#error "WASM_OPTIONAL_SCHEMA is required"
#endif
#ifndef WASM_ERROR_SCHEMA
#error "WASM_ERROR_SCHEMA is required"
#endif
#ifndef WASM_MULTI_RESPONSE_SCHEMA
#error "WASM_MULTI_RESPONSE_SCHEMA is required"
#endif

static int put_u8(unsigned char *out, size_t cap, size_t *used, uint8_t v) {
  if (out == NULL || used == NULL || *used >= cap) return 0;
  out[(*used)++] = v;
  return 1;
}

static int put_uleb(
    unsigned char *out, size_t cap, size_t *used, uint32_t value) {
  do {
    uint8_t byte = (uint8_t)(value & 0x7fu);
    value >>= 7u;
    if (value != 0u) byte |= 0x80u;
    if (!put_u8(out, cap, used, byte)) return 0;
  } while (value != 0u);
  return 1;
}

static int put_bytes(
    unsigned char *out, size_t cap, size_t *used,
    const void *data, size_t size) {
  if (out == NULL || used == NULL ||
      (size != 0u && data == NULL) || size > cap - *used)
    return 0;
  if (size != 0u) memcpy(out + *used, data, size);
  *used += size;
  return 1;
}

static int put_name(
    unsigned char *out, size_t cap, size_t *used, const char *name) {
  size_t length = name != NULL ? strlen(name) : 0u;
  return length <= UINT32_MAX &&
         put_uleb(out, cap, used, (uint32_t)length) &&
         put_bytes(out, cap, used, name, length);
}

static int write_core_add_module(
    const char *path, const char *export_name) {
  static const unsigned char header[] = {
      0x00u,0x61u,0x73u,0x6du,0x01u,0x00u,0x00u,0x00u};
  static const unsigned char type_section[] = {
      0x01u,0x07u,0x01u,0x60u,0x02u,0x7fu,0x7fu,0x01u,0x7fu};
  static const unsigned char func_section[] = {
      0x03u,0x02u,0x01u,0x00u};
  static const unsigned char code_section[] = {
      0x0au,0x09u,0x01u,0x07u,0x00u,
      0x20u,0x00u,0x20u,0x01u,0x6au,0x0bu};
  unsigned char bytes[512];
  unsigned char export_payload[256];
  size_t used = 0u;
  size_t export_used = 0u;
  FILE *file;

  if (!put_bytes(bytes, sizeof(bytes), &used, header, sizeof(header)) ||
      !put_bytes(bytes, sizeof(bytes), &used,
                 type_section, sizeof(type_section)) ||
      !put_bytes(bytes, sizeof(bytes), &used,
                 func_section, sizeof(func_section)) ||
      !put_uleb(export_payload, sizeof(export_payload), &export_used, 1u) ||
      !put_name(export_payload, sizeof(export_payload), &export_used,
                export_name) ||
      !put_u8(export_payload, sizeof(export_payload), &export_used, 0x00u) ||
      !put_uleb(export_payload, sizeof(export_payload), &export_used, 0u) ||
      export_used > UINT32_MAX ||
      !put_u8(bytes, sizeof(bytes), &used, 0x07u) ||
      !put_uleb(bytes, sizeof(bytes), &used, (uint32_t)export_used) ||
      !put_bytes(bytes, sizeof(bytes), &used,
                 export_payload, export_used) ||
      !put_bytes(bytes, sizeof(bytes), &used,
                 code_section, sizeof(code_section)))
    return 0;

  file = fopen(path, "wb");
  if (file == NULL) return 0;
  if (fwrite(bytes, 1u, used, file) != used ||
      fflush(file) != 0 || fclose(file) != 0)
    return 0;
  return 1;
}

static int bytes_contains(
    const unsigned char *data, size_t size, const char *needle) {
  size_t needle_size;
  size_t i;
  if (data == NULL || needle == NULL) return 0;
  needle_size = strlen(needle);
  if (needle_size == 0u || needle_size > size) return 0;
  for (i = 0u; i + needle_size <= size; ++i)
    if (memcmp(data + i, needle, needle_size) == 0) return 1;
  return 0;
}

static void cleanup_outputs(
    const char *component,
    const char *host_header,
    const char *host_source,
    const char *guest_header,
    const char *core) {
  (void)salts_fs_unlink(component);
  (void)salts_fs_unlink(host_header);
  (void)salts_fs_unlink(host_source);
  (void)salts_fs_unlink(guest_header);
  if (core != NULL) (void)salts_fs_unlink(core);
}

static int run_projection(
    const char *schema,
    const char *component_id,
    const char *core_path,
    const char *component_output,
    const char *host_header,
    const char *host_source,
    const char *guest_header) {
  Node *root = NULL;
  IdlContract *contract = NULL;
  char *schema_data = NULL;
  databind_compiler_projection_input input = {0};
  databind_compiler_wasm_config config = {
      .component_id = component_id,
      .native_header = "wasm_projection_native.h",
      .core_module_path = core_path,
      .host_header_output = host_header,
      .host_source_output = host_source,
      .guest_header_output = guest_header,
      .symbol_prefix = "databind_wasm_projection"};
  databind_compiler_projection_request request = {
      .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
             DATABIND_COMPILER_ARTIFACT_WASM},
      .output = component_output,
      .config = &config};
  databind_compiler_projection_backend backend =
      DATABIND_COMPILER_WASM_BACKEND;
  int status;

  if (databind_compiler_parse_contract_file(
          schema, &root, &contract, &schema_data) != 0)
    return -2;
  input.contract = contract;
  status = databind_compiler_projection_run(
      &input, &request, 1u, &backend, 1u);
  idl_contract_destroy(contract);
  node_free(root);
  free(schema_data);
  return status;
}

spec("DataBind WASM projection backend") {
  it("wraps one scalar Service Core module as a Component and host capability") {
    static const char core[] = "databind_wasm_projection_core.wasm";
    static const char component[] = "databind_wasm_projection.wasm";
    static const char host_h[] = "databind_wasm_projection.wasm.h";
    static const char host_c[] = "databind_wasm_projection.wasm.c";
    static const char guest_h[] = "databind_wasm_projection.wasm_guest.h";
    static const char export_symbol[] =
        "databind_14_WasmProjection_4_Calc_3_Add";
    salts_fs_buf_t generated = {0};

    cleanup_outputs(component, host_h, host_c, guest_h, core);
    check_true(write_core_add_module(core, export_symbol));

    check_equal(run_projection(
                    WASM_PROJECTION_SCHEMA,
                    "WasmProjection.Calculator",
                    core, component, host_h, host_c, guest_h),
                0);

    check_equal(salts_fs_read_file(component, &generated), 0);
    check(generated.len >= 8u);
    check_equal((unsigned char)generated.base[0], 0x00u);
    check_equal((unsigned char)generated.base[1], 0x61u);
    check_equal((unsigned char)generated.base[2], 0x73u);
    check_equal((unsigned char)generated.base[3], 0x6du);
    check_equal((unsigned char)generated.base[4], 0x0du);
    check_true(bytes_contains(
        (const unsigned char *)generated.base, generated.len,
        "WasmProjection.Calc.Add"));
    check_true(bytes_contains(
        (const unsigned char *)generated.base, generated.len,
        export_symbol));
    salts_fs_buf_free(&generated);

    check_equal(salts_fs_read_file(host_c, &generated), 0);
    check_not_null(strstr(
        generated.base, "turbowasm_component_instance_invoke"));
    check_not_null(strstr(
        generated.base, "DataBindNativeExecution"));
    check_not_null(strstr(
        generated.base, "WasmProjection.Calc.Add"));
    salts_fs_buf_free(&generated);

    check_equal(salts_fs_read_file(guest_h, &generated), 0);
    check_not_null(strstr(generated.base, export_symbol));
    check_not_null(strstr(
        generated.base, "uint32_t left, uint32_t right"));
    salts_fs_buf_free(&generated);

    cleanup_outputs(component, host_h, host_c, guest_h, core);
  }

  it("rejects optional, typed-error and multi-field response shapes") {
    static const char core[] = "databind_wasm_reject_core.wasm";
    static const char component[] = "databind_wasm_reject.wasm";
    static const char host_h[] = "databind_wasm_reject.wasm.h";
    static const char host_c[] = "databind_wasm_reject.wasm.c";
    static const char guest_h[] = "databind_wasm_reject.wasm_guest.h";

    cleanup_outputs(component, host_h, host_c, guest_h, core);
    check_true(write_core_add_module(
        core, "databind_12_WasmOptional_4_Calc_3_Add"));

    check_equal(run_projection(
                    WASM_OPTIONAL_SCHEMA,
                    "WasmOptional.Calculator",
                    core, component, host_h, host_c, guest_h),
                -1);
    check(salts_fs_access(component, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(host_h, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(host_c, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(guest_h, SALTS_FS_ACCESS_EXISTS) != 0);

    check_equal(run_projection(
                    WASM_ERROR_SCHEMA,
                    "WasmError.Calculator",
                    core, component, host_h, host_c, guest_h),
                -1);
    check(salts_fs_access(component, SALTS_FS_ACCESS_EXISTS) != 0);

    check_equal(run_projection(
                    WASM_MULTI_RESPONSE_SCHEMA,
                    "WasmMulti.Calculator",
                    core, component, host_h, host_c, guest_h),
                -1);
    check(salts_fs_access(component, SALTS_FS_ACCESS_EXISTS) != 0);

    cleanup_outputs(component, host_h, host_c, guest_h, core);
  }
}
